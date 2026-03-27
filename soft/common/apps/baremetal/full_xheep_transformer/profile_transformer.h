#ifndef PROFILE_TRANSFORMER_H
#define PROFILE_TRANSFORMER_H

#include <stdint.h>

#include "esp_accelerator.h"
#include "monitors.h"

#ifdef __riscv
static inline uint64_t profile_host_cycles(void)
{
#if __riscv_xlen == 32
    uint32_t hi0 = 0;
    uint32_t hi1 = 0;
    uint32_t lo = 0;
    asm volatile("csrr %0, mcycleh" : "=r"(hi0));
    asm volatile("csrr %0, mcycle" : "=r"(lo));
    asm volatile("csrr %0, mcycleh" : "=r"(hi1));
    if (hi0 != hi1) {
        asm volatile("csrr %0, mcycle" : "=r"(lo));
        hi0 = hi1;
    }
    return ((uint64_t)hi0 << 32) | lo;
#else
    uint64_t cycles = 0;
    asm volatile("csrr %0, mcycle" : "=r"(cycles));
    return cycles;
#endif
}
#else
static inline uint64_t profile_host_cycles(void)
{
    return 0;
}
#endif

static inline uint64_t profile_cycles_diff(uint64_t start, uint64_t end)
{
    if (end >= start) return end - start;
    return (UINT64_MAX - start + 1u) + end;
}

typedef struct xheep_quadrilatero_mon_profile {
    esp_acc_stats_t acc;
    uint32_t noc_injects[NOC_PLANES];
    uint32_t noc_backpressure[NOC_PLANES][NOC_QUEUES];
} xheep_quadrilatero_mon_profile_t;

typedef struct xheep_quadrilatero_stage_sample {
    uint16_t stage;
    uint16_t arg;
    uint64_t host_cycle;
} xheep_quadrilatero_stage_sample_t;

#define XHEEP_QUADRILATERO_STAGE_TRACE_MAX 64

static inline unsigned xheep_quadrilatero_tile_index_from_dev(struct esp_device *dev)
{
    return esp_get_y(dev) * SOC_COLS + esp_get_x(dev);
}

static inline unsigned xheep_quadrilatero_read_mon_tile(unsigned tile_index, unsigned mon_index)
{
    esp_monitor_args_t mon_args = {0};
    mon_args.read_mode  = ESP_MON_READ_SINGLE;
    mon_args.tile_index = tile_index;
    mon_args.mon_index  = mon_index;
    return esp_monitor(mon_args, NULL);
}

static inline void xheep_quadrilatero_read_mon_profile(struct esp_device *dev, xheep_quadrilatero_mon_profile_t *profile)
{
    unsigned tile_index = xheep_quadrilatero_tile_index_from_dev(dev);
    profile->acc.acc_tlb         = xheep_quadrilatero_read_mon_tile(tile_index, MON_ACC_TLB_INDEX);
    profile->acc.acc_mem_lo      = xheep_quadrilatero_read_mon_tile(tile_index, MON_ACC_MEM_LO_INDEX);
    profile->acc.acc_mem_hi      = xheep_quadrilatero_read_mon_tile(tile_index, MON_ACC_MEM_HI_INDEX);
    profile->acc.acc_tot_lo      = xheep_quadrilatero_read_mon_tile(tile_index, MON_ACC_TOT_LO_INDEX);
    profile->acc.acc_tot_hi      = xheep_quadrilatero_read_mon_tile(tile_index, MON_ACC_TOT_HI_INDEX);
    profile->acc.acc_invocations = xheep_quadrilatero_read_mon_tile(tile_index, MON_ACC_INVOCATIONS);
    for (unsigned p = 0; p < NOC_PLANES; ++p) {
        unsigned mon_index = MON_NOC_TILE_INJECT_BASE_INDEX + p;
        profile->noc_injects[p] = xheep_quadrilatero_read_mon_tile(tile_index, mon_index);
    }
    for (unsigned q = 0; q < NOC_QUEUES; ++q) {
        for (unsigned p = 0; p < NOC_PLANES; ++p) {
            unsigned mon_index = MON_NOC_QUEUES_FULL_BASE_INDEX + q * NOC_PLANES + p;
            profile->noc_backpressure[p][q] = xheep_quadrilatero_read_mon_tile(tile_index, mon_index);
        }
    }
}

static inline uint64_t xheep_quadrilatero_acc_cycles_u64(uint32_t lo, uint32_t hi)
{
    return ((uint64_t)hi << 32) | lo;
}

static inline void xheep_quadrilatero_print_mon_profile_diff(const char *label,
                                                struct esp_device *dev,
                                                const xheep_quadrilatero_mon_profile_t *start,
                                                const xheep_quadrilatero_mon_profile_t *end)
{
    uint64_t mem_start = xheep_quadrilatero_acc_cycles_u64(start->acc.acc_mem_lo, start->acc.acc_mem_hi);
    uint64_t mem_end   = xheep_quadrilatero_acc_cycles_u64(end->acc.acc_mem_lo, end->acc.acc_mem_hi);
    uint64_t tot_start = xheep_quadrilatero_acc_cycles_u64(start->acc.acc_tot_lo, start->acc.acc_tot_hi);
    uint64_t tot_end   = xheep_quadrilatero_acc_cycles_u64(end->acc.acc_tot_lo, end->acc.acc_tot_hi);

    uint64_t mem_cycles = profile_cycles_diff(mem_start, mem_end);
    uint64_t tot_cycles = profile_cycles_diff(tot_start, tot_end);
    uint64_t compute_cycles = (tot_cycles >= mem_cycles) ? (tot_cycles - mem_cycles) : 0;
    uint32_t tlb_cycles = sub_monitor_vals(start->acc.acc_tlb, end->acc.acc_tlb);
    uint32_t invocations = sub_monitor_vals(start->acc.acc_invocations, end->acc.acc_invocations);
    uint64_t noc_injects_total = 0;
    uint64_t noc_backpressure_total = 0;

    for (unsigned p = 0; p < NOC_PLANES; ++p) {
        noc_injects_total += sub_monitor_vals(start->noc_injects[p], end->noc_injects[p]);
        for (unsigned q = 0; q < NOC_QUEUES; ++q) {
            noc_backpressure_total +=
                sub_monitor_vals(start->noc_backpressure[p][q], end->noc_backpressure[p][q]);
        }
    }

    printf("[PROFILE][MON] %s tile=(y=%u x=%u): total=%llu mem=%llu compute=%llu tlb=%u invocations=%u noc_injects=%llu noc_backpressure=%llu\n",
           label, esp_get_y(dev), esp_get_x(dev),
           (unsigned long long)tot_cycles,
           (unsigned long long)mem_cycles,
           (unsigned long long)compute_cycles,
           tlb_cycles, invocations,
           (unsigned long long)noc_injects_total,
           (unsigned long long)noc_backpressure_total);
}

#endif
