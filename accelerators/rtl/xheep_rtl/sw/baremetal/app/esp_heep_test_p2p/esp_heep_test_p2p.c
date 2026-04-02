/* Copyright (c) 2011-2024 Columbia University, System Level Design Group */
/* SPDX-License-Identifier: Apache-2.0 */

// Note: this app has to be ran in an ESP system with 2 xheep_rtl instances.
// The apps to be ran by the X-Heep tiles are "esp_heep_profile_p2p_cons" and 
// "esp_heep_profile_p2p_prod". To compile them:
// make sw XHEEP_APPS="esp_heep_profile_p2p_cons esp_heep_profile_p2p_prod" ESP_BAREMETAL_APP="esp_heep_test_p2p"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_accelerator.h"
#include "esp_probe.h"
#include "monitors.h"
#include "xheep_rtl_accelerator.h"
#include "xheep_fw_esp_heep_profile_p2p_prod.h"
#include "xheep_fw_esp_heep_profile_p2p_cons.h"
#include "xheep_common.h"

#define ACC_COMPAT "sld,xheep_rtl"
#define ACC_VENDOR VENDOR_SLD
#define ACC_DEVID  0x068

#define PROD_INSTANCE_IDX 0u
#define CONS_INSTANCE_IDX 1u

#define CHUNK_SHIFT 20
#define CHUNK_SIZE  (1u << CHUNK_SHIFT)
#define NCHUNK(_sz) (((_sz) + CHUNK_SIZE - 1u) >> CHUNK_SHIFT)

#define ACC_RESERVED_BASE 0x82000000u
#define ACC_RESERVED_SIZE 0x01000000u
#define PROFILE_TIMEOUT_POLLS 50000000u

typedef struct {
    esp_acc_stats_t acc;
    uint32_t noc_injects[NOC_PLANES];
    uint32_t noc_backpressure[NOC_PLANES][NOC_QUEUES];
} acc_profile_t;

static inline uint64_t profile_host_cycles(void)
{
#ifdef __riscv
#if __riscv_xlen == 32
    uint32_t hi0 = 0u;
    uint32_t hi1 = 0u;
    uint32_t lo = 0u;

    asm volatile("csrr %0, mcycleh" : "=r"(hi0));
    asm volatile("csrr %0, mcycle" : "=r"(lo));
    asm volatile("csrr %0, mcycleh" : "=r"(hi1));
    if (hi0 != hi1) {
        asm volatile("csrr %0, mcycle" : "=r"(lo));
        hi0 = hi1;
    }
    return ((uint64_t)hi0 << 32) | lo;
#else
    uint64_t cycles = 0u;
    asm volatile("csrr %0, mcycle" : "=r"(cycles));
    return cycles;
#endif
#else
    return 0u;
#endif
}

static uint64_t cycles_diff(uint64_t start, uint64_t end)
{
    if (end >= start) return end - start;
    return (UINT64_MAX - start + 1u) + end;
}

static unsigned tile_index_from_dev(struct esp_device *dev)
{
    return esp_get_y(dev) * SOC_COLS + esp_get_x(dev);
}

static unsigned read_mon_tile(unsigned tile_index, unsigned mon_index)
{
    esp_monitor_args_t mon_args = {0};
    mon_args.read_mode = ESP_MON_READ_SINGLE;
    mon_args.tile_index = tile_index;
    mon_args.mon_index = mon_index;
    return esp_monitor(mon_args, NULL);
}

static void read_acc_profile(struct esp_device *dev, acc_profile_t *profile)
{
    unsigned tile_index = tile_index_from_dev(dev);

    profile->acc.acc_tlb = read_mon_tile(tile_index, MON_ACC_TLB_INDEX);
    profile->acc.acc_mem_lo = read_mon_tile(tile_index, MON_ACC_MEM_LO_INDEX);
    profile->acc.acc_mem_hi = read_mon_tile(tile_index, MON_ACC_MEM_HI_INDEX);
    profile->acc.acc_tot_lo = read_mon_tile(tile_index, MON_ACC_TOT_LO_INDEX);
    profile->acc.acc_tot_hi = read_mon_tile(tile_index, MON_ACC_TOT_HI_INDEX);
    profile->acc.acc_invocations = read_mon_tile(tile_index, MON_ACC_INVOCATIONS);
    for (unsigned p = 0; p < NOC_PLANES; ++p) {
        profile->noc_injects[p] = read_mon_tile(tile_index, MON_NOC_TILE_INJECT_BASE_INDEX + p);
    }
    for (unsigned q = 0; q < NOC_QUEUES; ++q) {
        for (unsigned p = 0; p < NOC_PLANES; ++p) {
            unsigned mon_index = MON_NOC_QUEUES_FULL_BASE_INDEX + q * NOC_PLANES + p;
            profile->noc_backpressure[p][q] = read_mon_tile(tile_index, mon_index);
        }
    }
}

static uint64_t acc_cycles_u64(uint32_t lo, uint32_t hi)
{
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t total_cycles_diff(const acc_profile_t *start, const acc_profile_t *end)
{
    return cycles_diff(acc_cycles_u64(start->acc.acc_tot_lo, start->acc.acc_tot_hi),
                       acc_cycles_u64(end->acc.acc_tot_lo, end->acc.acc_tot_hi));
}

static void print_acc_profile_diff(const char *label,
                                   const char *role,
                                   struct esp_device *dev,
                                   const acc_profile_t *start,
                                   const acc_profile_t *end)
{
    uint64_t mem_cycles = cycles_diff(acc_cycles_u64(start->acc.acc_mem_lo, start->acc.acc_mem_hi),
                                      acc_cycles_u64(end->acc.acc_mem_lo, end->acc.acc_mem_hi));
    uint64_t tot_cycles = total_cycles_diff(start, end);
    uint64_t compute_cycles = (tot_cycles >= mem_cycles) ? (tot_cycles - mem_cycles) : 0u;
    uint64_t noc_injects_total = 0u;
    uint64_t noc_backpressure_total = 0u;

    for (unsigned p = 0; p < NOC_PLANES; ++p) {
        noc_injects_total += sub_monitor_vals(start->noc_injects[p], end->noc_injects[p]);
        for (unsigned q = 0; q < NOC_QUEUES; ++q) {
            noc_backpressure_total +=
                sub_monitor_vals(start->noc_backpressure[p][q], end->noc_backpressure[p][q]);
        }
    }

    printf("[PROFILE][MON] %s %s tile=(y=%u x=%u): total=%llu mem=%llu compute=%llu tlb=%u invocations=%u noc_injects=%llu noc_backpressure=%llu\n",
           label, role, esp_get_y(dev), esp_get_x(dev),
           (unsigned long long)tot_cycles,
           (unsigned long long)mem_cycles,
           (unsigned long long)compute_cycles,
           sub_monitor_vals(start->acc.acc_tlb, end->acc.acc_tlb),
           sub_monitor_vals(start->acc.acc_invocations, end->acc.acc_invocations),
           (unsigned long long)noc_injects_total,
           (unsigned long long)noc_backpressure_total);
}

static void p2p_setup(struct esp_device *prod, struct esp_device *cons)
{
    esp_p2p_reset(prod);
    esp_p2p_set_mcast_ndests(prod, 1u);

    esp_p2p_reset(cons);
    esp_p2p_set_nsrcs(cons, 2u);
    esp_p2p_set_y(cons, 1u, esp_get_y(prod));
    esp_p2p_set_x(cons, 1u, esp_get_x(prod));
    esp_yx_reg_set_y(cons, esp_get_y(prod), 0u, 1u);
    esp_yx_reg_set_x(cons, esp_get_x(prod), 0u, 1u);
}

static void p2p_reset_both(struct esp_device *prod, struct esp_device *cons)
{
    esp_p2p_reset(prod);
    esp_p2p_reset(cons);
}

static int wait_for_device_done(struct esp_device *dev, const char *role, unsigned *out_polls)
{
    unsigned polls = 0u;

    while ((ioread32(dev, STATUS_REG) & STATUS_MASK_DONE) == 0u) {
        if (++polls >= PROFILE_TIMEOUT_POLLS) {
            iowrite32(dev, CMD_REG, 0u);
            printf("Error: %s timeout\n", role);
            return -1;
        }
    }

    iowrite32(dev, CMD_REG, 0u);
    if (out_polls != NULL) *out_polls = polls;
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n=== ESP-HEEP profile P2P baremetal ===\n");
    printf("mode=%s size=%u bytes\n",
           ESP_HEEP_PROFILE_P2P_MODE_NAME,
           (unsigned)ESP_HEEP_PROFILE_P2P_SIZE_BYTES);

    struct esp_device *devs = NULL;
    int ndev = probe(&devs, ACC_VENDOR, ACC_DEVID, ACC_COMPAT);
    if (ndev <= (int)CONS_INSTANCE_IDX) {
        printf("Error: need at least two quadrilatero accelerators, found %d\n", ndev);
        return 1;
    }

    struct esp_device *prod = &devs[PROD_INSTANCE_IDX];
    struct esp_device *cons = &devs[CONS_INSTANCE_IDX];
    unsigned max_chunks_prod = ioread32(prod, PT_NCHUNK_MAX_REG);
    unsigned max_chunks_cons = ioread32(cons, PT_NCHUNK_MAX_REG);
    if (max_chunks_prod == 0u || max_chunks_cons == 0u) {
        printf("Error: scatter-gather DMA disabled for producer or consumer\n");
        return 1;
    }

    xheep_fw_section_t prod_secs[XHEEP_FW_ESP_HEEP_PROFILE_P2P_PROD_NUM_SECTIONS];
    xheep_fw_section_t cons_secs[XHEEP_FW_ESP_HEEP_PROFILE_P2P_CONS_NUM_SECTIONS];
    for (unsigned s = 0; s < XHEEP_FW_ESP_HEEP_PROFILE_P2P_PROD_NUM_SECTIONS; ++s) {
        prod_secs[s].addr = xheep_fw_esp_heep_profile_p2p_prod_sections[s].addr;
        prod_secs[s].size = xheep_fw_esp_heep_profile_p2p_prod_sections[s].size;
        prod_secs[s].data =
            (const uint8_t *)xheep_fw_esp_heep_profile_p2p_prod_sections[s].data;
    }
    for (unsigned s = 0; s < XHEEP_FW_ESP_HEEP_PROFILE_P2P_CONS_NUM_SECTIONS; ++s) {
        cons_secs[s].addr = xheep_fw_esp_heep_profile_p2p_cons_sections[s].addr;
        cons_secs[s].size = xheep_fw_esp_heep_profile_p2p_cons_sections[s].size;
        cons_secs[s].data =
            (const uint8_t *)xheep_fw_esp_heep_profile_p2p_cons_sections[s].data;
    }

    size_t fw_buffer_size_prod =
        xheep_fw_size(prod_secs, XHEEP_FW_ESP_HEEP_PROFILE_P2P_PROD_NUM_SECTIONS);
    size_t fw_buffer_size_cons =
        xheep_fw_size(cons_secs, XHEEP_FW_ESP_HEEP_PROFILE_P2P_CONS_NUM_SECTIONS);
    size_t data_buffer_size = ESP_HEEP_PROFILE_P2P_SHARED_BYTES;

    unsigned nchunk_prod = NCHUNK(fw_buffer_size_prod);
    unsigned nchunk_cons = NCHUNK(fw_buffer_size_cons);
    unsigned nchunk_data = NCHUNK(data_buffer_size);
    if (nchunk_prod > max_chunks_prod || nchunk_cons > max_chunks_cons ||
        nchunk_data > max_chunks_prod || nchunk_data > max_chunks_cons) {
        printf("Error: not enough TLB entries (prod=%u cons=%u data=%u max_prod=%u max_cons=%u)\n",
               nchunk_prod, nchunk_cons, nchunk_data, max_chunks_prod, max_chunks_cons);
        return 1;
    }

    uintptr_t free_mem_ptr = ACC_RESERVED_BASE;
    uintptr_t mem_end = ACC_RESERVED_BASE + ACC_RESERVED_SIZE;

    free_mem_ptr = (free_mem_ptr + CHUNK_SIZE - 1u) & ~(uintptr_t)(CHUNK_SIZE - 1u);
    uint8_t *fw_buffer_prod = (uint8_t *)free_mem_ptr;
    free_mem_ptr += (uintptr_t)nchunk_prod * CHUNK_SIZE;

    free_mem_ptr = (free_mem_ptr + CHUNK_SIZE - 1u) & ~(uintptr_t)(CHUNK_SIZE - 1u);
    uint8_t *fw_buffer_cons = (uint8_t *)free_mem_ptr;
    free_mem_ptr += (uintptr_t)nchunk_cons * CHUNK_SIZE;

    free_mem_ptr = (free_mem_ptr + CHUNK_SIZE - 1u) & ~(uintptr_t)(CHUNK_SIZE - 1u);
    uint8_t *data_buffer = (uint8_t *)free_mem_ptr;
    free_mem_ptr += (uintptr_t)nchunk_data * CHUNK_SIZE;

    unsigned **ptable_prod = (unsigned **)free_mem_ptr;
    free_mem_ptr += (uintptr_t)nchunk_prod * sizeof(unsigned *);
    free_mem_ptr = (free_mem_ptr + 7u) & ~7u;

    unsigned **ptable_cons = (unsigned **)free_mem_ptr;
    free_mem_ptr += (uintptr_t)nchunk_cons * sizeof(unsigned *);
    free_mem_ptr = (free_mem_ptr + 7u) & ~7u;

    unsigned **ptable_data = (unsigned **)free_mem_ptr;
    free_mem_ptr += (uintptr_t)nchunk_data * sizeof(unsigned *);
    free_mem_ptr = (free_mem_ptr + 7u) & ~7u;

    if (free_mem_ptr > mem_end) {
        printf("Error: out of reserved memory for X-HEEP buffers\n");
        return 1;
    }

    memset(data_buffer, 0, data_buffer_size);
    xheep_fw_flatten(fw_buffer_prod, fw_buffer_size_prod, prod_secs,
                                  XHEEP_FW_ESP_HEEP_PROFILE_P2P_PROD_NUM_SECTIONS);
    xheep_fw_flatten(fw_buffer_cons, fw_buffer_size_cons, cons_secs,
                                  XHEEP_FW_ESP_HEEP_PROFILE_P2P_CONS_NUM_SECTIONS);

    for (unsigned i = 0; i < nchunk_prod; ++i) {
        ptable_prod[i] = (unsigned *)(fw_buffer_prod + i * CHUNK_SIZE);
    }
    for (unsigned i = 0; i < nchunk_cons; ++i) {
        ptable_cons[i] = (unsigned *)(fw_buffer_cons + i * CHUNK_SIZE);
    }
    for (unsigned i = 0; i < nchunk_data; ++i) {
        ptable_data[i] = (unsigned *)(data_buffer + i * CHUNK_SIZE);
    }

    iowrite32(prod, COHERENCE_REG, ACC_COH_NONE);
    iowrite32(prod, PT_ADDRESS_EXTENDED_REG, 0u);
    iowrite32(prod, PT_ADDRESS_REG, (unsigned long)ptable_prod);
    iowrite32(prod, PT_NCHUNK_REG, nchunk_prod);
    iowrite32(prod, PT_SHIFT_REG, CHUNK_SHIFT);
    iowrite32(prod, SRC_OFFSET_REG, 0u);
    iowrite32(prod, DST_OFFSET_REG, 0u);

    iowrite32(cons, COHERENCE_REG, ACC_COH_NONE);
    iowrite32(cons, PT_ADDRESS_EXTENDED_REG, 0u);
    iowrite32(cons, PT_ADDRESS_REG, (unsigned long)ptable_cons);
    iowrite32(cons, PT_NCHUNK_REG, nchunk_cons);
    iowrite32(cons, PT_SHIFT_REG, CHUNK_SHIFT);
    iowrite32(cons, SRC_OFFSET_REG, 0u);
    iowrite32(cons, DST_OFFSET_REG, 0u);
    esp_flush(ACC_COH_NONE);

    if (!xheep_fetch_firmware(prod, fw_buffer_size_prod / 4u, 0u, false,
                                           PROFILE_TIMEOUT_POLLS)) {
        printf("Error: producer firmware fetch failed\n");
        return 1;
    }
    if (!xheep_fetch_firmware(cons, fw_buffer_size_cons / 4u, 0u, false,
                                           PROFILE_TIMEOUT_POLLS)) {
        printf("Error: consumer firmware fetch failed\n");
        return 1;
    }

    iowrite32(prod, PT_ADDRESS_REG, (unsigned long)ptable_data);
    iowrite32(prod, PT_NCHUNK_REG, nchunk_data);
    iowrite32(cons, PT_ADDRESS_REG, (unsigned long)ptable_data);
    iowrite32(cons, PT_NCHUNK_REG, nchunk_data);
    esp_flush(ACC_COH_NONE);

#if ESP_HEEP_PROFILE_P2P_USE_P2P
    p2p_setup(prod, cons);
    esp_flush(ACC_COH_NONE);

    acc_profile_t prod_start;
    acc_profile_t cons_start;
    acc_profile_t prod_end;
    acc_profile_t cons_end;
    read_acc_profile(prod, &prod_start);
    read_acc_profile(cons, &cons_start);
    uint64_t host_start = profile_host_cycles();

    xheep_program_start(prod, false);
    xheep_program_start(cons, false);
    xheep_start(prod);
    xheep_start(cons);

    unsigned polls = 0u;
    while (((ioread32(prod, STATUS_REG) & STATUS_MASK_DONE) == 0u) ||
           ((ioread32(cons, STATUS_REG) & STATUS_MASK_DONE) == 0u)) {
        if (++polls >= PROFILE_TIMEOUT_POLLS) {
            iowrite32(prod, CMD_REG, 0u);
            iowrite32(cons, CMD_REG, 0u);
            printf("Error: P2P run timeout\n");
            return 1;
        }
    }

    iowrite32(prod, CMD_REG, 0u);
    iowrite32(cons, CMD_REG, 0u);
    uint64_t host_end = profile_host_cycles();
    read_acc_profile(prod, &prod_end);
    read_acc_profile(cons, &cons_end);

    printf("[PROFILE] host_cycles=%llu polls=%u\n",
           (unsigned long long)cycles_diff(host_start, host_end), polls);
    print_acc_profile_diff("run", "producer", prod, &prod_start, &prod_end);
    print_acc_profile_diff("run", "consumer", cons, &cons_start, &cons_end);
    {
        uint64_t prod_total = total_cycles_diff(&prod_start, &prod_end);
        uint64_t cons_total = total_cycles_diff(&cons_start, &cons_end);
        uint64_t combined = (prod_total > cons_total) ? prod_total : cons_total;
        printf("[PROFILE] combined_total_cycles=max(prod=%llu, cons=%llu)=%llu\n",
               (unsigned long long)prod_total,
               (unsigned long long)cons_total,
               (unsigned long long)combined);
    }
#else
    p2p_reset_both(prod, cons);
    esp_flush(ACC_COH_NONE);

    acc_profile_t prod_start;
    acc_profile_t prod_end;
    acc_profile_t cons_start;
    acc_profile_t cons_end;
    unsigned prod_polls = 0u;
    unsigned cons_polls = 0u;

    read_acc_profile(prod, &prod_start);
    uint64_t prod_host_start = profile_host_cycles();
    xheep_program_start(prod, false);
    xheep_start(prod);
    if (wait_for_device_done(prod, "producer", &prod_polls) != 0) return 1;
    uint64_t prod_host_end = profile_host_cycles();
    read_acc_profile(prod, &prod_end);
    esp_flush(ACC_COH_NONE);

    read_acc_profile(cons, &cons_start);
    uint64_t cons_host_start = profile_host_cycles();
    xheep_program_start(cons, false);
    xheep_start(cons);
    if (wait_for_device_done(cons, "consumer", &cons_polls) != 0) return 1;
    uint64_t cons_host_end = profile_host_cycles();
    read_acc_profile(cons, &cons_end);

    printf("[PROFILE] producer_host_cycles=%llu polls=%u\n",
           (unsigned long long)cycles_diff(prod_host_start, prod_host_end), prod_polls);
    printf("[PROFILE] consumer_host_cycles=%llu polls=%u\n",
           (unsigned long long)cycles_diff(cons_host_start, cons_host_end), cons_polls);
    printf("[PROFILE] combined_host_cycles=%llu\n",
           (unsigned long long)(cycles_diff(prod_host_start, prod_host_end) +
                                cycles_diff(cons_host_start, cons_host_end)));
    print_acc_profile_diff("run", "producer", prod, &prod_start, &prod_end);
    print_acc_profile_diff("run", "consumer", cons, &cons_start, &cons_end);
    {
        uint64_t prod_total = total_cycles_diff(&prod_start, &prod_end);
        uint64_t cons_total = total_cycles_diff(&cons_start, &cons_end);
        uint64_t combined = prod_total + cons_total;
        printf("[PROFILE] combined_total_cycles=prod(%llu)+cons(%llu)=%llu\n",
               (unsigned long long)prod_total,
               (unsigned long long)cons_total,
               (unsigned long long)combined);
    }
#endif

    return 0;
}
