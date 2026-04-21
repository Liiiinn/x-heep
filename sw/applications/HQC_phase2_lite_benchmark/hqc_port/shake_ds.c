#include "../../HQC/src/shake_ds.h"
#include "hqc_phase2_shake_backend.h"

#include <stddef.h>
#include "keccak_sponge_hqc128.h"
#include "keccak_sponge_hqc128_optimized.h"

static keccak_dma_t *g_keccak = NULL;
static uintptr_t g_state_addr = 0u;
static uintptr_t g_output_addr = 0u;
static hqc_phase2_shake_backend_t g_backend = HQC_PHASE2_SHAKE_BACKEND_P1;

void hqc_phase2_shake_backend_init(keccak_dma_t *keccak,
                                   uintptr_t state_addr,
                                   uintptr_t output_addr) {
    g_keccak = keccak;
    g_state_addr = state_addr;
    g_output_addr = output_addr;
    g_backend = HQC_PHASE2_SHAKE_BACKEND_P1;
}

void hqc_phase2_set_shake_backend(hqc_phase2_shake_backend_t backend) {
    if (backend == HQC_PHASE2_SHAKE_BACKEND_P1 || backend == HQC_PHASE2_SHAKE_BACKEND_P2) {
        g_backend = backend;
    }
}

hqc_phase2_shake_backend_t hqc_phase2_get_shake_backend(void) {
    return g_backend;
}

static void shake256_512_ds_fallback(shake256incctx *state,
                                     uint8_t *output,
                                     const uint8_t *input,
                                     size_t inlen,
                                     uint8_t domain) {
    shake256_inc_init(state);
    shake256_inc_absorb(state, input, inlen);
    shake256_inc_absorb(state, &domain, 1);
    shake256_inc_finalize(state);
    shake256_inc_squeeze(output, 512 / 8, state);
    shake256_inc_ctx_release(state);
}

void PQCLEAN_HQC128_CLEAN_shake256_512_ds(shake256incctx *state,
                                          uint8_t *output,
                                          const uint8_t *input,
                                          size_t inlen,
                                          uint8_t domain) {
    if (g_keccak == NULL ||
        ((g_state_addr | g_output_addr) & (KECCAK_DMA_ADDR_ALIGN_BYTES - 1u)) != 0u) {
        shake256_512_ds_fallback(state, output, input, inlen, domain);
        return;
    }

    keccak_dma_result_t ret;
    if (g_backend == HQC_PHASE2_SHAKE_BACKEND_P2) {
        keccak_sponge_opt_ctx_t ctx;
        ret = keccak_sponge_init_opt(&ctx, g_keccak, g_state_addr, g_output_addr);
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_absorb_opt(&ctx, input, inlen);
        }
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_finalize_opt(&ctx, domain);
        }
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_squeeze_opt(&ctx, output, 512 / 8);
        }
    } else {
        keccak_sponge_hqc128_ctx_t ctx;
        ret = keccak_sponge_init(&ctx, g_keccak, g_state_addr, g_output_addr);
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_absorb(&ctx, input, inlen);
        }
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_finalize(&ctx, domain);
        }
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_squeeze(&ctx, output, 512 / 8);
        }
    }

    if (ret != kKeccakDmaOk) {
        shake256_512_ds_fallback(state, output, input, inlen, domain);
    }
}
