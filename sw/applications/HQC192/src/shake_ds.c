#include "shake_ds.h"
#include "domains.h"
#include "hqc_keccak_backend.h"


/**
 * @file shake_ds.c
 * @brief Implementation SHAKE-256 with incremental API and domain separation
 */



/**
 * @brief SHAKE-256 with incremental API and domain separation
 *
 * Derived from function SHAKE_256 in fips202.c
 *
 * @param[out] state Internal state of SHAKE
 * @param[in] output Pointer to output
 * @param[in] input Pointer to input
 * @param[in] inlen length of input in bytes
 * @param[in] domain byte for domain separation
 */
static void shake256_512_ds_software(shake256incctx *state,
                                      uint8_t *output,
                                      const uint8_t *input,
                                      size_t inlen,
                                      uint8_t domain) {
    /* Init state */
    shake256_inc_init(state);

    /* Absorb input */
    shake256_inc_absorb(state, input, inlen);

    /* Absorb domain separation byte */
    shake256_inc_absorb(state, &domain, 1);

    /* Finalize */
    shake256_inc_finalize(state);

    /* Squeeze output */
    shake256_inc_squeeze(output, 512 / 8, state);

    /* Release ctx */
    shake256_inc_ctx_release(state);
}

void PQCLEAN_HQC192_CLEAN_shake256_512_ds(shake256incctx *state, uint8_t *output, const uint8_t *input, size_t inlen, uint8_t domain) {
#if HQC_USE_KECCAK_SPONGE_DMA
    if (domain == G_FCT_DOMAIN || domain == K_FCT_DOMAIN) {
        if (hqc_sponge_dma_shake256_512(output, input, inlen, domain) == 0) {
            return;
        }

        hqc_keccak_backend_set_raw_dma_suppressed(true);
        shake256_512_ds_software(state, output, input, inlen, domain);
        hqc_keccak_backend_set_raw_dma_suppressed(false);
        return;
    }
#endif

    shake256_512_ds_software(state, output, input, inlen, domain);
}
