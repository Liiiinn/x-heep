#ifndef _DRIVERS_KECCAK_SPONGE_HQC128_H_
#define _DRIVERS_KECCAK_SPONGE_HQC128_H_

#include <stdbool.h>
#include <stdint.h>
#include "keccak_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

// SHAKE256 constants for HQC-128
#define SHAKE256_RATE 136           // Rate block size in bytes
#define SHAKE256_CAPACITY 64        // Capacity = 1600 - rate*8 bits in bytes
#define SHAKE256_PADDING_BYTE 0x1F  // FIPS 202 multi-rate padding

// HQC-128 domain separation bytes
#define HQC128_G_FCT_DOMAIN 0x03    // G-function domain
#define HQC128_K_FCT_DOMAIN 0x04    // K-function domain

// Context for incremental SHAKE256 sponge
typedef struct {
    keccak_dma_t *keccak;
    uint8_t state[200];             // 1600-bit Keccak state (50×32-bit words)
    uint32_t state_addr;            // DMA memory address for state
    uint32_t output_addr;           // DMA memory address for output
    uint32_t block_count;           // Number of rate blocks absorbed
    uint8_t rate_buffer[SHAKE256_RATE];  // Partial rate block buffer
    uint32_t buffer_len;            // Bytes in buffer
    bool finalized;                 // Whether finalize() was called
} keccak_sponge_hqc128_ctx_t;

/**
 * @brief Initialize SHAKE256 sponge for HQC-128
 *
 * @param[out] ctx Context structure
 * @param[in] keccak Initialized Keccak DMA instance
 * @param[in] state_addr DMA accessible memory for Keccak state (200 bytes)
 * @param[in] output_addr DMA accessible memory for output (200 bytes)
 * @return kKeccakDmaOk on success
 */
keccak_dma_result_t keccak_sponge_init(
    keccak_sponge_hqc128_ctx_t *ctx,
    keccak_dma_t *keccak,
    uintptr_t state_addr,
    uintptr_t output_addr);

/**
 * @brief Absorb data into SHAKE256 sponge (incremental)
 *
 * Can be called multiple times to absorb data in chunks.
 *
 * @param[in,out] ctx Context structure
 * @param[in] input Data to absorb
 * @param[in] inlen Length of input in bytes
 * @return kKeccakDmaOk on success
 */
keccak_dma_result_t keccak_sponge_absorb(
    keccak_sponge_hqc128_ctx_t *ctx,
    const uint8_t *input,
    size_t inlen);

/**
 * @brief Absorb domain separation byte and finalize absorption
 *
 * After this call, sponge switches to squeeze mode.
 * Must be called before squeeze operations.
 *
 * @param[in,out] ctx Context structure
 * @param[in] domain Domain separation byte (e.g., HQC128_G_FCT_DOMAIN)
 * @return kKeccakDmaOk on success
 */
keccak_dma_result_t keccak_sponge_finalize(
    keccak_sponge_hqc128_ctx_t *ctx,
    uint8_t domain);

/**
 * @brief Squeeze output from SHAKE256 sponge
 *
 * Can be called multiple times to extract output in blocks.
 * Must be called after finalize().
 *
 * @param[in,out] ctx Context structure
 * @param[out] output Output buffer
 * @param[in] outlen Number of bytes to squeeze
 * @return kKeccakDmaOk on success
 */
keccak_dma_result_t keccak_sponge_squeeze(
    keccak_sponge_hqc128_ctx_t *ctx,
    uint8_t *output,
    size_t outlen);

/**
 * @brief HQC-128 G-function: SHAKE256(m || pk || salt, domain=3)
 *
 * Convenience wrapper for G-function in HQC-128 encapsulation.
 *
 * @param[in] keccak Initialized Keccak DMA instance
 * @param[out] output Output buffer (64 bytes)
 * @param[in] input Input data (m || pk || salt)
 * @param[in] inlen Total input length
 * @param[in] state_addr DMA accessible memory for Keccak state
 * @param[in] output_addr DMA accessible memory for output
 * @return kKeccakDmaOk on success
 */
keccak_dma_result_t keccak_sponge_shake256_hqc128_g(
    keccak_dma_t *keccak,
    uint8_t *output,
    const uint8_t *input,
    size_t inlen,
    uintptr_t state_addr,
    uintptr_t output_addr);

/**
 * @brief HQC-128 K-function: SHAKE256(m || u || v, domain=4)
 *
 * Convenience wrapper for K-function in HQC-128 encapsulation/decapsulation.
 *
 * @param[in] keccak Initialized Keccak DMA instance
 * @param[out] output Output buffer (64 bytes)
 * @param[in] input Input data (m || u || v)
 * @param[in] inlen Total input length
 * @param[in] state_addr DMA accessible memory for Keccak state
 * @param[in] output_addr DMA accessible memory for output
 * @return kKeccakDmaOk on success
 */
keccak_dma_result_t keccak_sponge_shake256_hqc128_k(
    keccak_dma_t *keccak,
    uint8_t *output,
    const uint8_t *input,
    size_t inlen,
    uintptr_t state_addr,
    uintptr_t output_addr);

#ifdef __cplusplus
}
#endif

#endif  // _DRIVERS_KECCAK_SPONGE_HQC128_H_
