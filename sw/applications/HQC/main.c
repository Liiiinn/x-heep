/**
 * @file main.c
 * @brief HQC-128 Performance Profiling Framework
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "csr.h"
#include "csr_registers.h"
#include "api.h"
#include "parameters.h"
#include "fips202.h"
#include "hqc_keccak_backend.h"
#include "keccak_dma.h"
#include "keccak_sponge_dma.h"
#include "randombytes.h"

// Profiling stage selection (set with -DHQC_PROFILE_STAGE=<value>).
// 0: all stages (default), 1: keygen only, 2: encaps only, 3: decaps only.
#ifndef HQC_PROFILE_STAGE
#define HQC_PROFILE_STAGE 0
#endif

#ifndef HQC_USE_KECCAK_DMA
#define HQC_USE_KECCAK_DMA 1
#endif

#ifndef HQC_USE_KECCAK_SPONGE_DMA
#define HQC_USE_KECCAK_SPONGE_DMA 0
#endif

#ifndef HQC_KECCAK_DMA_PROBE_TIMEOUT_CYCLES
#define HQC_KECCAK_DMA_PROBE_TIMEOUT_CYCLES 1000000u
#endif

#define HQC_STAGE_ALL         0
#define HQC_STAGE_KEYGEN_ONLY 1
#define HQC_STAGE_ENCAP_ONLY  2
#define HQC_STAGE_DECAP_ONLY  3


/**
 * @brief Enable cycle counter (clear mcountinhibit.CY)
 */
static inline void enable_cycle_counter(void) {
    CSR_CLEAR_BITS(CSR_REG_MCOUNTINHIBIT, 0x1);
}

/**
 * @brief Read mcycle as 64-bit value with rollover-safe high/low sequence
 */
static inline uint64_t read_cycle64(void) {
    uint32_t hi0, lo, hi1;
    do {
        CSR_READ(CSR_REG_MCYCLEH, &hi0);
        CSR_READ(CSR_REG_MCYCLE, &lo);
        CSR_READ(CSR_REG_MCYCLEH, &hi1);
    } while (hi0 != hi1);
    return ((uint64_t)hi1 << 32) | lo;
}

typedef struct {
    uint32_t keygen_cycles;
    uint32_t encaps_cycles;
    uint32_t decaps_cycles;
    uint32_t keygen_keccak_cycles;
    uint32_t encaps_keccak_cycles;
    uint32_t decaps_keccak_cycles;
    uint32_t keygen_keccak_calls;
    uint32_t encaps_keccak_calls;
    uint32_t decaps_keccak_calls;
    uint32_t keygen_raw_dma_calls;
    uint32_t encaps_raw_dma_calls;
    uint32_t decaps_raw_dma_calls;
    uint32_t keygen_raw_dma_fallbacks;
    uint32_t encaps_raw_dma_fallbacks;
    uint32_t decaps_raw_dma_fallbacks;
    uint32_t keygen_sponge_dma_calls;
    uint32_t encaps_sponge_dma_calls;
    uint32_t decaps_sponge_dma_calls;
    uint32_t keygen_sponge_dma_fallbacks;
    uint32_t encaps_sponge_dma_fallbacks;
    uint32_t decaps_sponge_dma_fallbacks;
    uint32_t keygen_sponge_dma_cycles;
    uint32_t encaps_sponge_dma_cycles;
    uint32_t decaps_sponge_dma_cycles;
} perf_stats_t;

// Keep large KEM buffers in .bss instead of stack.
static uint8_t pk[PQCLEAN_HQC128_CLEAN_CRYPTO_PUBLICKEYBYTES];
static uint8_t sk[PQCLEAN_HQC128_CLEAN_CRYPTO_SECRETKEYBYTES];
static uint8_t ct[PQCLEAN_HQC128_CLEAN_CRYPTO_CIPHERTEXTBYTES];
static uint8_t ss_encaps[PQCLEAN_HQC128_CLEAN_CRYPTO_BYTES];
static uint8_t ss_decaps[PQCLEAN_HQC128_CLEAN_CRYPTO_BYTES];

// ============================================================================
// Utility functions
// ============================================================================
static int compare_arrays(const uint8_t *a, const uint8_t *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static const char *profile_stage_name(int stage) {
    switch (stage) {
        case HQC_STAGE_KEYGEN_ONLY: return "keygen-only";
        case HQC_STAGE_ENCAP_ONLY:  return "encaps-only";
        case HQC_STAGE_DECAP_ONLY:  return "decaps-only";
        default:                    return "all";
    }
}

static int raw_dma_probe(uint32_t *status_out) {
#if HQC_USE_KECCAK_DMA
    static uint32_t probe_state[KECCAK_DMA_BLOCK_BYTES / sizeof(uint32_t)]
        __attribute__((aligned(4)));
    static uint32_t probe_output[KECCAK_DMA_BLOCK_BYTES / sizeof(uint32_t)]
        __attribute__((aligned(4)));
    keccak_dma_t keccak;
    memset(probe_state, 0, sizeof(probe_state));
    memset(probe_output, 0, sizeof(probe_output));
    keccak_dma_init(&keccak, KECCAK_DMA_START_ADDRESS);
    const keccak_dma_result_t ret = keccak_dma_hash_block(
        &keccak, (uintptr_t)probe_state, (uintptr_t)probe_output,
        HQC_KECCAK_DMA_PROBE_TIMEOUT_CYCLES);
    if (status_out != NULL) {
        *status_out = keccak_dma_get_status(&keccak);
    }
    return (ret == kKeccakDmaOk) ? 0 : (int)ret;
#else
    if (status_out != NULL) {
        *status_out = 0;
    }
    return 0;
#endif
}

static void print_stage_profile(const char *stage) {
    printf("[*] %s Keccak     : %lu cycles (%lu perms)\r\n",
           stage,
           (unsigned long)((uint32_t)hqc_keccak_profile_get_cycles()),
           (unsigned long)hqc_keccak_profile_get_calls());
    printf("[*] %s Raw DMA    : %lu calls, %lu fallbacks\r\n",
           stage,
           (unsigned long)hqc_keccak_profile_get_dma_calls(),
           (unsigned long)hqc_keccak_profile_get_dma_fallbacks());
    printf("[*] %s Sponge DMA : %lu calls, %lu fallbacks, %lu cycles, last_ret=%ld status=0x%08lx\r\n",
           stage,
           (unsigned long)hqc_sponge_dma_profile_get_calls(),
           (unsigned long)hqc_sponge_dma_profile_get_fallbacks(),
           (unsigned long)((uint32_t)hqc_sponge_dma_profile_get_cycles()),
           (long)hqc_sponge_dma_profile_get_last_ret(),
           (unsigned long)hqc_sponge_dma_profile_get_last_status());
}

int main(void) {
    printf("\r\n=== HQC-128 Profiling on X-HEEP ===\r\n\r\n");
    printf("[.] Profile mode: %s (HQC_PROFILE_STAGE=%d)\r\n", profile_stage_name(HQC_PROFILE_STAGE), HQC_PROFILE_STAGE);
    printf("[.] Keccak backend: %s\r\n", hqc_keccak_backend_name());
    printf("[.] Raw DMA base:    0x%08lx (enabled=%d)\r\n",
           (unsigned long)KECCAK_DMA_START_ADDRESS, HQC_USE_KECCAK_DMA);
    printf("[.] Sponge DMA base: 0x%08lx (enabled=%d)\r\n",
           (unsigned long)KECCAK_SPONGE_DMA_START_ADDRESS, HQC_USE_KECCAK_SPONGE_DMA);

    uint32_t raw_status = 0;
    const int raw_probe = raw_dma_probe(&raw_status);
#if HQC_USE_KECCAK_DMA
    printf("[CHECK] raw DMA probe: %s (ret=%d status=0x%08lx)\r\n",
           (raw_probe == 0) ? "PASS" : "FAIL",
           raw_probe,
           (unsigned long)raw_status);
#else
    printf("[CHECK] raw DMA probe: SKIP (disabled)\r\n");
#endif
#if HQC_USE_KECCAK_SPONGE_DMA
    const int sponge_probe = hqc_sponge_dma_probe();
    printf("[CHECK] sponge DMA probe: %s (ret=%ld status=0x%08lx)\r\n",
           (sponge_probe == 0) ? "PASS" : "FAIL",
           (long)hqc_sponge_dma_profile_get_last_ret(),
           (unsigned long)hqc_sponge_dma_profile_get_last_status());
#else
    printf("[CHECK] sponge DMA probe: SKIP (disabled)\r\n");
#endif
    fflush(stdout);

    perf_stats_t stats = {0};

    uint64_t start, end;
    int rc;

    enable_cycle_counter();
    randombytes_seedrng();

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_KEYGEN_ONLY) {
        // STEP 2: KeyGen profiling
        printf("[.] Starting KeyGen...\r\n");
        fflush(stdout);
        hqc_keccak_profile_reset();
        start = read_cycle64();
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_keypair(pk, sk);
        end = read_cycle64();
        if (rc != 0) {
            printf("[!] KeyGen failed, rc=%d\r\n", rc);
            return 1;
        }
        stats.keygen_cycles = (uint32_t)(end - start);
        stats.keygen_keccak_cycles = (uint32_t)hqc_keccak_profile_get_cycles();
        stats.keygen_keccak_calls = hqc_keccak_profile_get_calls();
        stats.keygen_raw_dma_calls = hqc_keccak_profile_get_dma_calls();
        stats.keygen_raw_dma_fallbacks = hqc_keccak_profile_get_dma_fallbacks();
        stats.keygen_sponge_dma_calls = hqc_sponge_dma_profile_get_calls();
        stats.keygen_sponge_dma_fallbacks = hqc_sponge_dma_profile_get_fallbacks();
        stats.keygen_sponge_dma_cycles = (uint32_t)hqc_sponge_dma_profile_get_cycles();
        printf("[*] KeyGen        : %lu cycles\r\n", (unsigned long)stats.keygen_cycles);
        print_stage_profile("KeyGen");
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ENCAP_ONLY || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        // Generate prerequisites without measuring them in single-stage modes.
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_keypair(pk, sk);
        if (rc != 0) {
            printf("[!] Setup KeyGen failed, rc=%d\r\n", rc);
            return 1;
        }
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_ENCAP_ONLY) {
        // STEP 3: Encapsulation profiling
        printf("[.] Starting Encapsulation...\r\n");
        fflush(stdout);
        hqc_keccak_profile_reset();
        start = read_cycle64();
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_enc(ct, ss_encaps, pk);
        end = read_cycle64();
        if (rc != 0) {
            printf("[!] Encapsulation failed, rc=%d\r\n", rc);
            return 1;
        }
        stats.encaps_cycles = (uint32_t)(end - start);
        stats.encaps_keccak_cycles = (uint32_t)hqc_keccak_profile_get_cycles();
        stats.encaps_keccak_calls = hqc_keccak_profile_get_calls();
        stats.encaps_raw_dma_calls = hqc_keccak_profile_get_dma_calls();
        stats.encaps_raw_dma_fallbacks = hqc_keccak_profile_get_dma_fallbacks();
        stats.encaps_sponge_dma_calls = hqc_sponge_dma_profile_get_calls();
        stats.encaps_sponge_dma_fallbacks = hqc_sponge_dma_profile_get_fallbacks();
        stats.encaps_sponge_dma_cycles = (uint32_t)hqc_sponge_dma_profile_get_cycles();
        printf("[*] Encapsulation : %lu cycles\r\n", (unsigned long)stats.encaps_cycles);
        print_stage_profile("Encap");
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        // Decapsulation requires a valid ciphertext/shared-secret context.
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_enc(ct, ss_encaps, pk);
        if (rc != 0) {
            printf("[!] Setup Encapsulation failed, rc=%d\r\n", rc);
            return 1;
        }
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        // STEP 4: Decapsulation profiling
        printf("[.] Starting Decapsulation...\r\n");
        fflush(stdout);
        hqc_keccak_profile_reset();
        start = read_cycle64();
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_dec(ss_decaps, ct, sk);
        end = read_cycle64();
        if (rc != 0) {
            printf("[!] Decapsulation failed, rc=%d\r\n", rc);
            return 1;
        }
        stats.decaps_cycles = (uint32_t)(end - start);
        stats.decaps_keccak_cycles = (uint32_t)hqc_keccak_profile_get_cycles();
        stats.decaps_keccak_calls = hqc_keccak_profile_get_calls();
        stats.decaps_raw_dma_calls = hqc_keccak_profile_get_dma_calls();
        stats.decaps_raw_dma_fallbacks = hqc_keccak_profile_get_dma_fallbacks();
        stats.decaps_sponge_dma_calls = hqc_sponge_dma_profile_get_calls();
        stats.decaps_sponge_dma_fallbacks = hqc_sponge_dma_profile_get_fallbacks();
        stats.decaps_sponge_dma_cycles = (uint32_t)hqc_sponge_dma_profile_get_cycles();
        printf("[*] Decapsulation : %lu cycles\r\n", (unsigned long)stats.decaps_cycles);
        print_stage_profile("Decap");
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        // STEP 5: Correctness check
        printf("\r\n--- Correctness Check ---\r\n");
        if (compare_arrays(ss_encaps, ss_decaps, PQCLEAN_HQC128_CLEAN_CRYPTO_BYTES)) {
            printf("Shared Secret Verification: PASS\r\n");
        } else {
            printf("Shared Secret Verification: FAIL\r\n");
        }
    }

    // STEP 6: Performance summary
    printf("\r\n--- Performance Summary ---\r\n");
    uint32_t total_cycles = stats.keygen_cycles + stats.encaps_cycles + stats.decaps_cycles;
    uint32_t total_keccak_cycles = stats.keygen_keccak_cycles + stats.encaps_keccak_cycles + stats.decaps_keccak_cycles;
    uint32_t total_keccak_calls = stats.keygen_keccak_calls + stats.encaps_keccak_calls + stats.decaps_keccak_calls;
    uint32_t total_raw_dma_calls = stats.keygen_raw_dma_calls + stats.encaps_raw_dma_calls + stats.decaps_raw_dma_calls;
    uint32_t total_raw_dma_fallbacks = stats.keygen_raw_dma_fallbacks + stats.encaps_raw_dma_fallbacks + stats.decaps_raw_dma_fallbacks;
    uint32_t total_sponge_dma_calls = stats.keygen_sponge_dma_calls + stats.encaps_sponge_dma_calls + stats.decaps_sponge_dma_calls;
    uint32_t total_sponge_dma_fallbacks = stats.keygen_sponge_dma_fallbacks + stats.encaps_sponge_dma_fallbacks + stats.decaps_sponge_dma_fallbacks;
    uint32_t total_sponge_dma_cycles = stats.keygen_sponge_dma_cycles + stats.encaps_sponge_dma_cycles + stats.decaps_sponge_dma_cycles;
    printf("Total KEM Operations Cycles: %lu\r\n", (unsigned long)total_cycles);
    printf("Total Keccak Cycles: %lu\r\n", (unsigned long)total_keccak_cycles);
    printf("Total Keccak Permutations: %lu\r\n", (unsigned long)total_keccak_calls);
    printf("Total Raw DMA Calls/Fallbacks: %lu/%lu\r\n",
           (unsigned long)total_raw_dma_calls,
           (unsigned long)total_raw_dma_fallbacks);
    printf("Total Sponge DMA Calls/Fallbacks/Cycles: %lu/%lu/%lu\r\n",
           (unsigned long)total_sponge_dma_calls,
           (unsigned long)total_sponge_dma_fallbacks,
           (unsigned long)total_sponge_dma_cycles);

    printf("\r\n=== Profiling Complete ===\r\n\r\n");

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        if (memcmp(ss_encaps, ss_decaps, PQCLEAN_HQC128_CLEAN_CRYPTO_BYTES) == 0) {
            printf("\r\n>>> SUCCESS: Shared secrets perfectly match! <<<\r\n");
        } else {
            printf("\r\n>>> ERROR: Decapsulation failed! <<<\r\n");
        }
    }
    
    return 0;
}
