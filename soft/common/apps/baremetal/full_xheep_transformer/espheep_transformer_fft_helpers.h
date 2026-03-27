#ifndef ESPHEEP_TRANSFORMER_FFT_HELPERS_H
#define ESPHEEP_TRANSFORMER_FFT_HELPERS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "transformer.h"
#include "data_cpp/signal_fft.h"
#include "weightsAndBiasesC.h"
#include "transformerBlockC.h"
#include "SYLT-FFT/fft.h"

static void pack_fft_input(const quant_bit_width *raw_input, quant_bit_width *packed_input);
static void prelayer_reference_cpu(const quant_bit_width *raw_input, quant_bit_width *prelayer_out);

#ifdef ESPHEEP_TRANSFORMER_FFT_HELPERS_IMPL

static inline uint32_t espheep_ilog2_u64_floor(uint64_t x)
{
    uint32_t n = 0;

    while (x >>= 1) {
        n++;
    }

    return n;
}

quant_bit_width compute_log_amp(int32_t real, int32_t imag)
{
    int32_t rs = (MUL_HQ(real, 25) >> 9);
    int32_t is = (MUL_HQ(imag, 25) >> 9);
    int64_t e = ((int64_t)rs * (int64_t)rs) + ((int64_t)is * (int64_t)is);

    if (e <= 0) {
        return (quant_bit_width)-23;
    }

    uint64_t eu = (uint64_t)e;
    uint32_t msb = espheep_ilog2_u64_floor(eu);
    uint64_t base = (uint64_t)1u << msb;
    uint32_t frac_q10 = (uint32_t)(((eu - base) << 10) / base);
    int32_t f = (int32_t)frac_q10;
    int32_t ln1pf_q10 = f - (int32_t)(((int64_t)f * f) >> 11);
    int32_t ln_e_q10 = (int32_t)(msb * 709) + ln1pf_q10;
    int32_t ln_amp_q10 = ln_e_q10 >> 1;

    return (quant_bit_width)(ln_amp_q10 >> 10);
}

static void initialize_stft_window(fft_complex_t *data, const quant_bit_width *raw_input_signal)
{
    for (size_t i = 0; i < XHEEP_FFT_INPUT_REAL_SAMPLES; ++i) {
        data[i].r = MUL_HQ(raw_input_signal[i], hanning[i]);
        data[i].i = 0;
    }
    for (size_t i = XHEEP_FFT_INPUT_REAL_SAMPLES; i < XHEEP_FFT_SIZE; ++i) {
        data[i].r = 0;
        data[i].i = 0;
    }
}

static void stft_rearrange_cpu(const quant_bit_width *raw_input_signal,
                               quant_bit_width *stft_vec,
                               size_t patch_height,
                               size_t patch_width)
{
    fft_complex_t data[XHEEP_FFT_SIZE];

    for (size_t ch = 0; ch < XHEEP_STFT_CHANNELS; ++ch) {
        for (size_t time_step = 0; time_step < XHEEP_STFT_TIME_STEPS; ++time_step) {
            const quant_bit_width *raw_signal_ptr =
                raw_input_signal + ch * XHEEP_RAW_SIGNAL_CH_SAMPLES + XHEEP_STFT_WINDOW_STRIDE * time_step;
            quant_bit_width *stft_vec_ptr =
                stft_vec +
                ch * XHEEP_STFT_TIME_STEPS * (2u * patch_height) +
                (time_step / patch_width) * patch_width * patch_height +
                (time_step % patch_width);

            initialize_stft_window(data, raw_signal_ptr);
            fft_fft(data, XHEEP_FFT_BITS);

            for (size_t index = 0; index < patch_height; ++index) {
                *stft_vec_ptr = compute_log_amp(data[index].r, data[index].i);
                stft_vec_ptr += patch_width;
            }

            stft_vec_ptr += patch_height * patch_width * 2u;
            for (size_t index = patch_height; index < 2u * patch_height; ++index) {
                *stft_vec_ptr = compute_log_amp(data[index].r, data[index].i);
                stft_vec_ptr += patch_width;
            }
        }
    }
}

static void pack_fft_input(const quant_bit_width *raw_input, quant_bit_width *packed_input)
{
    quant_bit_width *weight_vec[TRANSFORMER_WEIGHT_VEC_LEN];
    quant_bit_width *bias_vec[TRANSFORMER_WEIGHT_VEC_LEN];
    quant_bit_width *cls_token = getClassToken();
    quant_bit_width *pos_matrix = getPosEmbedding();
    size_t off = 0;

    getWeights(weight_vec);
    getBiases(bias_vec);

    memcpy(&packed_input[off], raw_input, XHEEP_PRE_RAW_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_RAW_ELEMS;

    memcpy(&packed_input[off], weight_vec[0], XHEEP_PRE_NORM1_WEIGHT_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_NORM1_WEIGHT_ELEMS;

    memcpy(&packed_input[off], bias_vec[0], XHEEP_PRE_NORM1_BIAS_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_NORM1_BIAS_ELEMS;

    memcpy(&packed_input[off], weight_vec[1], XHEEP_PRE_DENSE_WEIGHT_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_DENSE_WEIGHT_ELEMS;

    memcpy(&packed_input[off], bias_vec[1], XHEEP_PRE_DENSE_BIAS_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_DENSE_BIAS_ELEMS;

    memcpy(&packed_input[off], weight_vec[2], XHEEP_PRE_NORM2_WEIGHT_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_NORM2_WEIGHT_ELEMS;

    memcpy(&packed_input[off], bias_vec[2], XHEEP_PRE_NORM2_BIAS_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_NORM2_BIAS_ELEMS;

    memcpy(&packed_input[off], cls_token, XHEEP_PRE_CLS_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_CLS_ELEMS;

    memcpy(&packed_input[off], pos_matrix, XHEEP_PRE_POS_ELEMS * sizeof(quant_bit_width));
    off += XHEEP_PRE_POS_ELEMS;

    if (off != XHEEP_LAYER_IN_ELEMS) {
        printf("Error: fft input pack size mismatch packed=%u expected=%u\n",
               (unsigned)off, (unsigned)XHEEP_LAYER_IN_ELEMS);
    }
}

static void prelayer_reference_cpu(const quant_bit_width *raw_input, quant_bit_width *prelayer_out)
{
    static quant_bit_width stft_vec[D_EMBEDDING * D_SEQ];
    static quant_bit_width patch_out[D_SEQ * D_MODEL];
    quant_bit_width *weight_vec[TRANSFORMER_WEIGHT_VEC_LEN];
    quant_bit_width *bias_vec[TRANSFORMER_WEIGHT_VEC_LEN];
    quant_bit_width *cls_token = getClassToken();
    quant_bit_width *pos_matrix = getPosEmbedding();

    getWeights(weight_vec);
    getBiases(bias_vec);

    TransformerBlock *tb = createTransformerBlock(D_SEQ, D_MODEL, D_Q, NUM_HEAD, D_FF,
                                                  weight_vec, bias_vec, cls_token, pos_matrix);

    stft_rearrange_cpu(raw_input, stft_vec, XHEEP_STFT_PATCH_HEIGHT, XHEEP_STFT_PATCH_WIDTH);
    normalize(&tb->addNorm, stft_vec, stft_vec);
    computeDense(tb->patchEmbedding, D_SEQ, stft_vec, patch_out);
    normalize(&tb->addNorm2, patch_out, patch_out);
    clsConcatenate(tb->token, patch_out, prelayer_out);
    posEmbedding(tb->token, prelayer_out);
}

#endif

#endif
