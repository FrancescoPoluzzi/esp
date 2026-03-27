//
// Created by alireza on 10/25/21.
//

#ifndef FVLLMONTITRANSFORMER_TRANSFORMER_H
#define FVLLMONTITRANSFORMER_TRANSFORMER_H

#include <stdint.h>
#include <stddef.h>
#include "param.h"

float error_check(const quant_bit_width* groundTruth, const quant_bit_width* output, size_t length);
void prototype_distances(quant_bit_width* prototypeVec, const quant_bit_width* modelOutput, int32_t* distVec, size_t prototypeLength, int prototypeNums);
typedef struct xheep_quadrilatero_layer_ctx xheep_quadrilatero_layer_ctx_t;
int transformerInference(quant_bit_width * transformerInput, quant_bit_width * transformerOutput, quant_bit_width* input_normalized, quant_bit_width* qkv, quant_bit_width* intermediate, xheep_quadrilatero_layer_ctx_t *xheep_quadrilatero_ctx);
quant_bit_width compute_log_amp(int32_t real, int32_t imag);

#endif //FVLLMONTITRANSFORMER_TRANSFORMER_H
