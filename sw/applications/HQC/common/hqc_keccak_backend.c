#include "hqc_keccak_backend.h"

#include <string.h>

#include "domains.h"
#include "keccak_sponge_dma.h"
#include "parameters.h"

#define HQC_SPONGE_DMA_OUT_BYTES SHAKE256_512_BYTES
static uint8_t g_sponge_dma_output[HQC_SPONGE_DMA_OUT_BYTES]
    __attribute__((aligned(4)));
static uint8_t g_sponge_dma_probe_input[KECCAK_SPONGE_DMA_SHAKE256_RATE_BYTES]
    __attribute__((aligned(4)));
static keccak_sponge_dma_t g_sponge_dma;
static bool g_sponge_dma_initialized = false;
static bool g_raw_dma_suppressed = false;
static uint32_t g_sponge_dma_calls = 0;
static uint32_t g_sponge_dma_fallbacks = 0;
static uint64_t g_sponge_dma_cycles = 0;
static uint64_t g_sponge_dma_hw_op_cycles = 0;
static uint64_t g_sponge_dma_hw_core_cycles = 0;
static uint32_t g_sponge_dma_hw_core_perms = 0;
static int32_t g_sponge_dma_last_ret = 0;
static uint32_t g_sponge_dma_last_status = 0;

static inline uint64_t hqc_backend_read_cycle64(void) {
    uint32_t hi0, lo, hi1;
    do {
        __asm__ volatile ("csrr %0, mcycleh" : "=r" (hi0));
        __asm__ volatile ("csrr %0, mcycle" : "=r" (lo));
        __asm__ volatile ("csrr %0, mcycleh" : "=r" (hi1));
    } while (hi0 != hi1);
    return ((uint64_t)hi1 << 32) | lo;
}

static void hqc_sponge_dma_init_once(void) {
    if (!g_sponge_dma_initialized) {
        keccak_sponge_dma_init(&g_sponge_dma, KECCAK_SPONGE_DMA_START_ADDRESS);
        g_sponge_dma_initialized = true;
    }
}

const char *hqc_keccak_backend_name(void) {
#if HQC_USE_KECCAK_DMA && HQC_USE_KECCAK_SPONGE_DMA
    return "hybrid sponge-gk + raw-keccak-dma";
#elif HQC_USE_KECCAK_SPONGE_DMA
    return "sponge-gk dma + software keccak";
#elif HQC_USE_KECCAK_DMA
    return "raw DMA permutation";
#else
    return "software KeccakF1600";
#endif
}

void hqc_keccak_backend_profile_reset(void) {
    g_sponge_dma_calls = 0;
    g_sponge_dma_fallbacks = 0;
    g_sponge_dma_cycles = 0;
    g_sponge_dma_hw_op_cycles = 0;
    g_sponge_dma_hw_core_cycles = 0;
    g_sponge_dma_hw_core_perms = 0;
    g_sponge_dma_last_ret = 0;
    g_sponge_dma_last_status = 0;
    g_raw_dma_suppressed = false;
}

void hqc_keccak_backend_set_raw_dma_suppressed(bool suppress) {
    g_raw_dma_suppressed = suppress;
}

bool hqc_keccak_backend_raw_dma_is_suppressed(void) {
    return g_raw_dma_suppressed;
}

int hqc_sponge_dma_probe(void) {
#if HQC_USE_KECCAK_SPONGE_DMA
    hqc_sponge_dma_init_once();
    memset(g_sponge_dma_probe_input, 0, sizeof(g_sponge_dma_probe_input));
    memset(g_sponge_dma_output, 0, HQC_SPONGE_DMA_OUT_BYTES);
    const keccak_sponge_dma_result_t ret = keccak_sponge_dma_shake256(
        &g_sponge_dma,
        (uintptr_t)g_sponge_dma_probe_input,
        sizeof(g_sponge_dma_probe_input),
        (uintptr_t)g_sponge_dma_output,
        HQC_SPONGE_DMA_OUT_BYTES,
        G_FCT_DOMAIN,
        KECCAK_SPONGE_DMA_DEFAULT_TIMEOUT_CYCLES);
    g_sponge_dma_last_ret = (int32_t)ret;
    g_sponge_dma_last_status = keccak_sponge_dma_get_status(&g_sponge_dma);
    return (ret == kKeccakSpongeDmaOk) ? 0 : -1;
#else
    return -1;
#endif
}

int hqc_sponge_dma_shake256_512(uint8_t *output,
                                const uint8_t *input,
                                size_t inlen,
                                uint8_t domain) {
#if HQC_USE_KECCAK_SPONGE_DMA
    if (output == NULL || input == NULL ||
        (domain != G_FCT_DOMAIN && domain != K_FCT_DOMAIN)) {
        g_sponge_dma_fallbacks++;
        g_sponge_dma_last_ret = (int32_t)kKeccakSpongeDmaBadLen;
        return -1;
    }
    if (((uintptr_t)input & (KECCAK_SPONGE_DMA_ADDR_ALIGN_BYTES - 1u)) != 0u) {
        g_sponge_dma_fallbacks++;
        g_sponge_dma_last_ret = (int32_t)kKeccakSpongeDmaBadAddr;
        return -1;
    }

    hqc_sponge_dma_init_once();
    uint8_t *dma_output = output;
    if (((uintptr_t)output & (KECCAK_SPONGE_DMA_ADDR_ALIGN_BYTES - 1u)) != 0u) {
        dma_output = g_sponge_dma_output;
    }

    const uint64_t start = hqc_backend_read_cycle64();
    const keccak_sponge_dma_result_t ret = keccak_sponge_dma_shake256(
        &g_sponge_dma,
        (uintptr_t)input,
        (uint32_t)inlen,
        (uintptr_t)dma_output,
        HQC_SPONGE_DMA_OUT_BYTES,
        domain,
        KECCAK_SPONGE_DMA_DEFAULT_TIMEOUT_CYCLES);
    g_sponge_dma_cycles += hqc_backend_read_cycle64() - start;
    g_sponge_dma_last_ret = (int32_t)ret;
    g_sponge_dma_last_status = keccak_sponge_dma_get_status(&g_sponge_dma);

    if (ret != kKeccakSpongeDmaOk) {
        g_sponge_dma_fallbacks++;
        return -1;
    }

    g_sponge_dma_hw_op_cycles += keccak_sponge_dma_get_last_op_cycles(&g_sponge_dma);
    g_sponge_dma_hw_core_cycles += keccak_sponge_dma_get_last_core_cycles(&g_sponge_dma);
    g_sponge_dma_hw_core_perms += keccak_sponge_dma_get_last_core_perms(&g_sponge_dma);

    if (dma_output != output) {
        memcpy(output, g_sponge_dma_output, HQC_SPONGE_DMA_OUT_BYTES);
    }
    g_sponge_dma_calls++;
    return 0;
#else
    (void)output;
    (void)input;
    (void)inlen;
    (void)domain;
    return -1;
#endif
}

uint32_t hqc_sponge_dma_profile_get_calls(void) {
    return g_sponge_dma_calls;
}

uint32_t hqc_sponge_dma_profile_get_fallbacks(void) {
    return g_sponge_dma_fallbacks;
}

uint64_t hqc_sponge_dma_profile_get_cycles(void) {
    return g_sponge_dma_cycles;
}

uint64_t hqc_sponge_dma_profile_get_hw_op_cycles(void) {
    return g_sponge_dma_hw_op_cycles;
}

uint64_t hqc_sponge_dma_profile_get_hw_core_cycles(void) {
    return g_sponge_dma_hw_core_cycles;
}

uint32_t hqc_sponge_dma_profile_get_hw_core_perms(void) {
    return g_sponge_dma_hw_core_perms;
}

int32_t hqc_sponge_dma_profile_get_last_ret(void) {
    return g_sponge_dma_last_ret;
}

uint32_t hqc_sponge_dma_profile_get_last_status(void) {
    return g_sponge_dma_last_status;
}
