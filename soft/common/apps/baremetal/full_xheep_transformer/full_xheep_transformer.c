#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "transformer.h"
#include "data_cpp/signal.h"
#include "weightsAndBiasesC.h"
#include "transformerBlockC.h"

#include "esp_accelerator.h"
#include "esp_probe.h"
#include "monitors.h"
#include "soc_locs.h"

#include "xheep_fpu_rtl_accelerator.h"
#include "xheep_quadrilatero_rtl_accelerator.h"

#include "xheep_fpu_fw_transformer_FFT_0_9.h"
#include "xheep_fpu_fw_transformer_FFT_10_19.h"
#include "xheep_quadrilatero_fw_transformer_layer_0.h"
#include "xheep_quadrilatero_fw_transformer_layer_1.h"
#include "xheep_quadrilatero_fw_transformer_layer_2.h"
#include "xheep_quadrilatero_fw_transformer_layer_3.h"

#define PROFILE CONTINUE_RUN_PROFILE
#define DEBUG_FULL_XHEEP CONTINUE_RUN_DEBUG
#define CHECK_RESULTS CONTINUE_RUN_CHECK_RESULT
#define HOST_PROFILE 0

int __errno;

extern void tohost_exit(uintptr_t code) __attribute__((noreturn));
extern char _end;

static inline uintptr_t read_csr_mtval(void)
{
    uintptr_t v;
    asm volatile("csrr %0, mtval" : "=r"(v));
    return v;
}

static inline uintptr_t read_csr_mstatus(void)
{
    uintptr_t v;
    asm volatile("csrr %0, mstatus" : "=r"(v));
    return v;
}

uintptr_t handle_trap(uintptr_t cause, uintptr_t epc, uintptr_t regs[32])
{
    uintptr_t mtval = read_csr_mtval();
    uintptr_t mstatus = read_csr_mstatus();

    printf("[TRAP] cause=0x%08lx epc=0x%08lx mtval=0x%08lx mstatus=0x%08lx ra=0x%08lx sp=0x%08lx\n",
           (unsigned long)cause,
           (unsigned long)epc,
           (unsigned long)mtval,
           (unsigned long)mstatus,
           (unsigned long)regs[1],
           (unsigned long)regs[2]);
    tohost_exit(0xdead);
}

#define ACC_VENDOR VENDOR_SLD
#define ACC_DEVID  0x067

#define ACC_COMPAT_FPU_PRIMARY   "sld,xheep_fpu_rtl"
#define ACC_COMPAT_FPU_FALLBACK  "sld,xheep_fpu"
#define ACC_COMPAT_QUAD_PRIMARY  "sld,xheep_quadrilatero_rtl"
#define ACC_COMPAT_QUAD_FALLBACK "sld,xheep_quadrilatero_fpu_rtl"

#define CHUNK_SHIFT 20
#define CHUNK_SIZE  (1u << CHUNK_SHIFT)
#define NCHUNK(_sz) (((_sz) + CHUNK_SIZE - 1u) >> CHUNK_SHIFT)

#ifndef ACC_RESERVED_BASE
#define ACC_RESERVED_BASE 0x82000000u
#endif

#ifndef ACC_RESERVED_SIZE
#define ACC_RESERVED_SIZE 0x01000000u
#endif

#define ACC_STACK_TLS_BYTES (1u << 17)

#define XHEEP_SHARED_IO_OFFSET 0x00000000u

#define XHEEP_LAYER_SEQ_LEN         (D_SEQ + 1u)
#define XHEEP_LAYER_ELEMS           (XHEEP_LAYER_SEQ_LEN * D_MODEL)
#define XHEEP_LAYER_BYTES           (XHEEP_LAYER_ELEMS * sizeof(quant_bit_width))
#define XHEEP_LAYER_DONE_OFFSET     (XHEEP_SHARED_IO_OFFSET + XHEEP_LAYER_BYTES)
#define XHEEP_LAYER_DONE_BYTES      4u

#define XHEEP_LAYER_QKV_ELEMS       (4u * XHEEP_LAYER_SEQ_LEN * D_Q)
#define XHEEP_LAYER_QKV_BYTES       (XHEEP_LAYER_QKV_ELEMS * sizeof(quant_bit_width))
#define XHEEP_LAYER_INTERM_ELEMS    (XHEEP_LAYER_SEQ_LEN * XHEEP_LAYER_SEQ_LEN)
#define XHEEP_LAYER_INTERM_BYTES    (XHEEP_LAYER_INTERM_ELEMS * sizeof(quant_bit_width))

#define XHEEP_FFT_INPUT_REAL_SAMPLES 256u
#define XHEEP_FFT_SIZE               512u
#define XHEEP_FFT_BITS               9u
#define XHEEP_STFT_CHANNELS          20u
#define XHEEP_STFT_TIME_STEPS        15u
#define XHEEP_STFT_PATCH_HEIGHT      80u
#define XHEEP_STFT_PATCH_WIDTH       5u
#define XHEEP_STFT_OVERLAP           64u
#define XHEEP_STFT_WINDOW_STRIDE     (XHEEP_FFT_INPUT_REAL_SAMPLES - XHEEP_STFT_OVERLAP)
#define XHEEP_RAW_SIGNAL_CH_SAMPLES  3072u
#define XHEEP_RAW_SIGNAL_TOTAL_SAMPLES (XHEEP_STFT_CHANNELS * XHEEP_RAW_SIGNAL_CH_SAMPLES)

#define XHEEP_PRE_RAW_ELEMS           XHEEP_RAW_SIGNAL_TOTAL_SAMPLES
#define XHEEP_PRE_NORM1_WEIGHT_ELEMS  D_EMBEDDING
#define XHEEP_PRE_NORM1_BIAS_ELEMS    D_EMBEDDING
#define XHEEP_PRE_DENSE_WEIGHT_ELEMS  (D_EMBEDDING * D_MODEL)
#define XHEEP_PRE_DENSE_BIAS_ELEMS    D_MODEL
#define XHEEP_PRE_NORM2_WEIGHT_ELEMS  D_MODEL
#define XHEEP_PRE_NORM2_BIAS_ELEMS    D_MODEL
#define XHEEP_PRE_CLS_ELEMS           D_MODEL
#define XHEEP_PRE_POS_ELEMS           (XHEEP_LAYER_SEQ_LEN * D_MODEL)

#define XHEEP_LAYER_IN_ELEMS (XHEEP_PRE_RAW_ELEMS + XHEEP_PRE_NORM1_WEIGHT_ELEMS + XHEEP_PRE_NORM1_BIAS_ELEMS + \
                              XHEEP_PRE_DENSE_WEIGHT_ELEMS + XHEEP_PRE_DENSE_BIAS_ELEMS + XHEEP_PRE_NORM2_WEIGHT_ELEMS + \
                              XHEEP_PRE_NORM2_BIAS_ELEMS + XHEEP_PRE_CLS_ELEMS + XHEEP_PRE_POS_ELEMS)
#define XHEEP_LAYER_IN_BYTES          (XHEEP_LAYER_IN_ELEMS * sizeof(quant_bit_width))
#define XHEEP_LAYER_OUT_ELEMS         XHEEP_LAYER_ELEMS
#define XHEEP_LAYER_OUT_BYTES         (XHEEP_LAYER_OUT_ELEMS * sizeof(quant_bit_width))
#define XHEEP_LAYER_SHARED_BYTES      ((XHEEP_LAYER_IN_BYTES > XHEEP_LAYER_OUT_BYTES) ? XHEEP_LAYER_IN_BYTES : XHEEP_LAYER_OUT_BYTES)
#define XHEEP_FFT_DONE_OFFSET         (XHEEP_SHARED_IO_OFFSET + XHEEP_LAYER_SHARED_BYTES)

#define NUM_TRANSFORMER_LAYERS_OFFLOADED 4
#define TRANSFORMER_WEIGHT_VEC_LEN (NUM_LAYERS * (3 * NUM_HEAD + 5) + 5)

#define NUM_FPU_FFT_SPLIT_TILES 2u
#define FFT0_TILE_Y 0u
#define FFT0_TILE_X 1u
#define FFT1_TILE_Y 1u
#define FFT1_TILE_X 0u

#define POLL_REPORT_PERIOD 1000000u

/*
 * FFT->layer0 join in explicit indexed-source mode:
 * layer0 reads with read_user=1 then read_user=2; entries 1/2 come from YX_REG.
 */
#define XHEEP_FFT_P2P_SRC_SLOT0 1u
#define XHEEP_FFT_P2P_SRC_SLOT1 2u
#define XHEEP_FFT_P2P_NSRCS     3u

#include "espheep_tranformer_profiling_helpers.h"

#define ESPHEEP_TRANSFORMER_FFT_HELPERS_IMPL
#include "espheep_transformer_fft_helpers.h"
#undef ESPHEEP_TRANSFORMER_FFT_HELPERS_IMPL

#define ESPHEEP_TRANSFORMER_LAYER_HELPERS_IMPL
#include "espheep_transformer_layer_helpers.h"
#undef ESPHEEP_TRANSFORMER_LAYER_HELPERS_IMPL

typedef struct {
    struct esp_device *dev;
    uint8_t *data_buffer;
    size_t data_buffer_size;
    uint32_t *ptable_data;
    unsigned nchunk_data;
    const char *tag;
} fpu_fft_ctx_t;

typedef struct {
    struct esp_device *dev;
    uint8_t *data_buffer;
    size_t data_buffer_size;
    unsigned layer_id;
} quad_layer_ctx_t;

static uintptr_t g_acc_mem_next = ~(uintptr_t)0;
static uintptr_t g_acc_pt_next = ~(uintptr_t)0;
static const char *const k_quad_fw_fetch_labels[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {
    "quad_l0_fw_fetch",
    "quad_l1_fw_fetch",
    "quad_l2_fw_fetch",
    "quad_l3_fw_fetch"
};
static const char *const k_quad_run_labels[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {
    "quad_l0_run",
    "quad_l1_run",
    "quad_l2_run",
    "quad_l3_run"
};
static const char *const k_quad_input_move_labels[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {
    "quad_layer0_input_move",
    "quad_layer1_input_move",
    "quad_layer2_input_move",
    "quad_layer3_input_move"
};
static const char *const k_quad_output_move_labels[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {
    "quad_layer0_output_move",
    "quad_layer1_output_move",
    "quad_layer2_output_move",
    "quad_layer3_output_move"
};
static const char *const k_cpu_layer_labels[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {
    "cpu_layer0",
    "cpu_layer1",
    "cpu_layer2",
    "cpu_layer3"
};
#if !USE_P2P
static const char *const k_layer_compare_labels[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {
    "layer0",
    "layer1",
    "layer2",
    "layer3"
};
#endif

static inline uintptr_t align_up_uintptr(uintptr_t value, uintptr_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
}

static uintptr_t acc_reserved_base_runtime(void)
{
    if (ACC_RESERVED_BASE != 0u) return (uintptr_t)ACC_RESERVED_BASE;

    /*
     * Bare-metal Ariane places TLS at _end and reserves 128 KiB per hart for
     * TLS+stack. Start the accelerator scratch arena above hart0's slot and
     * align it to 1 MiB so the firmware/data chunks preserve CHUNK_SIZE
     * alignment without depending on the high DDR window.
     */
    return align_up_uintptr((uintptr_t)&_end + ACC_STACK_TLS_BYTES, CHUNK_SIZE);
}

static uintptr_t acc_mem_cursor_runtime(void)
{
    uintptr_t mem_base = acc_reserved_base_runtime();
    uintptr_t mem_end = mem_base + ACC_RESERVED_SIZE;

    if (g_acc_mem_next == ~(uintptr_t)0) return mem_base;
    if (g_acc_mem_next < mem_base) return mem_base;
    if (g_acc_mem_next >= mem_end) return mem_base;
    return g_acc_mem_next;
}

static uintptr_t acc_pt_cursor_runtime(void)
{
    uintptr_t mem_base = acc_reserved_base_runtime();
    uintptr_t mem_end = mem_base + ACC_RESERVED_SIZE;

    if (g_acc_pt_next == ~(uintptr_t)0) return mem_end;
    if (g_acc_pt_next <= mem_base) return mem_end;
    if (g_acc_pt_next > mem_end) return mem_end;
    return g_acc_pt_next;
}

static const char *label_from_table(const char *const *labels,
                                    size_t count,
                                    unsigned index,
                                    const char *fallback)
{
    return (index < count) ? labels[index] : fallback;
}

static int reserve_acc_chunks(unsigned nchunk_fw,
                              unsigned nchunk_data,
                              uint8_t **fw_buffer,
                              uint8_t **data_buffer,
                              uint32_t **ptable_fw,
                              uint32_t **ptable_data)
{
    uintptr_t mem_base = acc_reserved_base_runtime();
    uintptr_t next = acc_mem_cursor_runtime();
    uintptr_t pt_next = acc_pt_cursor_runtime();

    next = (next + CHUNK_SIZE - 1u) & ~(uintptr_t)(CHUNK_SIZE - 1u);
    *fw_buffer = (uint8_t *)next;
    next += (uintptr_t)nchunk_fw * CHUNK_SIZE;

    next = (next + CHUNK_SIZE - 1u) & ~(uintptr_t)(CHUNK_SIZE - 1u);
    *data_buffer = (uint8_t *)next;
    next += (uintptr_t)nchunk_data * CHUNK_SIZE;

    pt_next -= (uintptr_t)nchunk_data * sizeof(uint32_t);
    pt_next &= ~(uintptr_t)7u;
    *ptable_data = (uint32_t *)pt_next;

    pt_next -= (uintptr_t)nchunk_fw * sizeof(uint32_t);
    pt_next &= ~(uintptr_t)7u;
    *ptable_fw = (uint32_t *)pt_next;

    if (next > pt_next) return -1;

    g_acc_mem_next = next;
    g_acc_pt_next = pt_next;
    return 0;
}

static const char *fft_debug_stage_name(uint16_t stage)
{
    switch (stage) {
    case 32001: return "boot";
    case 32002: return "dma_in_cfg";
    case 32003: return "dma_in_wait";
    case 32004: return "dma_in_err";
    case 32100: return "compute";
    case 32110: return "token_start";
    case 32112: return "col_start";
    case 32113: return "col_fft";
    case 32114: return "col_done";
    case 32111: return "token_done";
    case 32120: return "compute_done";
    case 32200: return "dma_out_cfg";
    case 32201: return "dma_out_wait";
    case 32202: return "dma_out_err";
    case 32300: return "done";
    default: return "unknown";
    }
}

static const char *layer_debug_stage_name(uint16_t stage)
{
    switch (stage) {
    case 31001: return "boot";
    case 31002: return "dma_in_cfg";
    case 31003: return "dma_in_wait";
    case 31100: return "compute_enter";
    case 31110: return "setup_attention";
    case 31120: return "norm0";
    case 31130: return "self_attention_head_start";
    case 31131: return "self_attention_head_done";
    case 31140: return "transpose";
    case 31150: return "condense";
    case 31160: return "residual0";
    case 31170: return "norm1";
    case 31180: return "ff1";
    case 31190: return "ff2";
    case 31200: return "residual1";
    case 31210: return "memcpy_final";
    case 31220: return "compute_exit";
    case 31300: return "dma_out_cfg";
    case 31310: return "dma_out_wait";
    case 31320: return "done";
    default: return "unknown";
    }
}

static int probe_devices(const char *compat_primary,
                         const char *compat_fallback,
                         struct esp_device **out_devs,
                         int *out_ndev)
{
    int ndev = probe(out_devs, ACC_VENDOR, ACC_DEVID, compat_primary);
    if (ndev <= 0 && compat_fallback != NULL) {
        ndev = probe(out_devs, ACC_VENDOR, ACC_DEVID, compat_fallback);
    }
    *out_ndev = ndev;
    return (ndev > 0) ? 0 : -1;
}

static struct esp_device *find_device_by_coords(struct esp_device *devs,
                                                int ndev,
                                                unsigned y,
                                                unsigned x)
{
    if (devs == NULL || ndev <= 0) return NULL;
    for (int i = 0; i < ndev; ++i) {
        if ((unsigned)esp_get_y(&devs[i]) == y && (unsigned)esp_get_x(&devs[i]) == x) {
            return &devs[i];
        }
    }
    return NULL;
}

static struct esp_device *find_any_other_device(struct esp_device *devs,
                                                int ndev,
                                                struct esp_device *exclude)
{
    if (devs == NULL || ndev <= 0) return NULL;
    for (int i = 0; i < ndev; ++i) {
        if (&devs[i] != exclude) return &devs[i];
    }
    return NULL;
}

static const char *quad_fw_fetch_label(unsigned layer_id)
{
    return label_from_table(k_quad_fw_fetch_labels,
                            NUM_TRANSFORMER_LAYERS_OFFLOADED,
                            layer_id,
                            "quad_l?_fw_fetch");
}

static const char *quad_run_label(unsigned layer_id)
{
    return label_from_table(k_quad_run_labels,
                            NUM_TRANSFORMER_LAYERS_OFFLOADED,
                            layer_id,
                            "quad_l?_run");
}

static const char *quad_input_move_label(unsigned layer_id)
{
    return label_from_table(k_quad_input_move_labels,
                            NUM_TRANSFORMER_LAYERS_OFFLOADED,
                            layer_id,
                            "quad_layer?_input_move");
}

static const char *quad_output_move_label(unsigned layer_id)
{
    return label_from_table(k_quad_output_move_labels,
                            NUM_TRANSFORMER_LAYERS_OFFLOADED,
                            layer_id,
                            "quad_layer?_output_move");
}

static const char *cpu_layer_label(unsigned layer_id)
{
    return label_from_table(k_cpu_layer_labels,
                            NUM_TRANSFORMER_LAYERS_OFFLOADED,
                            layer_id,
                            "cpu_layer?");
}

#if !USE_P2P
static const char *layer_compare_label(unsigned layer_id)
{
    return label_from_table(k_layer_compare_labels,
                            NUM_TRANSFORMER_LAYERS_OFFLOADED,
                            layer_id,
                            "layer?");
}
#endif

#if USE_P2P
static void p2p_set_src_slot(struct esp_device *dev, unsigned slot, unsigned src_y, unsigned src_x)
{
    /* Program P2P_REG source table entry deterministically (clear+set). */
    uint32_t p2p = ioread32(dev, P2P_REG);
    uint32_t xmask = ((uint32_t)P2P_MASK_SRCS_YX) << P2P_SHIFT_SRCS_X(slot);
    uint32_t ymask = ((uint32_t)P2P_MASK_SRCS_YX) << P2P_SHIFT_SRCS_Y(slot);
    p2p &= ~(xmask | ymask);
    p2p |= ((uint32_t)(src_x & P2P_MASK_SRCS_YX)) << P2P_SHIFT_SRCS_X(slot);
    p2p |= ((uint32_t)(src_y & P2P_MASK_SRCS_YX)) << P2P_SHIFT_SRCS_Y(slot);
    iowrite32(dev, P2P_REG, p2p);

    /*
     * Keep YX_REG table entry in sync with same slot. Four slots per register.
     * We clear the slot fields before writing to avoid stale OR-accumulated values.
     */
    unsigned reg_off = 4u * (slot / 4u);
    unsigned idx = slot % 4u;
    unsigned xshift = idx * 2u * YX_WIDTH;
    unsigned yshift = xshift + YX_WIDTH;
    uint32_t yx = ioread32(dev, YX_REG + reg_off);
    uint32_t slot_mask = (((uint32_t)YX_MASK_YX) << xshift) | (((uint32_t)YX_MASK_YX) << yshift);
    yx &= ~slot_mask;
    yx |= ((uint32_t)(src_x & YX_MASK_YX)) << xshift;
    yx |= ((uint32_t)(src_y & YX_MASK_YX)) << yshift;
    iowrite32(dev, YX_REG + reg_off, yx);
}

static void configure_quad_chain_p2p(const quad_layer_ctx_t *ctx, unsigned nlayer)
{
    if (ctx == NULL || nlayer < 2u) return;

    for (unsigned l = 0; l < nlayer; ++l) {
        esp_p2p_reset(ctx[l].dev);
    }

    for (unsigned l = 0; l + 1u < nlayer; ++l) {
        struct esp_device *prod = ctx[l].dev;
        struct esp_device *cons = ctx[l + 1u].dev;
        unsigned py = (unsigned)esp_get_y(prod);
        unsigned px = (unsigned)esp_get_x(prod);

        /* Producer side: one destination in P2P mode. */
        esp_p2p_enable_dst(prod);
        esp_p2p_set_mcast_ndests(prod, XHEEP_LAYER_P2P_NDESTS);

        /*
         * Consumer side: pull source is selected by source slot.
         * We configure both P2P_REG and the YX table entry used by DMA user bits.
         */
        esp_p2p_enable_src(cons);
        esp_p2p_set_nsrcs(cons, XHEEP_LAYER_P2P_NSRCS);
        p2p_set_src_slot(cons, XHEEP_LAYER_P2P_SRC_SLOT, py, px);

        printf("[P2P] layer%u -> layer%u via slot=%u (src y=%u x=%u)\n",
               l, l + 1u, (unsigned)XHEEP_LAYER_P2P_SRC_SLOT, py, px);
    }
}

static void configure_full_pipeline_p2p(const fpu_fft_ctx_t *fft_ctx,
                                        const quad_layer_ctx_t *quad_ctx,
                                        unsigned nlayer)
{
    if (fft_ctx == NULL || quad_ctx == NULL || nlayer < 2u) return;

    /* Reset all participants first. */
    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
        esp_p2p_reset(fft_ctx[i].dev);
    }
    for (unsigned l = 0; l < nlayer; ++l) {
        esp_p2p_reset(quad_ctx[l].dev);
    }

    /* Keep the existing quadrilatero chain: layer0->layer1->layer2->layer3. */
    for (unsigned l = 0; l + 1u < nlayer; ++l) {
        struct esp_device *prod = quad_ctx[l].dev;
        struct esp_device *cons = quad_ctx[l + 1u].dev;
        unsigned py = (unsigned)esp_get_y(prod);
        unsigned px = (unsigned)esp_get_x(prod);

        esp_p2p_enable_dst(prod);
        esp_p2p_set_mcast_ndests(prod, XHEEP_LAYER_P2P_NDESTS);

        esp_p2p_enable_src(cons);
        esp_p2p_set_nsrcs(cons, XHEEP_LAYER_P2P_NSRCS);
        p2p_set_src_slot(cons, XHEEP_LAYER_P2P_SRC_SLOT, py, px);
    }

    /* Add split FFT producers feeding layer0 via slots 1 and 2. */
    struct esp_device *layer0 = quad_ctx[0].dev;
    struct esp_device *fft0 = fft_ctx[0].dev;
    struct esp_device *fft1 = fft_ctx[1].dev;
    unsigned y0 = (unsigned)esp_get_y(fft0);
    unsigned x0 = (unsigned)esp_get_x(fft0);
    unsigned y1 = (unsigned)esp_get_y(fft1);
    unsigned x1 = (unsigned)esp_get_x(fft1);
    struct esp_device fft_srcs[NUM_FPU_FFT_SPLIT_TILES];

    /*
     * Keep FFT producers in default memory mode for user=0 traffic.
     * Their actual split output still goes P2P via explicit DMA write_user=1.
     */

    esp_p2p_enable_src(layer0);
    esp_p2p_set_nsrcs(layer0, XHEEP_FFT_P2P_NSRCS);
    fft_srcs[0] = *fft0;
    fft_srcs[1] = *fft1;
    esp_set_acc_yx_table(layer0, fft_srcs, NUM_FPU_FFT_SPLIT_TILES);

    p2p_set_src_slot(layer0, XHEEP_FFT_P2P_SRC_SLOT0, y0, x0);
    p2p_set_src_slot(layer0, XHEEP_FFT_P2P_SRC_SLOT1, y1, x1);

    printf("[P2P] fft_0_9 -> layer0 via slot=%u (src y=%u x=%u)\n",
           (unsigned)XHEEP_FFT_P2P_SRC_SLOT0, y0, x0);
    printf("[P2P] fft_10_19 -> layer0 via slot=%u (src y=%u x=%u)\n",
           (unsigned)XHEEP_FFT_P2P_SRC_SLOT1, y1, x1);
    printf("[P2P] layer0 -> layer1 -> layer2 -> layer3 via slot=%u\n",
           (unsigned)XHEEP_LAYER_P2P_SRC_SLOT);
}
#endif

static int fill_quad_fw_sections(unsigned layer_id,
                                 xheep_quadrilatero_fw_section_t *fw_secs,
                                 unsigned *nsecs)
{
    switch (layer_id) {
    case 0:
        *nsecs = XHEEP_QUADRILATERO_FW_TRANSFORMER_LAYER_0_NUM_SECTIONS;
        for (unsigned s = 0; s < *nsecs; ++s) {
            fw_secs[s].addr = xheep_quadrilatero_fw_transformer_layer_0_sections[s].addr;
            fw_secs[s].size = xheep_quadrilatero_fw_transformer_layer_0_sections[s].size;
            fw_secs[s].data = (const uint8_t *)xheep_quadrilatero_fw_transformer_layer_0_sections[s].data;
        }
        return 0;
    case 1:
        *nsecs = XHEEP_QUADRILATERO_FW_TRANSFORMER_LAYER_1_NUM_SECTIONS;
        for (unsigned s = 0; s < *nsecs; ++s) {
            fw_secs[s].addr = xheep_quadrilatero_fw_transformer_layer_1_sections[s].addr;
            fw_secs[s].size = xheep_quadrilatero_fw_transformer_layer_1_sections[s].size;
            fw_secs[s].data = (const uint8_t *)xheep_quadrilatero_fw_transformer_layer_1_sections[s].data;
        }
        return 0;
    case 2:
        *nsecs = XHEEP_QUADRILATERO_FW_TRANSFORMER_LAYER_2_NUM_SECTIONS;
        for (unsigned s = 0; s < *nsecs; ++s) {
            fw_secs[s].addr = xheep_quadrilatero_fw_transformer_layer_2_sections[s].addr;
            fw_secs[s].size = xheep_quadrilatero_fw_transformer_layer_2_sections[s].size;
            fw_secs[s].data = (const uint8_t *)xheep_quadrilatero_fw_transformer_layer_2_sections[s].data;
        }
        return 0;
    case 3:
        *nsecs = XHEEP_QUADRILATERO_FW_TRANSFORMER_LAYER_3_NUM_SECTIONS;
        for (unsigned s = 0; s < *nsecs; ++s) {
            fw_secs[s].addr = xheep_quadrilatero_fw_transformer_layer_3_sections[s].addr;
            fw_secs[s].size = xheep_quadrilatero_fw_transformer_layer_3_sections[s].size;
            fw_secs[s].data = (const uint8_t *)xheep_quadrilatero_fw_transformer_layer_3_sections[s].data;
        }
        return 0;
    default:
        return -1;
    }
}

typedef enum {
    FPU_FFT_FW_0_9 = 0,
    FPU_FFT_FW_10_19 = 1
} fpu_fft_fw_kind_t;

static const char *fpu_fft_fw_label(fpu_fft_fw_kind_t fw_kind)
{
    switch (fw_kind) {
    case FPU_FFT_FW_0_9: return "fft_0_9";
    case FPU_FFT_FW_10_19: return "fft_10_19";
    default: return "fft_unknown";
    }
}

static const char *fpu_fft_run_label(const char *tag)
{
    if (tag == NULL) return "fpu_fft_run";
    if (strcmp(tag, "fft_0_9") == 0) return "fpu_fft_0_9_run";
    if (strcmp(tag, "fft_10_19") == 0) return "fpu_fft_10_19_run";
    return "fpu_fft_run";
}

static int fill_fpu_fft_fw_sections(fpu_fft_fw_kind_t fw_kind,
                                    xheep_fpu_fw_section_t *fw_secs,
                                    unsigned *nsecs)
{
    switch (fw_kind) {
    case FPU_FFT_FW_0_9:
        *nsecs = XHEEP_FPU_FW_TRANSFORMER_FFT_0_9_NUM_SECTIONS;
        for (unsigned s = 0; s < *nsecs; ++s) {
            fw_secs[s].addr = xheep_fpu_fw_transformer_FFT_0_9_sections[s].addr;
            fw_secs[s].size = xheep_fpu_fw_transformer_FFT_0_9_sections[s].size;
            fw_secs[s].data = (const uint8_t *)xheep_fpu_fw_transformer_FFT_0_9_sections[s].data;
        }
        return 0;
    case FPU_FFT_FW_10_19:
        *nsecs = XHEEP_FPU_FW_TRANSFORMER_FFT_10_19_NUM_SECTIONS;
        for (unsigned s = 0; s < *nsecs; ++s) {
            fw_secs[s].addr = xheep_fpu_fw_transformer_FFT_10_19_sections[s].addr;
            fw_secs[s].size = xheep_fpu_fw_transformer_FFT_10_19_sections[s].size;
            fw_secs[s].data = (const uint8_t *)xheep_fpu_fw_transformer_FFT_10_19_sections[s].data;
        }
        return 0;
    default:
        return -1;
    }
}

static void configure_data_pt(struct esp_device *dev,
                              uint32_t *ptable_data,
                              unsigned nchunk_data)
{
    iowrite32(dev, PT_ADDRESS_REG, (uint32_t)(uintptr_t)ptable_data);
    iowrite32(dev, PT_NCHUNK_REG, nchunk_data);
    iowrite32(dev, PT_SHIFT_REG, CHUNK_SHIFT);
    iowrite32(dev, SRC_OFFSET_REG, 0);
    iowrite32(dev, DST_OFFSET_REG, 0);
    esp_flush(ACC_COH_NONE);
}

static int init_fpu_fft_ctx(fpu_fft_ctx_t *ctx, struct esp_device *dev, fpu_fft_fw_kind_t fw_kind)
{
    xheep_fpu_fw_section_t fw_secs[16];
    unsigned nsecs = 0;
    const char *fw_tag = fpu_fft_fw_label(fw_kind);
    if (fill_fpu_fft_fw_sections(fw_kind, fw_secs, &nsecs) != 0) {
        printf("Error: invalid FPU FFT firmware kind=%u\n", (unsigned)fw_kind);
        return -1;
    }

    size_t fw_buffer_size = xheep_fpu_fw_size(fw_secs, nsecs);
    size_t data_buffer_size = (XHEEP_FFT_DONE_OFFSET + XHEEP_LAYER_DONE_BYTES + 7u) & ~7u;

    unsigned nchunk_fw = NCHUNK(fw_buffer_size);
    unsigned nchunk_data = NCHUNK(data_buffer_size);
    unsigned max_chunks = ioread32(dev, PT_NCHUNK_MAX_REG);
    if (nchunk_fw > max_chunks || nchunk_data > max_chunks) {
        printf("Error: FPU(%s) not enough TLB entries (fw=%u data=%u max=%u)\n",
               fw_tag, nchunk_fw, nchunk_data, max_chunks);
        return -1;
    }

    uint8_t *fw_buffer = NULL;
    uint8_t *data_buffer = NULL;
    uint32_t *ptable_fw = NULL;
    uint32_t *ptable_data = NULL;
    if (reserve_acc_chunks(nchunk_fw, nchunk_data, &fw_buffer, &data_buffer, &ptable_fw, &ptable_data) != 0) {
        printf("Error: out of accelerator reserved memory while initializing FPU FFT context (%s)\n", fw_tag);
        return -1;
    }

    memset(data_buffer, 0, data_buffer_size);
    for (unsigned s = 0; s < nsecs; ++s) {
        uint32_t sec_end = fw_secs[s].addr + fw_secs[s].size;
        if (sec_end > fw_buffer_size) {
            printf("Error: FPU(%s) firmware section %u exceeds flattened image (end=0x%08x size=%u)\n",
                   fw_tag, s, sec_end, (unsigned)fw_buffer_size);
            return -1;
        }
        copy_bytes(fw_buffer + fw_secs[s].addr, fw_secs[s].data, fw_secs[s].size);
    }

    for (unsigned i = 0; i < nchunk_fw; ++i) ptable_fw[i] = (uint32_t)(uintptr_t)(fw_buffer + i * CHUNK_SIZE);
    for (unsigned i = 0; i < nchunk_data; ++i) ptable_data[i] = (uint32_t)(uintptr_t)(data_buffer + i * CHUNK_SIZE);
    iowrite32(dev, COHERENCE_REG, ACC_COH_NONE);
    iowrite32(dev, PT_ADDRESS_EXTENDED_REG, 0);
    iowrite32(dev, PT_ADDRESS_REG, (uint32_t)(uintptr_t)ptable_fw);
    iowrite32(dev, PT_NCHUNK_REG, nchunk_fw);
    iowrite32(dev, PT_SHIFT_REG, CHUNK_SHIFT);
    iowrite32(dev, SRC_OFFSET_REG, 0);
    iowrite32(dev, DST_OFFSET_REG, 0);
    esp_flush(ACC_COH_NONE);

#if PROFILE
    mon_profile_t fw_mon_start, fw_mon_end;
    host_mon_profile_t fw_host_mon_start, fw_host_mon_end;
    uint64_t fw_host_start = host_cycles();
    read_mon_profile(dev, &fw_mon_start);
    read_host_mon_profile(&fw_host_mon_start);
#endif
    if (!xheep_fpu_fetch_firmware(dev, fw_buffer_size / 4u, 0, false, 50000000)) {
        printf("Error: failed to fetch %s firmware on FPU tile\n", fw_tag);
        return -1;
    }
#if PROFILE
    uint64_t fw_host_end = host_cycles();
    read_mon_profile(dev, &fw_mon_end);
    read_host_mon_profile(&fw_host_mon_end);
    printf("[PROFILE] fpu_%s_program_firmware host_cycles=%llu\n",
           fw_tag,
           (unsigned long long)host_cycles_diff(fw_host_start, fw_host_end));
    print_mon_profile_diff("fpu_fft_firmware_fetch", dev, &fw_mon_start, &fw_mon_end);
    print_host_mon_profile_diff("fpu_fft_firmware_fetch", &fw_host_mon_start, &fw_host_mon_end);
#endif

    configure_data_pt(dev, ptable_data, nchunk_data);

    ctx->dev = dev;
    ctx->data_buffer = data_buffer;
    ctx->data_buffer_size = data_buffer_size;
    ctx->ptable_data = ptable_data;
    ctx->nchunk_data = nchunk_data;
    ctx->tag = fw_tag;

    printf("[XHEEP][FFT:%s] tile=(y=%u x=%u) data_buffer=%p size=%u\n",
           fw_tag, esp_get_y(dev), esp_get_x(dev), (void *)data_buffer, (unsigned)data_buffer_size);
    return 0;
}

static int init_quad_layer_ctx(quad_layer_ctx_t *ctx, struct esp_device *dev, unsigned layer_id)
{
    xheep_quadrilatero_fw_section_t fw_secs[16];
    unsigned nsecs = 0;
    if (fill_quad_fw_sections(layer_id, fw_secs, &nsecs) != 0) {
        printf("Error: invalid layer_id=%u for quadrilatero firmware\n", layer_id);
        return -1;
    }

    size_t fw_buffer_size = xheep_quadrilatero_fw_size(fw_secs, nsecs);
    size_t data_buffer_size = (XHEEP_LAYER_DONE_OFFSET + XHEEP_LAYER_DONE_BYTES + 7u) & ~7u;

    unsigned nchunk_fw = NCHUNK(fw_buffer_size);
    unsigned nchunk_data = NCHUNK(data_buffer_size);
    unsigned max_chunks = ioread32(dev, PT_NCHUNK_MAX_REG);
    if (nchunk_fw > max_chunks || nchunk_data > max_chunks) {
        printf("Error: quad layer%u not enough TLB entries (fw=%u data=%u max=%u)\n",
               layer_id, nchunk_fw, nchunk_data, max_chunks);
        return -1;
    }

    uint8_t *fw_buffer = NULL;
    uint8_t *data_buffer = NULL;
    uint32_t *ptable_fw = NULL;
    uint32_t *ptable_data = NULL;
    if (reserve_acc_chunks(nchunk_fw, nchunk_data, &fw_buffer, &data_buffer, &ptable_fw, &ptable_data) != 0) {
        printf("Error: out of accelerator reserved memory while initializing layer%u context\n", layer_id);
        return -1;
    }

    memset(data_buffer, 0, data_buffer_size);
    xheep_quadrilatero_fw_flatten(fw_buffer, fw_buffer_size, fw_secs, nsecs);

    for (unsigned i = 0; i < nchunk_fw; ++i) ptable_fw[i] = (uint32_t)(uintptr_t)(fw_buffer + i * CHUNK_SIZE);
    for (unsigned i = 0; i < nchunk_data; ++i) ptable_data[i] = (uint32_t)(uintptr_t)(data_buffer + i * CHUNK_SIZE);

    iowrite32(dev, COHERENCE_REG, ACC_COH_NONE);
    iowrite32(dev, PT_ADDRESS_EXTENDED_REG, 0);
    iowrite32(dev, PT_ADDRESS_REG, (uint32_t)(uintptr_t)ptable_fw);
    iowrite32(dev, PT_NCHUNK_REG, nchunk_fw);
    iowrite32(dev, PT_SHIFT_REG, CHUNK_SHIFT);
    iowrite32(dev, SRC_OFFSET_REG, 0);
    iowrite32(dev, DST_OFFSET_REG, 0);
    esp_flush(ACC_COH_NONE);

#if PROFILE
    mon_profile_t fw_mon_start, fw_mon_end;
    host_mon_profile_t fw_host_mon_start, fw_host_mon_end;
    uint64_t fw_host_start = host_cycles();
    read_mon_profile(dev, &fw_mon_start);
    read_host_mon_profile(&fw_host_mon_start);
#endif
    if (!xheep_quadrilatero_fetch_firmware(dev, fw_buffer_size / 4u, 0, false, 50000000)) {
        printf("Error: failed to fetch layer%u firmware on quadrilatero tile\n", layer_id);
        return -1;
    }
#if PROFILE
    uint64_t fw_host_end = host_cycles();
    read_mon_profile(dev, &fw_mon_end);
    read_host_mon_profile(&fw_host_mon_end);
    printf("[PROFILE] quad_layer%u_program_firmware host_cycles=%llu\n",
           layer_id,
           (unsigned long long)host_cycles_diff(fw_host_start, fw_host_end));
    print_mon_profile_diff(quad_fw_fetch_label(layer_id), dev, &fw_mon_start, &fw_mon_end);
    print_host_mon_profile_diff(quad_fw_fetch_label(layer_id), &fw_host_mon_start, &fw_host_mon_end);
#endif

    configure_data_pt(dev, ptable_data, nchunk_data);

    ctx->dev = dev;
    ctx->data_buffer = data_buffer;
    ctx->data_buffer_size = data_buffer_size;
    ctx->layer_id = layer_id;

    printf("[XHEEP][LAYER%u] tile=(y=%u x=%u) data_buffer=%p size=%u\n",
           layer_id, esp_get_y(dev), esp_get_x(dev), (void *)data_buffer, (unsigned)data_buffer_size);
    return 0;
}

static int run_fpu_fft_split_parallel(const fpu_fft_ctx_t *ctx0,
                                      const fpu_fft_ctx_t *ctx1,
                                      const quant_bit_width *raw_input,
                                      quant_bit_width *pre_out)
{
    if (ctx0 == NULL || ctx1 == NULL || raw_input == NULL || pre_out == NULL) return -1;
    if (ctx0->dev == NULL || ctx1->dev == NULL) return -1;
    if (ctx0->data_buffer == NULL || ctx1->data_buffer == NULL) return -1;
    if (ctx0->data_buffer != ctx1->data_buffer) {
        printf("Error: split FFT contexts are not sharing the same data buffer\n");
        return -1;
    }

    const fpu_fft_ctx_t *ctx[NUM_FPU_FFT_SPLIT_TILES] = {ctx0, ctx1};
    quant_bit_width *shared_io = (quant_bit_width *)(ctx0->data_buffer + XHEEP_SHARED_IO_OFFSET);
    volatile uint32_t *dbg_word0 = (volatile uint32_t *)(void *)(ctx0->data_buffer + XHEEP_FFT_DONE_OFFSET);
    volatile uint32_t *dbg_word1 = (volatile uint32_t *)(void *)(ctx1->data_buffer + XHEEP_FFT_DONE_OFFSET);
    *dbg_word0 = 0u;
    if (dbg_word1 != dbg_word0) *dbg_word1 = 0u;

#if PROFILE
    host_mon_profile_t input_move_host_mon_start, input_move_host_mon_end;
    uint64_t input_move_start = host_cycles();
    read_host_mon_profile(&input_move_host_mon_start);
#endif
    pack_fft_input(raw_input, shared_io);
    esp_flush(ACC_COH_NONE);
#if PROFILE
    uint64_t input_move_end = host_cycles();
    read_host_mon_profile(&input_move_host_mon_end);
    printf("[PROFILE] fpu_fft_split_input_move host_cycles=%llu\n",
           (unsigned long long)host_cycles_diff(input_move_start, input_move_end));
    print_host_mon_profile_diff("fpu_fft_split_input_move", &input_move_host_mon_start, &input_move_host_mon_end);
#endif

    bool done[NUM_FPU_FFT_SPLIT_TILES] = {false};
    unsigned done_poll[NUM_FPU_FFT_SPLIT_TILES] = {0u};
    unsigned last_status[NUM_FPU_FFT_SPLIT_TILES] = {0u};

#if PROFILE
    mon_profile_t run_mon_start[NUM_FPU_FFT_SPLIT_TILES];
    mon_profile_t run_mon_end[NUM_FPU_FFT_SPLIT_TILES];
    host_mon_profile_t run_host_mon_start[NUM_FPU_FFT_SPLIT_TILES];
    host_mon_profile_t run_host_mon_end[NUM_FPU_FFT_SPLIT_TILES];
    host_mon_profile_t run_host_total_mon_start, run_host_total_mon_end;
    uint64_t run_host_start[NUM_FPU_FFT_SPLIT_TILES] = {0u};
    uint64_t run_host_end[NUM_FPU_FFT_SPLIT_TILES] = {0u};
    uint64_t run_host_total_start = host_cycles();
    read_host_mon_profile(&run_host_total_mon_start);
#endif

    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
#if PROFILE
        run_host_start[i] = host_cycles();
        read_mon_profile(ctx[i]->dev, &run_mon_start[i]);
        read_host_mon_profile(&run_host_mon_start[i]);
#endif
        xheep_fpu_program_start(ctx[i]->dev, false);
    }

    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
        xheep_fpu_start(ctx[i]->dev);
    }

    unsigned done_count = 0u;
    unsigned poll_count = 0u;
    bool timed_out = false;
    const unsigned max_polls = 20000000u;

    while (done_count < NUM_FPU_FFT_SPLIT_TILES) {
        poll_count++;
        if (poll_count >= max_polls) {
            timed_out = true;
            break;
        }

        for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
            if (done[i]) continue;
            unsigned status = ioread32(ctx[i]->dev, STATUS_REG);
            last_status[i] = status;
            if (status & STATUS_MASK_DONE) {
                done[i] = true;
                done_poll[i] = poll_count;
                done_count++;
                iowrite32(ctx[i]->dev, CMD_REG, 0x0);
#if PROFILE
                run_host_end[i] = host_cycles();
                read_mon_profile(ctx[i]->dev, &run_mon_end[i]);
                read_host_mon_profile(&run_host_mon_end[i]);
#endif
            }
        }

        if ((poll_count % POLL_REPORT_PERIOD) == 0u) {
            printf("[WAIT][FPU] polls=%u done=%u/%u\n",
                   poll_count, done_count, (unsigned)NUM_FPU_FFT_SPLIT_TILES);
        }
    }

#if PROFILE
    uint64_t run_host_total_end = host_cycles();
    read_host_mon_profile(&run_host_total_mon_end);
    printf("[PROFILE] fpu_fft_split_run host_cycles=%llu polls=%u\n",
           (unsigned long long)host_cycles_diff(run_host_total_start, run_host_total_end),
           poll_count);
    print_host_mon_profile_diff("fpu_fft_split_run_total", &run_host_total_mon_start, &run_host_total_mon_end);
#endif

    if (timed_out) {
        for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
            iowrite32(ctx[i]->dev, CMD_REG, 0x0);
            if (done[i]) continue;
            uint32_t dbg = *((volatile uint32_t *)(void *)(ctx[i]->data_buffer + XHEEP_FFT_DONE_OFFSET));
            uint16_t stage = (uint16_t)(dbg & 0xffffu);
            uint16_t arg = (uint16_t)(dbg >> 16);
            printf("Error: FPU %s timeout tile=(y=%u x=%u) status=0x%08x polls=%u dbg=0x%08x stage=%u(%s) arg=%u\n",
                   ctx[i]->tag, esp_get_y(ctx[i]->dev), esp_get_x(ctx[i]->dev), last_status[i], poll_count, dbg,
                   (unsigned)stage, fft_debug_stage_name(stage), (unsigned)arg);
        }
        return -1;
    }

#if PROFILE
    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
        printf("[PROFILE] fpu_%s_run host_cycles=%llu polls=%u\n",
               ctx[i]->tag,
               (unsigned long long)host_cycles_diff(run_host_start[i], run_host_end[i]),
               done_poll[i]);
        print_mon_profile_diff(fpu_fft_run_label(ctx[i]->tag), ctx[i]->dev, &run_mon_start[i], &run_mon_end[i]);
        print_host_mon_profile_diff(fpu_fft_run_label(ctx[i]->tag), &run_host_mon_start[i], &run_host_mon_end[i]);
    }
#endif

#if PROFILE
    host_mon_profile_t output_move_host_mon_start, output_move_host_mon_end;
    uint64_t output_move_start = host_cycles();
    read_host_mon_profile(&output_move_host_mon_start);
#endif
    memcpy(pre_out, shared_io, XHEEP_LAYER_OUT_BYTES);
#if PROFILE
    uint64_t output_move_end = host_cycles();
    read_host_mon_profile(&output_move_host_mon_end);
    printf("[PROFILE] fpu_fft_split_output_move host_cycles=%llu\n",
           (unsigned long long)host_cycles_diff(output_move_start, output_move_end));
    print_host_mon_profile_diff("fpu_fft_split_output_move", &output_move_host_mon_start, &output_move_host_mon_end);
#endif

    return 0;
}

static int run_quad_layer(const quad_layer_ctx_t *ctx,
                          const quant_bit_width *layer_input,
                          quant_bit_width *layer_output)
{
    if (ctx == NULL || ctx->dev == NULL || ctx->data_buffer == NULL) return -1;

    quant_bit_width *shared_io = (quant_bit_width *)(ctx->data_buffer + XHEEP_SHARED_IO_OFFSET);
#if PROFILE
    host_mon_profile_t input_move_host_mon_start, input_move_host_mon_end;
    uint64_t input_move_start = host_cycles();
    read_host_mon_profile(&input_move_host_mon_start);
#endif
    memcpy(shared_io, layer_input, XHEEP_LAYER_BYTES);
    esp_flush(ACC_COH_NONE);
#if PROFILE
    uint64_t input_move_end = host_cycles();
    read_host_mon_profile(&input_move_host_mon_end);
    printf("[PROFILE] quad_layer%u_input_move host_cycles=%llu\n",
           ctx->layer_id,
           (unsigned long long)host_cycles_diff(input_move_start, input_move_end));
    print_host_mon_profile_diff(quad_input_move_label(ctx->layer_id),
                                &input_move_host_mon_start, &input_move_host_mon_end);
#endif

#if PROFILE
    mon_profile_t run_mon_start, run_mon_end;
    host_mon_profile_t run_host_mon_start, run_host_mon_end;
    uint64_t run_host_start = host_cycles();
    read_mon_profile(ctx->dev, &run_mon_start);
    read_host_mon_profile(&run_host_mon_start);
#endif
    xheep_quadrilatero_program_start(ctx->dev, false);
    xheep_quadrilatero_start(ctx->dev);

    unsigned polls = 0;
    bool timed_out = false;
    unsigned status = ioread32(ctx->dev, STATUS_REG);
    while (!(status & STATUS_MASK_DONE)) {
        polls++;
        if (polls >=1000000u) {
            timed_out = true;
            break;
        }
        status = ioread32(ctx->dev, STATUS_REG);
        if ((polls % POLL_REPORT_PERIOD) == 0u) {
            printf("[WAIT][LAYER%u] polls=%u status=0x%08x\n",
                   ctx->layer_id, polls, status);
        }
    }
    iowrite32(ctx->dev, CMD_REG, 0x0);

#if PROFILE
    uint64_t run_host_end = host_cycles();
    read_mon_profile(ctx->dev, &run_mon_end);
    read_host_mon_profile(&run_host_mon_end);
    printf("[PROFILE] quad_layer%u_run host_cycles=%llu polls=%u\n",
           ctx->layer_id,
           (unsigned long long)host_cycles_diff(run_host_start, run_host_end), polls);
    print_mon_profile_diff(quad_run_label(ctx->layer_id), ctx->dev, &run_mon_start, &run_mon_end);
    print_host_mon_profile_diff(quad_run_label(ctx->layer_id), &run_host_mon_start, &run_host_mon_end);
#endif

    if (timed_out) {
        uint32_t dbg = *((volatile uint32_t *)(void *)(ctx->data_buffer + XHEEP_LAYER_DONE_OFFSET));
        uint16_t stage = (uint16_t)(dbg & 0xffffu);
        uint16_t arg = (uint16_t)(dbg >> 16);
        printf("Error: quad layer%u timeout status=0x%08x polls=%u dbg=0x%08x stage=%u(%s) arg=%u\n",
               ctx->layer_id,
               status, polls, dbg, (unsigned)stage, layer_debug_stage_name(stage), (unsigned)arg);
        return -1;
    }

#if PROFILE
    host_mon_profile_t output_move_host_mon_start, output_move_host_mon_end;
    uint64_t output_move_start = host_cycles();
    read_host_mon_profile(&output_move_host_mon_start);
#endif
    memcpy(layer_output, shared_io, XHEEP_LAYER_BYTES);
#if PROFILE
    uint64_t output_move_end = host_cycles();
    read_host_mon_profile(&output_move_host_mon_end);
    printf("[PROFILE] quad_layer%u_output_move host_cycles=%llu\n",
           ctx->layer_id,
           (unsigned long long)host_cycles_diff(output_move_start, output_move_end));
    print_host_mon_profile_diff(quad_output_move_label(ctx->layer_id),
                                &output_move_host_mon_start, &output_move_host_mon_end);
#endif

    return 0;
}

#if USE_P2P
static int run_full_pipeline_p2p(const fpu_fft_ctx_t *fft_ctx,
                                 const quad_layer_ctx_t *quad_ctx,
                                 const quant_bit_width *raw_input,
                                 quant_bit_width *layer3_output)
{
    if (fft_ctx == NULL || quad_ctx == NULL || raw_input == NULL || layer3_output == NULL) return -1;
    if (fft_ctx[0].data_buffer == NULL || fft_ctx[1].data_buffer == NULL) return -1;
    if (fft_ctx[0].data_buffer != fft_ctx[1].data_buffer) return -1;

    quant_bit_width *fft_shared_io =
        (quant_bit_width *)(fft_ctx[0].data_buffer + XHEEP_SHARED_IO_OFFSET);
    quant_bit_width *layer3_shared_io =
        (quant_bit_width *)(quad_ctx[NUM_TRANSFORMER_LAYERS_OFFLOADED - 1u].data_buffer + XHEEP_SHARED_IO_OFFSET);

    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
        volatile uint32_t *dbg_word =
            (volatile uint32_t *)(void *)(fft_ctx[i].data_buffer + XHEEP_FFT_DONE_OFFSET);
        *dbg_word = 0u;
    }
    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        volatile uint32_t *dbg_word =
            (volatile uint32_t *)(void *)(quad_ctx[l].data_buffer + XHEEP_LAYER_DONE_OFFSET);
        *dbg_word = 0u;
    }

#if PROFILE
    host_mon_profile_t input_move_host_mon_start, input_move_host_mon_end;
    uint64_t input_move_start = host_cycles();
    read_host_mon_profile(&input_move_host_mon_start);
#endif
    pack_fft_input(raw_input, fft_shared_io);
    esp_flush(ACC_COH_NONE);
#if PROFILE
    uint64_t input_move_end = host_cycles();
    read_host_mon_profile(&input_move_host_mon_end);
    printf("[PROFILE] full_pipeline_p2p_fft_input_move host_cycles=%llu\n",
           (unsigned long long)host_cycles_diff(input_move_start, input_move_end));
    print_host_mon_profile_diff("full_pipeline_p2p_fft_input_move",
                                &input_move_host_mon_start, &input_move_host_mon_end);
#endif

    configure_full_pipeline_p2p(fft_ctx, quad_ctx, NUM_TRANSFORMER_LAYERS_OFFLOADED);

    bool fpu_done[NUM_FPU_FFT_SPLIT_TILES] = {false};
    bool quad_done[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {false};
    unsigned fpu_done_poll[NUM_FPU_FFT_SPLIT_TILES] = {0u};
    unsigned quad_done_poll[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};
    unsigned fpu_last_status[NUM_FPU_FFT_SPLIT_TILES] = {0u};
    unsigned quad_last_status[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};

#if PROFILE
    mon_profile_t fpu_mon_start[NUM_FPU_FFT_SPLIT_TILES];
    mon_profile_t fpu_mon_end[NUM_FPU_FFT_SPLIT_TILES];
    mon_profile_t quad_mon_start[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    mon_profile_t quad_mon_end[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    host_mon_profile_t fpu_host_mon_start[NUM_FPU_FFT_SPLIT_TILES];
    host_mon_profile_t fpu_host_mon_end[NUM_FPU_FFT_SPLIT_TILES];
    host_mon_profile_t quad_host_mon_start[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    host_mon_profile_t quad_host_mon_end[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    host_mon_profile_t total_host_mon_start, total_host_mon_end;
    uint64_t fpu_host_start[NUM_FPU_FFT_SPLIT_TILES] = {0u};
    uint64_t fpu_host_end[NUM_FPU_FFT_SPLIT_TILES] = {0u};
    uint64_t quad_host_start[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};
    uint64_t quad_host_end[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};
    uint64_t total_host_start = host_cycles();
    read_host_mon_profile(&total_host_mon_start);
#endif

    for (int l = (int)NUM_TRANSFORMER_LAYERS_OFFLOADED - 1; l >= 0; --l) {
#if PROFILE
        quad_host_start[l] = host_cycles();
        read_mon_profile(quad_ctx[l].dev, &quad_mon_start[l]);
        read_host_mon_profile(&quad_host_mon_start[l]);
#endif
        xheep_quadrilatero_program_start(quad_ctx[l].dev, false);
        xheep_quadrilatero_start(quad_ctx[l].dev);
    }

    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
#if PROFILE
        fpu_host_start[i] = host_cycles();
        read_mon_profile(fft_ctx[i].dev, &fpu_mon_start[i]);
        read_host_mon_profile(&fpu_host_mon_start[i]);
#endif
        xheep_fpu_program_start(fft_ctx[i].dev, false);
        xheep_fpu_start(fft_ctx[i].dev);
    }

    unsigned done_count = 0u;
    unsigned poll_count = 0u;
    bool timed_out = false;
    const unsigned total_targets = NUM_FPU_FFT_SPLIT_TILES + NUM_TRANSFORMER_LAYERS_OFFLOADED;
    const unsigned max_polls = 2000000u;

    while (done_count < total_targets) {
        poll_count++;
        if (poll_count >= max_polls) {
            timed_out = true;
            break;
        }

        for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
            if (fpu_done[i]) continue;
            unsigned status = ioread32(fft_ctx[i].dev, STATUS_REG);
            fpu_last_status[i] = status;
            if (status & STATUS_MASK_DONE) {
                fpu_done[i] = true;
                fpu_done_poll[i] = poll_count;
                done_count++;
#if PROFILE
                fpu_host_end[i] = host_cycles();
                read_mon_profile(fft_ctx[i].dev, &fpu_mon_end[i]);
                read_host_mon_profile(&fpu_host_mon_end[i]);
#endif
            }
        }

        for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
            if (quad_done[l]) continue;
            unsigned status = ioread32(quad_ctx[l].dev, STATUS_REG);
            quad_last_status[l] = status;
            if (status & STATUS_MASK_DONE) {
                quad_done[l] = true;
                quad_done_poll[l] = poll_count;
                done_count++;
#if PROFILE
                quad_host_end[l] = host_cycles();
                read_mon_profile(quad_ctx[l].dev, &quad_mon_end[l]);
                read_host_mon_profile(&quad_host_mon_end[l]);
#endif
            }
        }

        if ((poll_count % POLL_REPORT_PERIOD) == 0u) {
            printf("[WAIT][P2P] polls=%u done=%u/%u\n",
                   poll_count, done_count, total_targets);
        }
    }

    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
        iowrite32(fft_ctx[i].dev, CMD_REG, 0x0);
    }
    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        iowrite32(quad_ctx[l].dev, CMD_REG, 0x0);
    }

#if PROFILE
    uint64_t total_host_end = host_cycles();
    read_host_mon_profile(&total_host_mon_end);
    printf("[PROFILE] full_pipeline_p2p_run host_cycles=%llu polls=%u\n",
           (unsigned long long)host_cycles_diff(total_host_start, total_host_end), poll_count);
    print_host_mon_profile_diff("full_pipeline_p2p_run", &total_host_mon_start, &total_host_mon_end);
#endif

    if (timed_out) {
        for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
            if (fpu_done[i]) continue;
            uint32_t dbg = *((volatile uint32_t *)(void *)(fft_ctx[i].data_buffer + XHEEP_FFT_DONE_OFFSET));
            uint16_t stage = (uint16_t)(dbg & 0xffffu);
            uint16_t arg = (uint16_t)(dbg >> 16);
            printf("Error: FPU %s timeout status=0x%08x polls=%u dbg=0x%08x stage=%u(%s) arg=%u\n",
                   fft_ctx[i].tag, fpu_last_status[i], poll_count, dbg,
                   (unsigned)stage, fft_debug_stage_name(stage), (unsigned)arg);
        }
        for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
            if (quad_done[l]) continue;
            uint32_t dbg = *((volatile uint32_t *)(void *)(quad_ctx[l].data_buffer + XHEEP_LAYER_DONE_OFFSET));
            uint16_t stage = (uint16_t)(dbg & 0xffffu);
            uint16_t arg = (uint16_t)(dbg >> 16);
            printf("Error: quad layer%u timeout status=0x%08x polls=%u dbg=0x%08x stage=%u(%s) arg=%u\n",
                   l, quad_last_status[l], poll_count, dbg,
                   (unsigned)stage, layer_debug_stage_name(stage), (unsigned)arg);
        }
        return -1;
    }

#if PROFILE
    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
        printf("[PROFILE] fpu_%s_p2p_run host_cycles=%llu polls=%u\n",
               fft_ctx[i].tag,
               (unsigned long long)host_cycles_diff(fpu_host_start[i], fpu_host_end[i]),
               fpu_done_poll[i]);
        print_mon_profile_diff(fpu_fft_run_label(fft_ctx[i].tag), fft_ctx[i].dev,
                               &fpu_mon_start[i], &fpu_mon_end[i]);
        print_host_mon_profile_diff(fpu_fft_run_label(fft_ctx[i].tag),
                                    &fpu_host_mon_start[i], &fpu_host_mon_end[i]);
    }
    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        printf("[PROFILE] quad_layer%u_p2p_run host_cycles=%llu polls=%u\n",
               l,
               (unsigned long long)host_cycles_diff(quad_host_start[l], quad_host_end[l]),
               quad_done_poll[l]);
        print_mon_profile_diff(quad_run_label(l), quad_ctx[l].dev,
                               &quad_mon_start[l], &quad_mon_end[l]);
        print_host_mon_profile_diff(quad_run_label(l),
                                    &quad_host_mon_start[l], &quad_host_mon_end[l]);
    }
#endif

#if PROFILE
    host_mon_profile_t output_move_host_mon_start, output_move_host_mon_end;
    uint64_t output_move_start = host_cycles();
    read_host_mon_profile(&output_move_host_mon_start);
#endif
    memcpy(layer3_output, layer3_shared_io, XHEEP_LAYER_BYTES);
#if PROFILE
    uint64_t output_move_end = host_cycles();
    read_host_mon_profile(&output_move_host_mon_end);
    printf("[PROFILE] full_pipeline_p2p_layer3_output_move host_cycles=%llu\n",
           (unsigned long long)host_cycles_diff(output_move_start, output_move_end));
    print_host_mon_profile_diff("full_pipeline_p2p_layer3_output_move",
                                &output_move_host_mon_start, &output_move_host_mon_end);
#endif

    return 0;
}

#if CONTINUE_RUN
static bool restart_continuous_fft_tile_if_done(const fpu_fft_ctx_t *ctx)
{
    const unsigned status = ioread32(ctx->dev, STATUS_REG);

    if ((status & STATUS_MASK_DONE) == 0u) return false;

    printf("[STREAM][fft] %s done status=0x%08x tile=(y=%u x=%u), resetting\n",
           ctx->tag, status, esp_get_y(ctx->dev), esp_get_x(ctx->dev));

    iowrite32(ctx->dev, CMD_REG, 0x0);

    unsigned clear_polls = 0u;
    for (unsigned polls = 0; polls < 1000000u; ++polls) {
        const unsigned cleared = ioread32(ctx->dev, STATUS_REG);
        clear_polls = polls;
        if ((cleared & (STATUS_MASK_DONE | STATUS_MASK_RUN)) == 0u) {
            printf("[STREAM][fft] %s reset-cleared after %u polls, relaunching\n",
                   ctx->tag, polls);
            xheep_fpu_program_start(ctx->dev, false);
            xheep_fpu_start(ctx->dev);
            return true;
        }
    }

    printf("[STREAM][fft] %s reset did not clear status within %u polls, last_status=0x%08x\n",
           ctx->tag, clear_polls + 1u, ioread32(ctx->dev, STATUS_REG));
    return false;
}

static int start_full_pipeline_p2p_continuous(const fpu_fft_ctx_t *fft_ctx,
                                              const quad_layer_ctx_t *quad_ctx,
                                              const quant_bit_width *raw_input)
{
    if (fft_ctx == NULL || quad_ctx == NULL || raw_input == NULL) return -1;
    if (fft_ctx[0].data_buffer == NULL || fft_ctx[1].data_buffer == NULL) return -1;
    if (fft_ctx[0].data_buffer != fft_ctx[1].data_buffer) return -1;

    quant_bit_width *fft_shared_io =
        (quant_bit_width *)(fft_ctx[0].data_buffer + XHEEP_SHARED_IO_OFFSET);

    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
        volatile uint32_t *dbg_word =
            (volatile uint32_t *)(void *)(fft_ctx[i].data_buffer + XHEEP_FFT_DONE_OFFSET);
        *dbg_word = 0u;
    }
    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        volatile uint32_t *done_word =
            (volatile uint32_t *)(void *)(quad_ctx[l].data_buffer + XHEEP_LAYER_DONE_OFFSET);
        *done_word = 0u;
    }

    pack_fft_input(raw_input, fft_shared_io);
    esp_flush(ACC_COH_NONE);

    printf("[STREAM][launch] configure_full_pipeline_p2p begin\n");
    configure_full_pipeline_p2p(fft_ctx, quad_ctx, NUM_TRANSFORMER_LAYERS_OFFLOADED);
    printf("[STREAM][launch] configure_full_pipeline_p2p done\n");

    for (int l = (int)NUM_TRANSFORMER_LAYERS_OFFLOADED - 1; l >= 0; --l) {
        printf("[STREAM][launch] quad%u program_start tile=(y=%u x=%u)\n",
               (unsigned)l, esp_get_y(quad_ctx[l].dev), esp_get_x(quad_ctx[l].dev));
        xheep_quadrilatero_program_start(quad_ctx[l].dev, false);
        printf("[STREAM][launch] quad%u start tile=(y=%u x=%u)\n",
               (unsigned)l, esp_get_y(quad_ctx[l].dev), esp_get_x(quad_ctx[l].dev));
        xheep_quadrilatero_start(quad_ctx[l].dev);
        printf("[STREAM][launch] quad%u started\n", (unsigned)l);
    }

    for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
        printf("[STREAM][launch] fpu%u(%s) program_start tile=(y=%u x=%u)\n",
               i, fft_ctx[i].tag, esp_get_y(fft_ctx[i].dev), esp_get_x(fft_ctx[i].dev));
        xheep_fpu_program_start(fft_ctx[i].dev, false);
        printf("[STREAM][launch] fpu%u(%s) start tile=(y=%u x=%u)\n",
               i, fft_ctx[i].tag, esp_get_y(fft_ctx[i].dev), esp_get_x(fft_ctx[i].dev));
        xheep_fpu_start(fft_ctx[i].dev);
        printf("[STREAM][launch] fpu%u(%s) started\n", i, fft_ctx[i].tag);
    }

    printf("[STREAM][launch] all accelerators started\n");
    return 0;
}
#endif

int run_quad_pipeline_p2p(const quad_layer_ctx_t *ctx,
                          const quant_bit_width *layer0_input,
                          quant_bit_width *layer3_output)
{
    if (ctx == NULL || layer0_input == NULL || layer3_output == NULL) return -1;

    quant_bit_width *layer0_shared_io =
        (quant_bit_width *)(ctx[0].data_buffer + XHEEP_SHARED_IO_OFFSET);
    quant_bit_width *layer3_shared_io =
        (quant_bit_width *)(ctx[NUM_TRANSFORMER_LAYERS_OFFLOADED - 1u].data_buffer + XHEEP_SHARED_IO_OFFSET);

    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        volatile uint32_t *dbg_word =
            (volatile uint32_t *)(void *)(ctx[l].data_buffer + XHEEP_LAYER_DONE_OFFSET);
        *dbg_word = 0u;
    }

#if PROFILE
    host_mon_profile_t input_move_host_mon_start, input_move_host_mon_end;
    uint64_t input_move_start = host_cycles();
    read_host_mon_profile(&input_move_host_mon_start);
#endif
    memcpy(layer0_shared_io, layer0_input, XHEEP_LAYER_BYTES);
    esp_flush(ACC_COH_NONE);
#if PROFILE
    uint64_t input_move_end = host_cycles();
    read_host_mon_profile(&input_move_host_mon_end);
    printf("[PROFILE] quad_pipeline_p2p_layer0_input_move host_cycles=%llu\n",
           (unsigned long long)host_cycles_diff(input_move_start, input_move_end));
    print_host_mon_profile_diff("quad_pipeline_p2p_layer0_input_move",
                                &input_move_host_mon_start, &input_move_host_mon_end);
#endif

    configure_quad_chain_p2p(ctx, NUM_TRANSFORMER_LAYERS_OFFLOADED);

    bool done[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {false};
    unsigned last_status[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};

#if PROFILE
    unsigned done_poll[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};
    mon_profile_t run_mon_start[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    mon_profile_t run_mon_end[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    host_mon_profile_t run_host_mon_start[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    host_mon_profile_t run_host_mon_end[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    uint64_t run_host_start[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};
    uint64_t run_host_end[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};
#endif

    for (int l = (int)NUM_TRANSFORMER_LAYERS_OFFLOADED - 1; l >= 0; --l) {
#if PROFILE
        run_host_start[l] = host_cycles();
        read_mon_profile(ctx[l].dev, &run_mon_start[l]);
        read_host_mon_profile(&run_host_mon_start[l]);
#endif
        xheep_quadrilatero_program_start(ctx[l].dev, false);
        xheep_quadrilatero_start(ctx[l].dev);
    }

    unsigned done_count = 0u;
    unsigned poll_count = 0u;
    bool timed_out = false;
    const unsigned max_polls = 200000000u;

    while (done_count < NUM_TRANSFORMER_LAYERS_OFFLOADED) {
        poll_count++;
        if (poll_count >= max_polls) {
            timed_out = true;
            break;
        }

        for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
            if (done[l]) continue;
            unsigned status = ioread32(ctx[l].dev, STATUS_REG);
            last_status[l] = status;
            if (status & STATUS_MASK_DONE) {
                done[l] = true;
#if PROFILE
                done_poll[l] = poll_count;
#endif
                done_count++;
#if PROFILE
                run_host_end[l] = host_cycles();
                read_mon_profile(ctx[l].dev, &run_mon_end[l]);
                read_host_mon_profile(&run_host_mon_end[l]);
#endif
            }
        }

        if ((poll_count % POLL_REPORT_PERIOD) == 0u) {
            printf("[WAIT][QUAD-P2P] polls=%u done=%u/%u\n",
                   poll_count, done_count, (unsigned)NUM_TRANSFORMER_LAYERS_OFFLOADED);
        }
    }

    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        iowrite32(ctx[l].dev, CMD_REG, 0x0);
    }

    if (timed_out) {
        for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
            if (done[l]) continue;
            uint32_t dbg = *((volatile uint32_t *)(void *)(ctx[l].data_buffer + XHEEP_LAYER_DONE_OFFSET));
            uint16_t stage = (uint16_t)(dbg & 0xffffu);
            uint16_t arg = (uint16_t)(dbg >> 16);
            printf("Error: quad layer%u P2P timeout status=0x%08x polls=%u dbg=0x%08x stage=%u(%s) arg=%u\n",
                   l, last_status[l], poll_count, dbg,
                   (unsigned)stage, layer_debug_stage_name(stage), (unsigned)arg);
        }
        return -1;
    }

#if PROFILE
    uint64_t host_elapsed_cycles[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};
    uint64_t mon_total_cycles[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};
    uint64_t mon_mem_cycles[NUM_TRANSFORMER_LAYERS_OFFLOADED] = {0u};

    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        uint64_t mem_start = ((uint64_t)run_mon_start[l].acc.acc_mem_hi << 32) | run_mon_start[l].acc.acc_mem_lo;
        uint64_t mem_end   = ((uint64_t)run_mon_end[l].acc.acc_mem_hi << 32) | run_mon_end[l].acc.acc_mem_lo;
        uint64_t tot_start = ((uint64_t)run_mon_start[l].acc.acc_tot_hi << 32) | run_mon_start[l].acc.acc_tot_lo;
        uint64_t tot_end   = ((uint64_t)run_mon_end[l].acc.acc_tot_hi << 32) | run_mon_end[l].acc.acc_tot_lo;

        host_elapsed_cycles[l] = host_cycles_diff(run_host_start[l], run_host_end[l]);
        mon_mem_cycles[l] = host_cycles_diff(mem_start, mem_end);
        mon_total_cycles[l] = host_cycles_diff(tot_start, tot_end);
        printf("[PROFILE] quad_layer%u_p2p_run host_cycles=%llu polls=%u\n",
               l,
               (unsigned long long)host_elapsed_cycles[l],
               done_poll[l]);
        print_mon_profile_diff(quad_run_label(l), ctx[l].dev, &run_mon_start[l], &run_mon_end[l]);
        print_host_mon_profile_diff(quad_run_label(l), &run_host_mon_start[l], &run_host_mon_end[l]);
    }

    /*
     * In the linear P2P chain (L0->L1->L2->L3), downstream layers include
     * upstream blocking in their DMA-in wait time. Report an "exclusive"
     * metric by subtracting predecessor total runtime.
     */
    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        uint64_t upstream_total = (l == 0u) ? 0u : mon_total_cycles[l - 1u];
        uint64_t upstream_host = (l == 0u) ? 0u : host_elapsed_cycles[l - 1u];

        uint64_t excl_total = (mon_total_cycles[l] >= upstream_total)
                                  ? (mon_total_cycles[l] - upstream_total)
                                  : 0u;
        uint64_t excl_mem = (mon_mem_cycles[l] >= upstream_total)
                                ? (mon_mem_cycles[l] - upstream_total)
                                : 0u;
        uint64_t excl_compute = (excl_total >= excl_mem) ? (excl_total - excl_mem) : 0u;
        uint64_t excl_host = (host_elapsed_cycles[l] >= upstream_host)
                                 ? (host_elapsed_cycles[l] - upstream_host)
                                 : 0u;

        printf("[PROFILE][P2P][EXCL] layer%u host=%llu total=%llu mem=%llu compute=%llu removed_upstream=%llu\n",
               l,
               (unsigned long long)excl_host,
               (unsigned long long)excl_total,
               (unsigned long long)excl_mem,
               (unsigned long long)excl_compute,
               (unsigned long long)upstream_total);
    }
#endif

#if PROFILE
    host_mon_profile_t output_move_host_mon_start, output_move_host_mon_end;
    uint64_t output_move_start = host_cycles();
    read_host_mon_profile(&output_move_host_mon_start);
#endif
    memcpy(layer3_output, layer3_shared_io, XHEEP_LAYER_BYTES);
#if PROFILE
    uint64_t output_move_end = host_cycles();
    read_host_mon_profile(&output_move_host_mon_end);
    printf("[PROFILE] quad_pipeline_p2p_layer3_output_move host_cycles=%llu\n",
           (unsigned long long)host_cycles_diff(output_move_start, output_move_end));
    print_host_mon_profile_diff("quad_pipeline_p2p_layer3_output_move",
                                &output_move_host_mon_start, &output_move_host_mon_end);
#endif

    return 0;
}
#endif

int main(void)
{
    printf("Full X-HEEP transformer offload\n");
    printf("  D_SEQ=%d D_MODEL=%d D_Q=%d NUM_HEAD=%d D_FF=%d NUM_LAYERS=%d\n",
           D_SEQ, D_MODEL, D_Q, NUM_HEAD, D_FF, NUM_LAYERS);
    printf("  USE_P2P=%u\n", (unsigned)USE_P2P);

    quant_bit_width *rawInputSignal = raw_signal + 160 * 15;

    static quant_bit_width pre_acc[XHEEP_LAYER_ELEMS];
    static quant_bit_width pre_cpu[XHEEP_LAYER_ELEMS];

    static quant_bit_width acc_state[XHEEP_LAYER_ELEMS];
    static quant_bit_width cpu_state[XHEEP_LAYER_ELEMS];
    static quant_bit_width acc_layer_out[XHEEP_LAYER_ELEMS];
    static quant_bit_width cpu_layer_out[XHEEP_LAYER_ELEMS];

    static quant_bit_width cpu_input_normalized[XHEEP_LAYER_ELEMS];
    static quant_bit_width cpu_qkv[XHEEP_LAYER_QKV_ELEMS];
    static quant_bit_width cpu_intermediate[XHEEP_LAYER_INTERM_ELEMS];

    static quant_bit_width acc_logits[D_MODEL];
    static quant_bit_width cpu_logits[D_MODEL];
    int32_t acc_distances[2] = {0};
    int32_t cpu_distances[2] = {0};
    int discrepancy_count = 0;

    struct esp_device *fpu_devs = NULL;
    struct esp_device *quad_devs = NULL;
    int fpu_ndev = 0;
    int quad_ndev = 0;
    static const unsigned quad_layer_coords[NUM_TRANSFORMER_LAYERS_OFFLOADED][2] = {
        {1u, 1u}, // layer 0
        {1u, 2u}, // layer 1
        {2u, 1u}, // layer 2
        {2u, 2u}  // layer 3
    };

    if (probe_devices(ACC_COMPAT_FPU_PRIMARY, ACC_COMPAT_FPU_FALLBACK, &fpu_devs, &fpu_ndev) != 0) {
        printf("Error: FPU accelerator not found (compat %s / %s)\n",
               ACC_COMPAT_FPU_PRIMARY, ACC_COMPAT_FPU_FALLBACK);
        return 1;
    }
    if (probe_devices(ACC_COMPAT_QUAD_PRIMARY, ACC_COMPAT_QUAD_FALLBACK, &quad_devs, &quad_ndev) != 0) {
        printf("Error: quadrilatero accelerator not found (compat %s / %s)\n",
               ACC_COMPAT_QUAD_PRIMARY, ACC_COMPAT_QUAD_FALLBACK);
        return 1;
    }

    printf("[DISCOVERY] fpu tiles=%d quadrilatero tiles=%d\n", fpu_ndev, quad_ndev);
    if (quad_ndev < NUM_TRANSFORMER_LAYERS_OFFLOADED) {
        printf("Error: need at least %d quadrilatero tiles, found %d\n",
               NUM_TRANSFORMER_LAYERS_OFFLOADED, quad_ndev);
        return 1;
    }

    for (int i = 0; i < fpu_ndev; ++i) {
        printf("[DISCOVERY] fpu[%d] tile=(y=%u x=%u)\n",
               i, (unsigned)esp_get_y(&fpu_devs[i]), (unsigned)esp_get_x(&fpu_devs[i]));
    }
    for (int i = 0; i < quad_ndev; ++i) {
        printf("[DISCOVERY] quad[%d] tile=(y=%u x=%u)\n",
               i, (unsigned)esp_get_y(&quad_devs[i]), (unsigned)esp_get_x(&quad_devs[i]));
    }

    if (fpu_ndev < (int)NUM_FPU_FFT_SPLIT_TILES) {
        printf("Error: need at least %u FPU tiles, found %d\n",
               NUM_FPU_FFT_SPLIT_TILES, fpu_ndev);
        return 1;
    }

    struct esp_device *fft0_dev = find_device_by_coords(fpu_devs, fpu_ndev, FFT0_TILE_Y, FFT0_TILE_X);
    if (fft0_dev == NULL) {
        fft0_dev = &fpu_devs[0];
        printf("[WARN] configured FFT_0_9 tile (y=%u x=%u) not found, fallback to discovered tile (y=%u x=%u)\n",
               FFT0_TILE_Y, FFT0_TILE_X, esp_get_y(fft0_dev), esp_get_x(fft0_dev));
    }
    struct esp_device *fft1_dev = find_device_by_coords(fpu_devs, fpu_ndev, FFT1_TILE_Y, FFT1_TILE_X);
    if (fft1_dev == NULL || fft1_dev == fft0_dev) {
        fft1_dev = find_any_other_device(fpu_devs, fpu_ndev, fft0_dev);
        if (fft1_dev == NULL) {
            printf("Error: could not find a second FPU tile for FFT_10_19\n");
            return 1;
        }
        printf("[WARN] configured FFT_10_19 tile (y=%u x=%u) unavailable, fallback to discovered tile (y=%u x=%u)\n",
               FFT1_TILE_Y, FFT1_TILE_X, esp_get_y(fft1_dev), esp_get_x(fft1_dev));
    }

    fpu_fft_ctx_t fpu_ctx[NUM_FPU_FFT_SPLIT_TILES];
    memset(fpu_ctx, 0, sizeof(fpu_ctx));
    if (init_fpu_fft_ctx(&fpu_ctx[0], fft0_dev, FPU_FFT_FW_0_9) != 0) {
        printf("Error: failed to initialize FPU FFT_0_9 context\n");
        return 1;
    }
    if (init_fpu_fft_ctx(&fpu_ctx[1], fft1_dev, FPU_FFT_FW_10_19) != 0) {
        printf("Error: failed to initialize FPU FFT_10_19 context\n");
        return 1;
    }

    /* Both split kernels must share one external data aperture. */
    configure_data_pt(fpu_ctx[1].dev, fpu_ctx[0].ptable_data, fpu_ctx[0].nchunk_data);
    fpu_ctx[1].data_buffer = fpu_ctx[0].data_buffer;
    fpu_ctx[1].data_buffer_size = fpu_ctx[0].data_buffer_size;
    fpu_ctx[1].ptable_data = fpu_ctx[0].ptable_data;
    fpu_ctx[1].nchunk_data = fpu_ctx[0].nchunk_data;
    printf("[XHEEP][FFT] shared_data tile=(y=%u x=%u)<-(y=%u x=%u) buffer=%p size=%u\n",
           esp_get_y(fpu_ctx[1].dev), esp_get_x(fpu_ctx[1].dev),
           esp_get_y(fpu_ctx[0].dev), esp_get_x(fpu_ctx[0].dev),
           (void *)fpu_ctx[0].data_buffer, (unsigned)fpu_ctx[0].data_buffer_size);

    if (fpu_ctx[0].dev == fpu_ctx[1].dev) {
        printf("Error: FFT split contexts are mapped to the same tile\n");
        return 1;
    }

    quad_layer_ctx_t quad_ctx[NUM_TRANSFORMER_LAYERS_OFFLOADED];
    memset(quad_ctx, 0, sizeof(quad_ctx));
    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        struct esp_device *layer_dev = find_device_by_coords(
            quad_devs, quad_ndev, quad_layer_coords[l][0], quad_layer_coords[l][1]);
        if (layer_dev == NULL) {
            printf("Error: configured layer%u quadrilatero tile (y=%u x=%u) not found\n",
                   l, quad_layer_coords[l][0], quad_layer_coords[l][1]);
            return 1;
        }
        if (init_quad_layer_ctx(&quad_ctx[l], layer_dev, l) != 0) {
            printf("Error: failed to initialize quadrilatero layer %u context\n", l);
            return 1;
        }
    }

    memset(pre_acc, 0, sizeof(pre_acc));
    memset(pre_cpu, 0, sizeof(pre_cpu));

#if CHECK_RESULTS
#if PROFILE
    host_mon_profile_t pre_cpu_host_mon_start, pre_cpu_host_mon_end;
    uint64_t pre_cpu_start = host_cycles();
    read_host_mon_profile(&pre_cpu_host_mon_start);
#endif
    prelayer_reference_cpu(rawInputSignal, pre_cpu);
#if PROFILE
    uint64_t pre_cpu_end = host_cycles();
    read_host_mon_profile(&pre_cpu_host_mon_end);
    printf("[PROFILE] cpu_prelayer_reference host_cycles=%llu\n",
           (unsigned long long)host_cycles_diff(pre_cpu_start, pre_cpu_end));
    print_host_mon_profile_diff("cpu_prelayer_reference", &pre_cpu_host_mon_start, &pre_cpu_host_mon_end);
#endif
#endif

#if !USE_P2P
    if (run_fpu_fft_split_parallel(&fpu_ctx[0], &fpu_ctx[1], rawInputSignal, pre_acc) != 0) {
        printf("Error: FPU FFT run failed\n");
        // return 1;
    }
#if CHECK_RESULTS
    if (compare_buffers("prelayer", pre_acc, pre_cpu, XHEEP_LAYER_ELEMS) != 0) {
        printf("[WARN] prelayer mismatch (continuing debug flow)\n");
        discrepancy_count++;
    }
#endif
    memcpy(acc_state, pre_acc, sizeof(acc_state));
#else
    memset(acc_state, 0, sizeof(acc_state));
#endif
#if CHECK_RESULTS
    memcpy(cpu_state, pre_cpu, sizeof(cpu_state));
#else
    memset(cpu_state, 0, sizeof(cpu_state));
#endif

    quant_bit_width *weightVec[TRANSFORMER_WEIGHT_VEC_LEN];
    quant_bit_width *biasVec[TRANSFORMER_WEIGHT_VEC_LEN];
    getWeights(weightVec);
    getBiases(biasVec);
    TransformerBlock *tb = createTransformerBlock(D_SEQ, D_MODEL, D_Q, NUM_HEAD, D_FF,
                                                  weightVec, biasVec, getClassToken(), getPosEmbedding());

#if CONTINUE_RUN
#if !USE_P2P
#error "CONTINUE_RUN requires USE_P2P=1"
#endif

    printf("[RUN] CONTINUE_RUN start: fft(2-way)->layer0->layer1->layer2->layer3(mem)\n");
    if (start_full_pipeline_p2p_continuous(fpu_ctx, quad_ctx, rawInputSignal) != 0) {
        printf("Error: failed to start full P2P continuous pipeline\n");
        return 1;
    }
    printf("[STREAM][launch] returned to host wait loop\n");
    quant_bit_width *layer3_shared_io =
        (quant_bit_width *)(quad_ctx[NUM_TRANSFORMER_LAYERS_OFFLOADED - 1u].data_buffer +
                            XHEEP_SHARED_IO_OFFSET);
    volatile uint32_t *layer3_done_word =
        (volatile uint32_t *)(void *)(quad_ctx[NUM_TRANSFORMER_LAYERS_OFFLOADED - 1u].data_buffer +
                                      XHEEP_LAYER_DONE_OFFSET);
        uint32_t done_word_initial = *layer3_done_word;
    printf("[STREAM][addr] done_word_initial=%u\n", (unsigned)done_word_initial);

    unsigned iter = 0u;
    unsigned wait_polls = 0u;
    const unsigned wait_report_period = 2500000u;

    while (1) {
        for (unsigned i = 0; i < NUM_FPU_FFT_SPLIT_TILES; ++i) {
            restart_continuous_fft_tile_if_done(&fpu_ctx[i]);
        }

        uint32_t curr_done_word = *layer3_done_word;
        if (curr_done_word == 0u) {
            wait_polls++;
            if ((wait_polls % wait_report_period) == 0u) {
                uint32_t status_fpu0 = ioread32(fpu_ctx[0].dev, STATUS_REG);
                uint32_t status_fpu1 = ioread32(fpu_ctx[1].dev, STATUS_REG);
                uint32_t dbg_fpu0 = *((volatile uint32_t *)(void *)(fpu_ctx[0].data_buffer + XHEEP_FFT_DONE_OFFSET));
                uint32_t dbg_fpu1 = *((volatile uint32_t *)(void *)(fpu_ctx[1].data_buffer + XHEEP_FFT_DONE_OFFSET));
                uint16_t stage0 = (uint16_t)(dbg_fpu0 & 0xffffu);
                uint16_t arg0 = (uint16_t)(dbg_fpu0 >> 16);
                uint16_t stage1 = (uint16_t)(dbg_fpu1 & 0xffffu);
                uint16_t arg1 = (uint16_t)(dbg_fpu1 >> 16);
                printf("[STREAM][wait] polls=%u done_word=%u status_fpu0=0x%08x dbg_fpu0=0x%08x(%s arg=%u) status_fpu1=0x%08x dbg_fpu1=0x%08x(%s arg=%u)\n",
                       wait_polls,
                       (unsigned)curr_done_word,
                       status_fpu0,
                       dbg_fpu0, fft_debug_stage_name(stage0), (unsigned)arg0,
                       status_fpu1,
                       dbg_fpu1, fft_debug_stage_name(stage1), (unsigned)arg1);
            }
            continue;
        }
        printf("[STREAM] layer3 done after %u polls done_word=%u\n",
               wait_polls, (unsigned)curr_done_word);
        wait_polls = 0u;
        *layer3_done_word = 0u;

        memcpy(acc_state, layer3_shared_io, XHEEP_LAYER_BYTES);
        memset(acc_logits, 0, sizeof(acc_logits));
        memset(cpu_input_normalized, 0, sizeof(cpu_input_normalized));

        normalize(&tb->mlp_head_norm, acc_state, cpu_input_normalized);
        computeDense(tb->mlp_head_linear, 1, cpu_input_normalized, acc_logits);
        prototype_distances(prototypes, acc_logits, acc_distances, D_MODEL, 2);

        int pred = (acc_distances[0] <= acc_distances[1]) ? 0 : 1;
        printf("[STREAM] iter=%u distances: class0=%d class1=%d pred=%d\n",
               iter, acc_distances[0], acc_distances[1], pred);
        iter++;
    }
#endif

#if USE_P2P
    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        memset(cpu_layer_out, 0, sizeof(cpu_layer_out));
        memset(cpu_input_normalized, 0, sizeof(cpu_input_normalized));
        memset(cpu_qkv, 0, sizeof(cpu_qkv));
        memset(cpu_intermediate, 0, sizeof(cpu_intermediate));

#if PROFILE
        host_mon_profile_t layer_cpu_host_mon_start, layer_cpu_host_mon_end;
        uint64_t layer_cpu_start = host_cycles();
        read_host_mon_profile(&layer_cpu_host_mon_start);
#endif
        run_cpu_layer(tb, (int)l, cpu_state, cpu_layer_out,
                      cpu_input_normalized, cpu_qkv, cpu_intermediate);
#if PROFILE
        uint64_t layer_cpu_end = host_cycles();
        read_host_mon_profile(&layer_cpu_host_mon_end);
        printf("[PROFILE] cpu_layer%u host_cycles=%llu\n",
               l,
               (unsigned long long)host_cycles_diff(layer_cpu_start, layer_cpu_end));
        print_host_mon_profile_diff(cpu_layer_label(l),
                                    &layer_cpu_host_mon_start, &layer_cpu_host_mon_end);
#endif
        memcpy(cpu_state, cpu_layer_out, sizeof(cpu_state));
    }

    printf("[RUN] P2P full pipeline start: fft(2-way)->layer0->layer1->layer2->layer3(mem)\n");
    if (run_full_pipeline_p2p(fpu_ctx, quad_ctx, rawInputSignal, acc_state) != 0) {
        printf("Error: full P2P pipeline run failed\n");
        discrepancy_count++;
    }
#else
    for (unsigned l = 0; l < NUM_TRANSFORMER_LAYERS_OFFLOADED; ++l) {
        const char *layer_label = layer_compare_label(l);
        printf("[RUN] layer %u on quadrilatero tile (y=%u x=%u)\n",
               l, esp_get_y(quad_ctx[l].dev), esp_get_x(quad_ctx[l].dev));

        memset(acc_layer_out, 0, sizeof(acc_layer_out));
        // Debug mode: feed accelerator with CPU reference state from previous layer.
        if (run_quad_layer(&quad_ctx[l], cpu_state, acc_layer_out) != 0) {
            printf("Error: quadrilatero layer %u run failed\n", l);
            // return 1;
        }

        memset(cpu_layer_out, 0, sizeof(cpu_layer_out));
        memset(cpu_input_normalized, 0, sizeof(cpu_input_normalized));
        memset(cpu_qkv, 0, sizeof(cpu_qkv));
        memset(cpu_intermediate, 0, sizeof(cpu_intermediate));

#if PROFILE
        host_mon_profile_t layer_cpu_host_mon_start, layer_cpu_host_mon_end;
        uint64_t layer_cpu_start = host_cycles();
        read_host_mon_profile(&layer_cpu_host_mon_start);
#endif
        run_cpu_layer(tb, (int)l, cpu_state, cpu_layer_out,
                      cpu_input_normalized, cpu_qkv, cpu_intermediate);
#if PROFILE
        uint64_t layer_cpu_end = host_cycles();
        read_host_mon_profile(&layer_cpu_host_mon_end);
        printf("[PROFILE] cpu_layer%u host_cycles=%llu\n",
               l,
               (unsigned long long)host_cycles_diff(layer_cpu_start, layer_cpu_end));
        print_host_mon_profile_diff(cpu_layer_label(l),
                                    &layer_cpu_host_mon_start, &layer_cpu_host_mon_end);
#endif

        if (compare_buffers(layer_label, acc_layer_out, cpu_state, XHEEP_LAYER_ELEMS) != 0) {
            printf("[WARN] layer %u mismatch (continuing debug flow)\n", l);
            discrepancy_count++;
        }

        memcpy(acc_state, acc_layer_out, sizeof(acc_state));
    }
#endif

    memset(acc_logits, 0, sizeof(acc_logits));
    memset(cpu_logits, 0, sizeof(cpu_logits));
    memset(cpu_input_normalized, 0, sizeof(cpu_input_normalized));

    normalize(&tb->mlp_head_norm, acc_state, cpu_input_normalized);
    computeDense(tb->mlp_head_linear, 1, cpu_input_normalized, acc_logits);

    memset(cpu_input_normalized, 0, sizeof(cpu_input_normalized));
    normalize(&tb->mlp_head_norm, cpu_state, cpu_input_normalized);
    computeDense(tb->mlp_head_linear, 1, cpu_input_normalized, cpu_logits);

    if (compare_buffers("mlp_head", acc_logits, cpu_logits, D_MODEL) != 0) {
        printf("[WARN] MLP head mismatch (continuing debug flow)\n");
        discrepancy_count++;
    }

    prototype_distances(prototypes, acc_logits, acc_distances, D_MODEL, 2);
    prototype_distances(prototypes, cpu_logits, cpu_distances, D_MODEL, 2);

    printf("[RESULT] distances(acc): class0=%d class1=%d\n", acc_distances[0], acc_distances[1]);
    printf("[RESULT] distances(cpu): class0=%d class1=%d\n", cpu_distances[0], cpu_distances[1]);

    if (discrepancy_count == 0) {
#if USE_P2P
        printf("Full X-HEEP P2P flow completed successfully (no mismatches)\n");
#else
        printf("Full X-HEEP layer-by-layer flow completed successfully (no mismatches)\n");
#endif
    } else {
#if USE_P2P
        printf("Full X-HEEP P2P flow completed with %d mismatched check(s)\n",
               discrepancy_count);
#else
        printf("Full X-HEEP layer-by-layer flow completed with %d mismatched check(s)\n",
               discrepancy_count);
#endif
    }

    while (1) {
    }

    return 0;
}
