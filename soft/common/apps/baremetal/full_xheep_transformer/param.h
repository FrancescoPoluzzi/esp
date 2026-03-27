//
// Created by alireza on 10/6/23.
//

#ifndef FVLLMONTITRANSFORMER_PARAM_H
#define FVLLMONTITRANSFORMER_PARAM_H
#include "stdint.h"

#define HAVE_FPU 1

#define D_Q 4
#define D_SEQ 120
#define D_MODEL 16
#define NUM_HEAD 4
#define NUM_LAYERS 4
#define D_FF 4
#define D_EMBEDDING 400


#define NUM_FRACTION_BITS 0
// #define MUL(x, y) (int32_t) (((int32_t)(x) * (int32_t)(y)) >> NUM_FRACTION_BITS)
// #define MUL(x, y) (int32_t) (((int32_t)(x) * (int32_t)(y)))
// #define MUL_LONG(x, y) (int64_t) (((int64_t)(x) * (int64_t)(y)))
// #define MUL_HQ(x, y) (int32_t) (((int32_t)(x) * (int32_t)(y)))
#define MUL(x, y) x*y 
#define MUL_LONG(x, y) x*y
#define MUL_HQ(x, y) x*y
#define SHIFT(x) ((x) >> NUM_FRACTION_BITS)

/*
 * Optional P2P pipeline mode for quadrilatero transformer layers:
 *   layer0: MEM -> P2P
 *   layer1: P2P -> P2P
 *   layer2: P2P -> P2P
 *   layer3: P2P -> MEM
 */
#define CONTINUE_RUN 0u
#define USE_P2P 0u
#define CONTINUE_RUN_PROFILE 1u
#define CONTINUE_RUN_CHECK_RESULT 1u
#define CONTINUE_RUN_DEBUG 0u
#if CONTINUE_RUN
#undef USE_P2P
#define USE_P2P 0u
#undef CONTINUE_RUN_PROFILE
#undef CONTINUE_RUN_CHECK_RESULT
#undef CONTINUE_RUN_DEBUG
#define CONTINUE_RUN_PROFILE 0u
#define CONTINUE_RUN_CHECK_RESULT 0u
#define CONTINUE_RUN_DEBUG 0u
#endif
#define XHEEP_LAYER_P2P_SRC_SLOT 1u
#define XHEEP_LAYER_P2P_NSRCS    2u
#define XHEEP_LAYER_P2P_NDESTS   1u

typedef int16_t quant_bit_width;
#endif //FVLLMONTITRANSFORMER_PARAM_H
