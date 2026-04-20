#include "keccak_sponge_hqc128.h"
#include "keccak_dma.h"
#include <string.h>

/**
 * @file keccak_sponge_hqc128.c
 * @brief Software Sponge layer for SHAKE256 with hardware Keccak-f[1600] acceleration
 *
 * Implements FIPS 202 sponge construction:
 * - Absorb phase: XOR input blocks with rate portion, apply f[1600]
 * - Finalize: Apply padding and final permutation
 * - Squeeze phase: Extract rate bytes per permutation
 *
 * HQC-128 specific:
 * - Rate: 136 bytes (SHAKE256)
 * - Capacity: 64 bytes
 * - Padding: 0x1F (FIPS 202 multi-rate padding: 0x04 | 0x00...0x00 | 0x80)
 * - Domain separation: 1 byte appended before finalize
 */

keccak_dma_result_t keccak_sponge_init(
    keccak_sponge_hqc128_ctx_t *ctx,
    keccak_dma_t *keccak,
    uintptr_t state_addr,
    uintptr_t output_addr) {
    
    if (ctx == NULL || keccak == NULL) {
        return kKeccakDmaBadLen;
    }
    
    ctx->keccak = keccak;
    ctx->state_addr = state_addr;
    ctx->output_addr = output_addr;
    ctx->block_count = 0;
    ctx->buffer_len = 0;
    ctx->finalized = false;
    
    // Initialize state to all zeros
    memset(ctx->state, 0, sizeof(ctx->state));
    
    return kKeccakDmaOk;
}

keccak_dma_result_t keccak_sponge_absorb(
    keccak_sponge_hqc128_ctx_t *ctx,
    const uint8_t *input,
    size_t inlen) {
    
    if (ctx == NULL || input == NULL || ctx->finalized) {
        return kKeccakDmaBadLen;
    }
    
    size_t input_offset = 0;
    
    // Process buffered data if any
    while (input_offset < inlen) {
        size_t space_in_buffer = SHAKE256_RATE - ctx->buffer_len;
        size_t to_copy = (inlen - input_offset < space_in_buffer) ?
                         (inlen - input_offset) : space_in_buffer;
        
        // Copy to buffer
        memcpy(&ctx->rate_buffer[ctx->buffer_len], 
               &input[input_offset], 
               to_copy);
        ctx->buffer_len += to_copy;
        input_offset += to_copy;
        
        // If buffer is full, absorb it
        if (ctx->buffer_len == SHAKE256_RATE) {
            // XOR rate buffer with state[0:136]
            for (size_t i = 0; i < SHAKE256_RATE; i++) {
                ctx->state[i] ^= ctx->rate_buffer[i];
            }
            
            // Apply Keccak-f[1600] via hardware DMA
            // Write state to DMA input
            memcpy((void *)ctx->state_addr, ctx->state, 200);
            
            keccak_dma_result_t ret = keccak_dma_hash_block(
                ctx->keccak,
                ctx->state_addr,
                ctx->output_addr,
                1000000  // timeout
            );
            
            if (ret != kKeccakDmaOk) {
                return ret;
            }
            
            // Read result from DMA output
            memcpy(ctx->state, (void *)ctx->output_addr, 200);
            
            ctx->block_count++;
            ctx->buffer_len = 0;
        }
    }
    
    return kKeccakDmaOk;
}

keccak_dma_result_t keccak_sponge_finalize(
    keccak_sponge_hqc128_ctx_t *ctx,
    uint8_t domain) {
    
    if (ctx == NULL || ctx->finalized) {
        return kKeccakDmaBadLen;
    }
    
    // Append domain separation byte to buffer
    ctx->rate_buffer[ctx->buffer_len] = domain;
    ctx->buffer_len++;
    
    // Apply padding: 0x04 at start position (already handled by buffer[buffer_len]),
    // 0x80 at end position
    // For incremental API, we apply padding as:
    // - Domain byte already at current position
    // - Padding continuation: XOR with 0x80 at position (rate-1)
    
    // Fill remaining buffer with zeros until end
    memset(&ctx->rate_buffer[ctx->buffer_len], 0, 
           SHAKE256_RATE - ctx->buffer_len);
    
    // Set final byte (padding end indicator)
    ctx->rate_buffer[SHAKE256_RATE - 1] |= 0x80;
    
    // Set first byte of rate (padding start indicator, if buffer was at start)
    // This is implicitly 0x04 from the domain byte
    ctx->rate_buffer[0] |= 0x04;
    
    // XOR final padded block with state
    for (size_t i = 0; i < SHAKE256_RATE; i++) {
        ctx->state[i] ^= ctx->rate_buffer[i];
    }
    
    // Apply final Keccak-f[1600]
    memcpy((void *)ctx->state_addr, ctx->state, 200);
    
    keccak_dma_result_t ret = keccak_dma_hash_block(
        ctx->keccak,
        ctx->state_addr,
        ctx->output_addr,
        1000000
    );
    
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    // Read result
    memcpy(ctx->state, (void *)ctx->output_addr, 200);
    
    ctx->block_count = 0;
    ctx->buffer_len = 0;
    ctx->finalized = true;
    
    return kKeccakDmaOk;
}

keccak_dma_result_t keccak_sponge_squeeze(
    keccak_sponge_hqc128_ctx_t *ctx,
    uint8_t *output,
    size_t outlen) {
    
    if (ctx == NULL || output == NULL || !ctx->finalized) {
        return kKeccakDmaBadLen;
    }
    
    size_t output_offset = 0;
    
    while (output_offset < outlen) {
        size_t to_squeeze = (outlen - output_offset < SHAKE256_RATE) ?
                           (outlen - output_offset) : SHAKE256_RATE;
        
        // Copy rate bytes from state
        memcpy(&output[output_offset], ctx->state, to_squeeze);
        output_offset += to_squeeze;
        
        // If more output needed, apply another permutation
        if (output_offset < outlen) {
            memcpy((void *)ctx->state_addr, ctx->state, 200);
            
            keccak_dma_result_t ret = keccak_dma_hash_block(
                ctx->keccak,
                ctx->state_addr,
                ctx->output_addr,
                1000000
            );
            
            if (ret != kKeccakDmaOk) {
                return ret;
            }
            
            memcpy(ctx->state, (void *)ctx->output_addr, 200);
        }
    }
    
    return kKeccakDmaOk;
}

keccak_dma_result_t keccak_sponge_shake256_hqc128_g(
    keccak_dma_t *keccak,
    uint8_t *output,
    const uint8_t *input,
    size_t inlen,
    uintptr_t state_addr,
    uintptr_t output_addr) {
    
    keccak_sponge_hqc128_ctx_t ctx;
    
    keccak_dma_result_t ret = keccak_sponge_init(&ctx, keccak, state_addr, output_addr);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    ret = keccak_sponge_absorb(&ctx, input, inlen);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    ret = keccak_sponge_finalize(&ctx, HQC128_G_FCT_DOMAIN);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    ret = keccak_sponge_squeeze(&ctx, output, 64);
    
    return ret;
}

keccak_dma_result_t keccak_sponge_shake256_hqc128_k(
    keccak_dma_t *keccak,
    uint8_t *output,
    const uint8_t *input,
    size_t inlen,
    uintptr_t state_addr,
    uintptr_t output_addr) {
    
    keccak_sponge_hqc128_ctx_t ctx;
    
    keccak_dma_result_t ret = keccak_sponge_init(&ctx, keccak, state_addr, output_addr);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    ret = keccak_sponge_absorb(&ctx, input, inlen);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    ret = keccak_sponge_finalize(&ctx, HQC128_K_FCT_DOMAIN);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    ret = keccak_sponge_squeeze(&ctx, output, 64);
    
    return ret;
}
