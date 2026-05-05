#ifndef HQC_KECCAK_BACKEND_H
#define HQC_KECCAK_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__has_include)
#if __has_include("hqc_app_config.h")
#include "hqc_app_config.h"
#endif
#endif

#ifndef HQC_USE_KECCAK_DMA
#define HQC_USE_KECCAK_DMA 1
#endif

#ifndef HQC_USE_KECCAK_SPONGE_DMA
#define HQC_USE_KECCAK_SPONGE_DMA 0
#endif

const char *hqc_keccak_backend_name(void);

void hqc_keccak_backend_profile_reset(void);

void hqc_keccak_backend_set_raw_dma_suppressed(bool suppress);
bool hqc_keccak_backend_raw_dma_is_suppressed(void);

int hqc_sponge_dma_probe(void);
int hqc_sponge_dma_shake256_512(uint8_t *output,
                                const uint8_t *input,
                                size_t inlen,
                                uint8_t domain);

uint32_t hqc_sponge_dma_profile_get_calls(void);
uint32_t hqc_sponge_dma_profile_get_fallbacks(void);
uint64_t hqc_sponge_dma_profile_get_cycles(void);
int32_t hqc_sponge_dma_profile_get_last_ret(void);
uint32_t hqc_sponge_dma_profile_get_last_status(void);

#endif
