//
// Created by alireza on 10/6/23.
//

#include <stdio.h>
#include "transformerBlockC.h"

SingleHeadSelfAttn global_selfatten[NUM_LAYERS * NUM_HEAD];
Dense global_query_layer[NUM_LAYERS * NUM_HEAD];
Dense global_key_layer[NUM_LAYERS * NUM_HEAD];
Dense global_value_layer[NUM_LAYERS * NUM_HEAD];

Dense global_condense[NUM_LAYERS];
Dense global_patch;
Dense global_FF[NUM_LAYERS * 2];
Dense global_mlp;

TransformerBlock global_transformer_block;
TokenPosEmbedding global_token_embedding;

TransformerBlock *createTransformerBlock(size_t pre_seq_len, size_t input_dim, size_t head_hidden_size, size_t num_heads, size_t ff_size, int16_t **weightVector, int16_t **biasVector, int16_t *clsTokenVector, int16_t *posMatrix)
{
    TransformerBlock *transformerBlock = &global_transformer_block;
    transformerBlock->num_heads_ = num_heads;
    transformerBlock->head_hidden_size_ = head_hidden_size;
    transformerBlock->input_dim_ = input_dim;
    transformerBlock->ff_size_ = ff_size;

    transformerBlock->addNorm = createAddNormalize(pre_seq_len, D_EMBEDDING, weightVector[0], biasVector[0]);
    transformerBlock->patchEmbedding = &global_patch;
    createDense(transformerBlock->patchEmbedding, D_EMBEDDING, D_MODEL, weightVector[1], biasVector[1]);
    transformerBlock->addNorm2 = createAddNormalize(pre_seq_len, D_MODEL, weightVector[2], biasVector[2]);
    transformerBlock->token = &global_token_embedding;
    createTokenPosEmbedding(transformerBlock->token, posMatrix, clsTokenVector, pre_seq_len, input_dim, D_SEQ + 1);

    for (int l = 0; l < 4; l++)
    {
        transformerBlock->transformer_layer_0_addNorm[l] = createAddNormalize((pre_seq_len + 1), D_MODEL, weightVector[l * 17 + 3], biasVector[l * 17 + 3]);

        for (int n = 0; n < num_heads; n++)
        {
            transformerBlock->selfatten[l * num_heads + n] = &global_selfatten[l * num_heads + n];
            transformerBlock->selfatten[l * num_heads + n]->query_layer = &global_query_layer[l * num_heads + n];
            transformerBlock->selfatten[l * num_heads + n]->key_layer = &global_key_layer[l * num_heads + n];
            transformerBlock->selfatten[l * num_heads + n]->value_layer = &global_value_layer[l * num_heads + n];

            create_SingleHeadSelfAttn(transformerBlock->selfatten[l * num_heads + n], (pre_seq_len + 1), input_dim, head_hidden_size, weightVector + l * 17 + 4 + n * 3);
        }

        transformerBlock->condense[l] = &global_condense[l];
        createDense(transformerBlock->condense[l], num_heads * head_hidden_size, input_dim, weightVector[l * 17 + num_heads * 3 + 4], biasVector[l * 17 + num_heads * 3 + 4]);

        transformerBlock->transformer_layer_1_addNorm[l] = createAddNormalize((pre_seq_len + 1), input_dim, weightVector[l * 17 + num_heads * 3 + 5], biasVector[l * 17 + num_heads * 3 + 5]);

        transformerBlock->feedForward0[l] = &global_FF[2 * l];
        createDense(transformerBlock->feedForward0[l], input_dim, ff_size, weightVector[l * 17 + num_heads * 3 + 6], biasVector[l * 17 + num_heads * 3 + 6]);

        transformerBlock->feedForward1[l] = &global_FF[2 * l + 1];
        createDense(transformerBlock->feedForward1[l], ff_size, input_dim, weightVector[l * 17 + num_heads * 3 + 7], biasVector[l * 17 + num_heads * 3 + 7]);
    }

    transformerBlock->mlp_head_norm = createAddNormalize(1, D_MODEL, weightVector[(NUM_LAYERS - 1) * 17 + NUM_HEAD * 3 + 8], biasVector[(NUM_LAYERS - 1) * 17 + NUM_HEAD * 3 + 8]);

    transformerBlock->mlp_head_linear = &global_mlp;
    createDense(transformerBlock->mlp_head_linear, D_MODEL, D_MODEL, weightVector[(NUM_LAYERS - 1) * 17 + NUM_HEAD * 3 + 9], biasVector[(NUM_LAYERS - 1) * 17 + NUM_HEAD * 3 + 9]);

    return transformerBlock;
}

void destroyTransformerBlock(TransformerBlock *transformerBlock)
{
    // Free dynamically allocated memory

    // free(transformerBlock);
}

void computeFixedPoint(TransformerBlock *transformerBlock, size_t seq_len, quant_bit_width *input,
                       quant_bit_width *input_normalized, quant_bit_width *output,
                       quant_bit_width *intermediate, quant_bit_width *qkv)
{
    printf("computeFixedPoint sizes:\n");
    printf("  seq_len=%u, input_dim=%u, head_hidden=%u, num_heads=%u, ff_size=%u\n",
           seq_len, transformerBlock->input_dim_, transformerBlock->head_hidden_size_,
           transformerBlock->num_heads_, transformerBlock->ff_size_);
    printf("  input buffer=%u, output buffer=%u, intermediate buffer=%u, qkv buffer=%u\n",
           seq_len * transformerBlock->input_dim_, seq_len * transformerBlock->input_dim_,
           seq_len * transformerBlock->input_dim_, seq_len * transformerBlock->input_dim_);
    normalize(&transformerBlock->addNorm, input, input);
    printf("  addNorm0: input/output=%u x %u\n", seq_len, transformerBlock->input_dim_);
    printf("    weights=%u x %u, bias=%u x %u\n",
           (size_t)1, transformerBlock->addNorm.input_dim_,
           (size_t)1, transformerBlock->addNorm.input_dim_);

    computeDense(transformerBlock->patchEmbedding, seq_len, input, output);
    printf("  patchEmbedding (Dense): input=%u x %u, weight=%u x %u, output=%u x %u\n",
           seq_len, transformerBlock->patchEmbedding->input_size_,
           transformerBlock->patchEmbedding->input_size_, transformerBlock->patchEmbedding->output_size_,
           seq_len, transformerBlock->patchEmbedding->output_size_);
    printf("    bias=%u x %u\n", (size_t)1, transformerBlock->patchEmbedding->output_size_);

    normalize(&transformerBlock->addNorm2, output, output);
    printf("  addNorm1: input/output=%u x %u\n", seq_len, transformerBlock->addNorm2.input_dim_);
    printf("    weights=%u x %u, bias=%u x %u\n",
           (size_t)1, transformerBlock->addNorm2.input_dim_,
           (size_t)1, transformerBlock->addNorm2.input_dim_);

    clsConcatenate(transformerBlock->token, output, input);
    printf("  clsConcatenate: input seq_len=%u -> seq_len=%u, embed_dim=%u\n",
           seq_len, seq_len + 1, transformerBlock->input_dim_);
    printf("    weights/bias: n/a\n");

    seq_len++;
    posEmbedding(transformerBlock->token, input);
    printf("  posEmbedding: seq_len=%u, embed_dim=%u\n", seq_len, transformerBlock->input_dim_);
    printf("    weights/bias: n/a\n");

    for (int l = 0; l < 4; l++)
    {
        normalize(&transformerBlock->transformer_layer_0_addNorm[l], input, input_normalized);
        printf("  layer %d addNorm0: input/output=%u x %u\n", l, seq_len, transformerBlock->input_dim_);
        printf("    weights=%u x %u, bias=%u x %u\n",
               (size_t)1, transformerBlock->transformer_layer_0_addNorm[l].input_dim_,
               (size_t)1, transformerBlock->transformer_layer_0_addNorm[l].input_dim_);
        for (int n = 0; n < NUM_HEAD; n++)
        {
            compute_SingleHeadSelfAttn(transformerBlock->selfatten[l * NUM_HEAD + n], input_normalized,
                                       output + n * (seq_len * transformerBlock->head_hidden_size_), qkv, intermediate);
            printf("    layer %d head %d self-attn:\n", l, n);
            printf("      input=%u x %u, Q/K/V weight=%u x %u, output=%u x %u\n",
                   seq_len, transformerBlock->input_dim_,
                   transformerBlock->input_dim_, transformerBlock->head_hidden_size_,
                   seq_len, transformerBlock->head_hidden_size_);
            printf("      Q/K/V bias=%u x %u\n", (size_t)1, transformerBlock->head_hidden_size_);
            // destroy_SingleHeadSelfAttn(transformerBlock->selfatten[l * NUM_HEAD + n]);
        }

        multihead_transpose(output, intermediate, seq_len, transformerBlock->head_hidden_size_, transformerBlock->num_heads_);
        printf("  layer %d multihead_transpose: input=%u x %u x %u -> output=%u x %u\n",
               l, transformerBlock->num_heads_, seq_len, transformerBlock->head_hidden_size_,
               seq_len, transformerBlock->num_heads_ * transformerBlock->head_hidden_size_);
        printf("    weights/bias: n/a\n");

        computeDense(transformerBlock->condense[l], seq_len, intermediate, output);
        printf("  layer %d condense (Dense): input=%u x %u, weight=%u x %u, output=%u x %u\n",
               l, seq_len, transformerBlock->condense[l]->input_size_,
               transformerBlock->condense[l]->input_size_, transformerBlock->condense[l]->output_size_,
               seq_len, transformerBlock->condense[l]->output_size_);
        printf("    bias=%u x %u\n", (size_t)1, transformerBlock->condense[l]->output_size_);

        add(input, output, seq_len, transformerBlock->input_dim_);
        printf("  layer %d add0: input/output=%u x %u\n", l, seq_len, transformerBlock->input_dim_);
        printf("    weights/bias: n/a\n");

        normalize(&transformerBlock->transformer_layer_1_addNorm[l], input, input_normalized);
        printf("  layer %d addNorm1: input/output=%u x %u\n", l, seq_len, transformerBlock->input_dim_);
        printf("    weights=%u x %u, bias=%u x %u\n",
               (size_t)1, transformerBlock->transformer_layer_1_addNorm[l].input_dim_,
               (size_t)1, transformerBlock->transformer_layer_1_addNorm[l].input_dim_);

        computeDense(transformerBlock->feedForward0[l], seq_len, input_normalized, intermediate);
        printf("  layer %d FF0 (Dense): input=%u x %u, weight=%u x %u, output=%u x %u\n",
               l, seq_len, transformerBlock->feedForward0[l]->input_size_,
               transformerBlock->feedForward0[l]->input_size_, transformerBlock->feedForward0[l]->output_size_,
               seq_len, transformerBlock->feedForward0[l]->output_size_);
        printf("    bias=%u x %u\n", (size_t)1, transformerBlock->feedForward0[l]->output_size_);

        activation(transformerBlock->feedForward0[l], seq_len * transformerBlock->ff_size_, intermediate, intermediate);
        printf("  layer %d FF0 activation: length=%u\n", l, seq_len * transformerBlock->ff_size_);
        printf("    weights/bias: n/a\n");

        computeDense(transformerBlock->feedForward1[l], seq_len, intermediate, output);
        printf("  layer %d FF1 (Dense): input=%u x %u, weight=%u x %u, output=%u x %u\n",
               l, seq_len, transformerBlock->feedForward1[l]->input_size_,
               transformerBlock->feedForward1[l]->input_size_, transformerBlock->feedForward1[l]->output_size_,
               seq_len, transformerBlock->feedForward1[l]->output_size_);
        printf("    bias=%u x %u\n", (size_t)1, transformerBlock->feedForward1[l]->output_size_);

        add(input, output, seq_len, transformerBlock->input_dim_);
        printf("  layer %d add1: input/output=%u x %u\n", l, seq_len, transformerBlock->input_dim_);
        printf("    weights/bias: n/a\n");
    }

    normalize(&transformerBlock->mlp_head_norm, input, input_normalized);
    printf("  mlp_head_norm: input/output=%u x %u\n", seq_len, transformerBlock->input_dim_);
    printf("    weights=%u x %u, bias=%u x %u\n",
           (size_t)1, transformerBlock->mlp_head_norm.input_dim_,
           (size_t)1, transformerBlock->mlp_head_norm.input_dim_);

    computeDense(transformerBlock->mlp_head_linear, 1, input_normalized, output);
    printf("  mlp_head_linear (Dense): input=%d x %u, weight=%u x %u, output=%d x %u\n",
           1, transformerBlock->mlp_head_linear->input_size_,
           transformerBlock->mlp_head_linear->input_size_, transformerBlock->mlp_head_linear->output_size_,
           1, transformerBlock->mlp_head_linear->output_size_);
    printf("    bias=%u x %u\n", (size_t)1, transformerBlock->mlp_head_linear->output_size_);
}
