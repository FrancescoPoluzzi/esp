#ifndef ESPHEEP_TRANFORMER_PROFILING_HELPERS_H
#define ESPHEEP_TRANFORMER_PROFILING_HELPERS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_accelerator.h"
#include "esp_probe.h"
#include "monitors.h"

#ifndef HOST_PROFILE
#define HOST_PROFILE 0
#endif

typedef struct {
    esp_acc_stats_t acc;
    uint32_t noc_injects[NOC_PLANES];
    uint32_t noc_backpressure[NOC_PLANES][NOC_QUEUES];
} mon_profile_t;

typedef struct {
    unsigned cpu_index;
    unsigned tile_index;
    uint32_t ddr_accesses[SOC_NMEM];
    esp_mem_reqs_t mem_reqs[SOC_NMEM];
    esp_cache_stats_t host_l2;
    esp_cache_stats_t llc_stats[SOC_NMEM];
    uint32_t dvfs_op[DVFS_OP_POINTS];
    uint32_t noc_injects[NOC_PLANES];
    uint32_t noc_backpressure[NOC_PLANES][NOC_QUEUES];
} host_mon_profile_t;

static inline uint64_t host_cycles(void)
{
#ifdef __riscv
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
#else
    return 0;
#endif
}

static inline uint64_t host_cycles_diff(uint64_t start, uint64_t end)
{
    if (end >= start) {
        return end - start;
    }

    return (UINT64_MAX - start + 1u) + end;
}

static inline unsigned tile_index_from_dev(struct esp_device *dev)
{
    return esp_get_y(dev) * SOC_COLS + esp_get_x(dev);
}

static inline unsigned read_mon_tile(unsigned tile_index, unsigned mon_index)
{
    esp_monitor_args_t mon_args = {0};

    mon_args.read_mode = ESP_MON_READ_SINGLE;
    mon_args.tile_index = tile_index;
    mon_args.mon_index = mon_index;
    return esp_monitor(mon_args, NULL);
}

static inline unsigned host_cpu_index(void)
{
    int pid = get_pid();

    if (pid < 0 || pid >= SOC_NCPU) {
        return 0u;
    }

    return (unsigned)pid;
}

static inline unsigned tile_index_from_loc(soc_loc_t loc)
{
    return loc.row * SOC_COLS + loc.col;
}

static inline unsigned percent_u64(uint64_t part, uint64_t total)
{
    if (total == 0u) {
        return 0u;
    }

    return (unsigned)((100u * part + (total / 2u)) / total);
}

static void read_mon_profile(struct esp_device *dev, mon_profile_t *profile)
{
    unsigned tile_index = tile_index_from_dev(dev);

    profile->acc.acc_tlb = read_mon_tile(tile_index, MON_ACC_TLB_INDEX);
    profile->acc.acc_mem_lo = read_mon_tile(tile_index, MON_ACC_MEM_LO_INDEX);
    profile->acc.acc_mem_hi = read_mon_tile(tile_index, MON_ACC_MEM_HI_INDEX);
    profile->acc.acc_tot_lo = read_mon_tile(tile_index, MON_ACC_TOT_LO_INDEX);
    profile->acc.acc_tot_hi = read_mon_tile(tile_index, MON_ACC_TOT_HI_INDEX);
    profile->acc.acc_invocations = read_mon_tile(tile_index, MON_ACC_INVOCATIONS);

    for (unsigned plane = 0; plane < NOC_PLANES; ++plane) {
        profile->noc_injects[plane] = read_mon_tile(tile_index, MON_NOC_TILE_INJECT_BASE_INDEX + plane);
    }
    for (unsigned queue = 0; queue < NOC_QUEUES; ++queue) {
        for (unsigned plane = 0; plane < NOC_PLANES; ++plane) {
            profile->noc_backpressure[plane][queue] =
                read_mon_tile(tile_index, MON_NOC_QUEUES_FULL_BASE_INDEX + plane * NOC_QUEUES + queue);
        }
    }
}

static void read_host_mon_profile(host_mon_profile_t *profile)
{
#if HOST_PROFILE
    memset(profile, 0, sizeof(*profile));

    profile->cpu_index = host_cpu_index();
    profile->tile_index = tile_index_from_loc(cpu_locs[profile->cpu_index]);

    const unsigned host_tile = profile->tile_index;
    profile->host_l2.hits = read_mon_tile(host_tile, MON_L2_HIT_INDEX);
    profile->host_l2.misses = read_mon_tile(host_tile, MON_L2_MISS_INDEX);

    for (unsigned point = 0; point < DVFS_OP_POINTS; ++point) {
        profile->dvfs_op[point] = read_mon_tile(host_tile, MON_DVFS_BASE_INDEX + point);
    }
    for (unsigned plane = 0; plane < NOC_PLANES; ++plane) {
        profile->noc_injects[plane] = read_mon_tile(host_tile, MON_NOC_TILE_INJECT_BASE_INDEX + plane);
        for (unsigned queue = 0; queue < NOC_QUEUES; ++queue) {
            profile->noc_backpressure[plane][queue] =
                read_mon_tile(host_tile, MON_NOC_QUEUES_FULL_BASE_INDEX + plane * NOC_QUEUES + queue);
        }
    }

    for (unsigned mem = 0; mem < SOC_NMEM; ++mem) {
        const unsigned mem_tile = tile_index_from_loc(mem_locs[mem]);

        profile->ddr_accesses[mem] = read_mon_tile(mem_tile, MON_DDR_WORD_TRANSFER_INDEX);
        profile->mem_reqs[mem].coh_reqs = read_mon_tile(mem_tile, MON_MEM_COH_REQ_INDEX);
        profile->mem_reqs[mem].coh_fwds = read_mon_tile(mem_tile, MON_MEM_COH_FWD_INDEX);
        profile->mem_reqs[mem].coh_rsps_rcv = read_mon_tile(mem_tile, MON_MEM_COH_RSP_RCV_INDEX);
        profile->mem_reqs[mem].coh_rsps_snd = read_mon_tile(mem_tile, MON_MEM_COH_RSP_SND_INDEX);
        profile->mem_reqs[mem].dma_reqs = read_mon_tile(mem_tile, MON_MEM_DMA_REQ_INDEX);
        profile->mem_reqs[mem].dma_rsps = read_mon_tile(mem_tile, MON_MEM_DMA_RSP_INDEX);
        profile->mem_reqs[mem].coh_dma_reqs = read_mon_tile(mem_tile, MON_MEM_COH_DMA_REQ_INDEX);
        profile->mem_reqs[mem].coh_dma_rsps = read_mon_tile(mem_tile, MON_MEM_COH_DMA_RSP_INDEX);
        profile->llc_stats[mem].hits = read_mon_tile(mem_tile, MON_LLC_HIT_INDEX);
        profile->llc_stats[mem].misses = read_mon_tile(mem_tile, MON_LLC_MISS_INDEX);
    }
#else
    memset(profile, 0, sizeof(*profile));
#endif
}

static void print_mon_profile_diff(const char *label,
                                   struct esp_device *dev,
                                   const mon_profile_t *start,
                                   const mon_profile_t *end)
{
    uint64_t mem_start = ((uint64_t)start->acc.acc_mem_hi << 32) | start->acc.acc_mem_lo;
    uint64_t mem_end = ((uint64_t)end->acc.acc_mem_hi << 32) | end->acc.acc_mem_lo;
    uint64_t tot_start = ((uint64_t)start->acc.acc_tot_hi << 32) | start->acc.acc_tot_lo;
    uint64_t tot_end = ((uint64_t)end->acc.acc_tot_hi << 32) | end->acc.acc_tot_lo;
    uint64_t mem_cycles = host_cycles_diff(mem_start, mem_end);
    uint64_t tot_cycles = host_cycles_diff(tot_start, tot_end);
    uint64_t compute_cycles = (tot_cycles >= mem_cycles) ? (tot_cycles - mem_cycles) : 0;
    uint32_t tlb_cycles = sub_monitor_vals(start->acc.acc_tlb, end->acc.acc_tlb);
    uint32_t invocations = sub_monitor_vals(start->acc.acc_invocations, end->acc.acc_invocations);
    uint64_t noc_injects_total = 0;
    uint64_t noc_backpressure_total = 0;
    uint64_t max_plane_injects = 0;
    uint32_t max_backpressure = 0;
    unsigned busiest_plane = 0u;
    unsigned busiest_bp_plane = 0u;
    unsigned busiest_bp_queue = 0u;

    for (unsigned plane = 0; plane < NOC_PLANES; ++plane) {
        uint64_t plane_injects = sub_monitor_vals(start->noc_injects[plane], end->noc_injects[plane]);

        noc_injects_total += plane_injects;
        if (plane_injects > max_plane_injects) {
            max_plane_injects = plane_injects;
            busiest_plane = plane;
        }
        for (unsigned queue = 0; queue < NOC_QUEUES; ++queue) {
            uint32_t backpressure =
                sub_monitor_vals(start->noc_backpressure[plane][queue], end->noc_backpressure[plane][queue]);

            noc_backpressure_total += backpressure;
            if (backpressure > max_backpressure) {
                max_backpressure = backpressure;
                busiest_bp_plane = plane;
                busiest_bp_queue = queue;
            }
        }
    }

    printf("[PROFILE][MON] %s tile=(y=%u x=%u): total=%llu mem=%llu(%u%%) compute=%llu(%u%%) tlb=%u invocations=%u noc_injects=%llu busiest_plane=%u(%llu) noc_backpressure=%llu busiest_bp=(plane=%u queue=%u cycles=%u)\n",
           label,
           esp_get_y(dev), esp_get_x(dev),
           (unsigned long long)tot_cycles,
           (unsigned long long)mem_cycles,
           percent_u64(mem_cycles, tot_cycles),
           (unsigned long long)compute_cycles,
           percent_u64(compute_cycles, tot_cycles),
           tlb_cycles, invocations,
           (unsigned long long)noc_injects_total,
           busiest_plane,
           (unsigned long long)max_plane_injects,
           (unsigned long long)noc_backpressure_total,
           busiest_bp_plane, busiest_bp_queue, max_backpressure);
}

static void print_host_mon_profile_diff(const char *label,
                                        const host_mon_profile_t *start,
                                        const host_mon_profile_t *end)
{
#if HOST_PROFILE
    uint64_t l2_hits = sub_monitor_vals(start->host_l2.hits, end->host_l2.hits);
    uint64_t l2_misses = sub_monitor_vals(start->host_l2.misses, end->host_l2.misses);
    uint64_t l2_total = l2_hits + l2_misses;
    uint64_t ddr_words = 0;
    uint64_t llc_hits = 0;
    uint64_t llc_misses = 0;
    uint64_t coh_reqs = 0;
    uint64_t coh_fwds = 0;
    uint64_t coh_rsps_rcv = 0;
    uint64_t coh_rsps_snd = 0;
    uint64_t dma_reqs = 0;
    uint64_t dma_rsps = 0;
    uint64_t coh_dma_reqs = 0;
    uint64_t coh_dma_rsps = 0;
    uint64_t mem_ddr_words[SOC_NMEM] = {0};
    uint64_t mem_llc_hits[SOC_NMEM] = {0};
    uint64_t mem_llc_misses[SOC_NMEM] = {0};
    uint64_t mem_dma_req_counts[SOC_NMEM] = {0};
    uint64_t mem_coh_req_counts[SOC_NMEM] = {0};
    uint64_t mem_coh_dma_req_counts[SOC_NMEM] = {0};
    uint64_t noc_injects_total = 0;
    uint64_t max_plane_injects = 0;
    uint64_t noc_backpressure_total = 0;
    uint64_t plane_injects[NOC_PLANES] = {0};
    uint64_t plane_backpressure[NOC_PLANES] = {0};
    uint32_t max_backpressure = 0;
    unsigned busiest_plane = 0u;
    unsigned busiest_bp_plane = 0u;
    unsigned busiest_bp_queue = 0u;
    uint32_t dvfs_diff[DVFS_OP_POINTS] = {0};
    uint64_t max_ddr_words = 0;
    uint64_t max_dma_reqs = 0;
    uint64_t max_llc_traffic = 0;
    unsigned busiest_mem_ddr = 0u;
    unsigned busiest_mem_dma = 0u;
    unsigned busiest_mem_llc = 0u;
    uint32_t active_dvfs_cycles = 0u;
    unsigned active_dvfs_op = 0u;

    for (unsigned mem = 0; mem < SOC_NMEM; ++mem) {
        mem_ddr_words[mem] = sub_monitor_vals(start->ddr_accesses[mem], end->ddr_accesses[mem]);
        mem_coh_req_counts[mem] = sub_monitor_vals(start->mem_reqs[mem].coh_reqs, end->mem_reqs[mem].coh_reqs);
        mem_dma_req_counts[mem] = sub_monitor_vals(start->mem_reqs[mem].dma_reqs, end->mem_reqs[mem].dma_reqs);
        mem_coh_dma_req_counts[mem] =
            sub_monitor_vals(start->mem_reqs[mem].coh_dma_reqs, end->mem_reqs[mem].coh_dma_reqs);
        mem_llc_hits[mem] = sub_monitor_vals(start->llc_stats[mem].hits, end->llc_stats[mem].hits);
        mem_llc_misses[mem] = sub_monitor_vals(start->llc_stats[mem].misses, end->llc_stats[mem].misses);

        ddr_words += mem_ddr_words[mem];
        coh_reqs += mem_coh_req_counts[mem];
        coh_fwds += sub_monitor_vals(start->mem_reqs[mem].coh_fwds, end->mem_reqs[mem].coh_fwds);
        coh_rsps_rcv += sub_monitor_vals(start->mem_reqs[mem].coh_rsps_rcv, end->mem_reqs[mem].coh_rsps_rcv);
        coh_rsps_snd += sub_monitor_vals(start->mem_reqs[mem].coh_rsps_snd, end->mem_reqs[mem].coh_rsps_snd);
        dma_reqs += mem_dma_req_counts[mem];
        dma_rsps += sub_monitor_vals(start->mem_reqs[mem].dma_rsps, end->mem_reqs[mem].dma_rsps);
        coh_dma_reqs += mem_coh_dma_req_counts[mem];
        coh_dma_rsps += sub_monitor_vals(start->mem_reqs[mem].coh_dma_rsps, end->mem_reqs[mem].coh_dma_rsps);
        llc_hits += mem_llc_hits[mem];
        llc_misses += mem_llc_misses[mem];

        if (mem_ddr_words[mem] > max_ddr_words) {
            max_ddr_words = mem_ddr_words[mem];
            busiest_mem_ddr = mem;
        }
        if (mem_dma_req_counts[mem] > max_dma_reqs) {
            max_dma_reqs = mem_dma_req_counts[mem];
            busiest_mem_dma = mem;
        }
        if ((mem_llc_hits[mem] + mem_llc_misses[mem]) > max_llc_traffic) {
            max_llc_traffic = mem_llc_hits[mem] + mem_llc_misses[mem];
            busiest_mem_llc = mem;
        }
    }

    for (unsigned point = 0; point < DVFS_OP_POINTS; ++point) {
        dvfs_diff[point] = sub_monitor_vals(start->dvfs_op[point], end->dvfs_op[point]);
        if (dvfs_diff[point] > active_dvfs_cycles) {
            active_dvfs_cycles = dvfs_diff[point];
            active_dvfs_op = point;
        }
    }
    for (unsigned plane = 0; plane < NOC_PLANES; ++plane) {
        plane_injects[plane] = sub_monitor_vals(start->noc_injects[plane], end->noc_injects[plane]);
        noc_injects_total += plane_injects[plane];
        if (plane_injects[plane] > max_plane_injects) {
            max_plane_injects = plane_injects[plane];
            busiest_plane = plane;
        }
        for (unsigned queue = 0; queue < NOC_QUEUES; ++queue) {
            uint32_t backpressure =
                sub_monitor_vals(start->noc_backpressure[plane][queue], end->noc_backpressure[plane][queue]);

            noc_backpressure_total += backpressure;
            plane_backpressure[plane] += backpressure;
            if (backpressure > max_backpressure) {
                max_backpressure = backpressure;
                busiest_bp_plane = plane;
                busiest_bp_queue = queue;
            }
        }
    }

    const soc_loc_t host_loc = cpu_locs[end->cpu_index];
    const uint64_t llc_total = llc_hits + llc_misses;

    printf("[PROFILE][HOST][MON] %s cpu=%u tile=(y=%u x=%u): l2_hits=%llu l2_misses=%llu l2_hit_rate=%u%% ddr_words=%llu llc_hits=%llu llc_misses=%llu llc_hit_rate=%u%% noc_injects=%llu busiest_plane=%u(%llu) noc_backpressure=%llu busiest_bp=(plane=%u queue=%u cycles=%u)\n",
           label,
           end->cpu_index,
           host_loc.row, host_loc.col,
           (unsigned long long)l2_hits,
           (unsigned long long)l2_misses,
           percent_u64(l2_hits, l2_total),
           (unsigned long long)ddr_words,
           (unsigned long long)llc_hits,
           (unsigned long long)llc_misses,
           percent_u64(llc_hits, llc_total),
           (unsigned long long)noc_injects_total,
           busiest_plane,
           (unsigned long long)max_plane_injects,
           (unsigned long long)noc_backpressure_total,
           busiest_bp_plane, busiest_bp_queue, max_backpressure);
    printf("[PROFILE][HOST][MON] %s mem_reqs: coh=%llu/%llu/%llu/%llu dma=%llu/%llu coh_dma=%llu/%llu dvfs=[%u,%u,%u,%u]\n",
           label,
           (unsigned long long)coh_reqs,
           (unsigned long long)coh_fwds,
           (unsigned long long)coh_rsps_rcv,
           (unsigned long long)coh_rsps_snd,
           (unsigned long long)dma_reqs,
           (unsigned long long)dma_rsps,
           (unsigned long long)coh_dma_reqs,
           (unsigned long long)coh_dma_rsps,
           dvfs_diff[0], dvfs_diff[1], dvfs_diff[2], dvfs_diff[3]);
    printf("[PROFILE][HOST][MON] %s peaks: active_dvfs=%u(%u cycles) mem_ddr=mem%u(%llu words) mem_dma=mem%u(%llu reqs) mem_llc=mem%u(hit_rate=%u%% total=%llu)\n",
           label,
           active_dvfs_op,
           active_dvfs_cycles,
           busiest_mem_ddr,
           (unsigned long long)max_ddr_words,
           busiest_mem_dma,
           (unsigned long long)max_dma_reqs,
           busiest_mem_llc,
           percent_u64(mem_llc_hits[busiest_mem_llc], mem_llc_hits[busiest_mem_llc] + mem_llc_misses[busiest_mem_llc]),
           (unsigned long long)max_llc_traffic);

    for (unsigned mem = 0; mem < SOC_NMEM; ++mem) {
        const soc_loc_t mem_loc = mem_locs[mem];
        const uint64_t mem_llc_total = mem_llc_hits[mem] + mem_llc_misses[mem];

        if (mem_ddr_words[mem] == 0u &&
            mem_coh_req_counts[mem] == 0u &&
            mem_dma_req_counts[mem] == 0u &&
            mem_coh_dma_req_counts[mem] == 0u &&
            mem_llc_total == 0u) {
            continue;
        }
        printf("[PROFILE][HOST][MEM] %s mem%u tile=(y=%u x=%u): ddr_words=%llu llc_hits=%llu llc_misses=%llu llc_hit_rate=%u%% coh_reqs=%llu dma_reqs=%llu coh_dma_reqs=%llu\n",
               label,
               mem,
               mem_loc.row, mem_loc.col,
               (unsigned long long)mem_ddr_words[mem],
               (unsigned long long)mem_llc_hits[mem],
               (unsigned long long)mem_llc_misses[mem],
               percent_u64(mem_llc_hits[mem], mem_llc_total),
               (unsigned long long)mem_coh_req_counts[mem],
               (unsigned long long)mem_dma_req_counts[mem],
               (unsigned long long)mem_coh_dma_req_counts[mem]);
    }

    for (unsigned plane = 0; plane < NOC_PLANES; ++plane) {
        if (plane_injects[plane] == 0u && plane_backpressure[plane] == 0u) {
            continue;
        }
        printf("[PROFILE][HOST][NOC] %s tile=(y=%u x=%u) plane=%u injects=%llu backpressure=%llu\n",
               label,
               host_loc.row, host_loc.col,
               plane,
               (unsigned long long)plane_injects[plane],
               (unsigned long long)plane_backpressure[plane]);
    }
#else
    (void)label;
    (void)start;
    (void)end;
#endif
}

#endif
