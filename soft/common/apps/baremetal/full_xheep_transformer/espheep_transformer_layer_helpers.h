#ifndef ESPHEEP_TRANSFORMER_LAYER_HELPERS_H
#define ESPHEEP_TRANSFORMER_LAYER_HELPERS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "transformer.h"
#include "transformerBlockC.h"

static void run_cpu_layer(TransformerBlock *tb,
                          int layer_idx,
                          quant_bit_width *state,
                          quant_bit_width *output,
                          quant_bit_width *input_normalized,
                          quant_bit_width *qkv,
                          quant_bit_width *intermediate);
static int compare_buffers(const char *label,
                           const quant_bit_width *a,
                           const quant_bit_width *b,
                           size_t elems);

#ifdef ESPHEEP_TRANSFORMER_LAYER_HELPERS_IMPL

static void run_cpu_layer(TransformerBlock *tb,
                          int layer_idx,
                          quant_bit_width *state,
                          quant_bit_width *output,
                          quant_bit_width *input_normalized,
                          quant_bit_width *qkv,
                          quant_bit_width *intermediate)
{
    const size_t seq_len = XHEEP_LAYER_SEQ_LEN;

    normalize(&tb->transformer_layer_0_addNorm[layer_idx], state, input_normalized);
    for (int head = 0; head < NUM_HEAD; ++head) {
        compute_SingleHeadSelfAttn(tb->selfatten[layer_idx * NUM_HEAD + head],
                                   input_normalized,
                                   output + head * (seq_len * tb->head_hidden_size_),
                                   qkv, intermediate);
    }

    multihead_transpose(output, intermediate, seq_len, tb->head_hidden_size_, tb->num_heads_);
    computeDense(tb->condense[layer_idx], seq_len, intermediate, output);
    add(state, output, seq_len, tb->input_dim_);

    normalize(&tb->transformer_layer_1_addNorm[layer_idx], state, input_normalized);
    computeDense(tb->feedForward0[layer_idx], seq_len, input_normalized, intermediate);
    activation(tb->feedForward0[layer_idx], seq_len * tb->ff_size_, intermediate, intermediate);
    computeDense(tb->feedForward1[layer_idx], seq_len, intermediate, output);
    add(state, output, seq_len, tb->input_dim_);
}

static int compare_buffers(const char *label,
                           const quant_bit_width *a,
                           const quant_bit_width *b,
                           size_t elems)
{
    size_t mismatch_count = 0;
    size_t first_idx = elems;

    for (size_t i = 0; i < elems; ++i) {
        if (a[i] != b[i]) {
            mismatch_count++;
            if (first_idx == elems) {
                first_idx = i;
            }
        }
    }

    if (mismatch_count == 0) {
        printf("[CHECK] %s PASSED (%u elements equal)\n", label, (unsigned)elems);
        return 0;
    }

    printf("[CHECK] %s FAILED mismatches=%u/%u first_idx=%u a=%d b=%d\n",
           label,
           (unsigned)mismatch_count,
           (unsigned)elems,
           (unsigned)first_idx,
           (int)a[first_idx], (int)b[first_idx]);

    size_t printed = 0;
    for (size_t i = 0; i < elems && printed < 8; ++i) {
        if (a[i] != b[i]) {
            printf("[CHECK] %s mismatch[%u]: a=%d b=%d\n",
                   label, (unsigned)i, (int)a[i], (int)b[i]);
            printed++;
        }
    }

    return -1;
}

void prototype_distances(quant_bit_width *prototype_vec,
                         const quant_bit_width *model_output,
                         int32_t *dist_vec,
                         size_t prototype_length,
                         int prototype_nums)
{
    for (int p = 0; p < prototype_nums; ++p) {
        long dist = 0;
        quant_bit_width *prototype_ptr = prototype_vec + (p * prototype_length);

        for (size_t i = 0; i < prototype_length; ++i) {
            dist += MUL_HQ(prototype_ptr[i] - model_output[i], prototype_ptr[i] - model_output[i]);
        }

        dist_vec[p] = (int32_t)(dist >> NUM_FRACTION_BITS);
    }
}

#endif

#endif
