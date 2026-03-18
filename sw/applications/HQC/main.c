/**
 * @file main.c
 * @brief HQC-128 Performance Profiling Framework
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "api.h"
#include "parameters.h"

// Profiling stage selection (set with -DHQC_PROFILE_STAGE=<value>).
// 0: all stages (default), 1: keygen only, 2: encaps only, 3: decaps only.
#ifndef HQC_PROFILE_STAGE
#define HQC_PROFILE_STAGE 0
#endif

#define HQC_STAGE_ALL         0
#define HQC_STAGE_KEYGEN_ONLY 1
#define HQC_STAGE_ENCAP_ONLY  2
#define HQC_STAGE_DECAP_ONLY  3


/**
 * @brief Read the mcycle counter
*/
static inline uint32_t read_cycle(void) {
    uint32_t cycle;
    __asm__ volatile ("csrr %0, mcycle" : "=r" (cycle));
    return cycle;
}

typedef struct {
    uint32_t keygen_cycles;
    uint32_t encaps_cycles;
    uint32_t decaps_cycles;
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

int main(void) {
    printf("\n=== HQC-128 Profiling on X-HEEP ===\n\n");
    printf("[.] Profile mode: %s (HQC_PROFILE_STAGE=%d)\n", profile_stage_name(HQC_PROFILE_STAGE), HQC_PROFILE_STAGE);
    fflush(stdout);

    perf_stats_t stats = {0};

    uint32_t start, end;
    int rc;

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_KEYGEN_ONLY) {
        // STEP 2: KeyGen profiling
        printf("[.] Starting KeyGen...\n");
        fflush(stdout);
        start = read_cycle();
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_keypair(pk, sk);
        end = read_cycle();
        if (rc != 0) {
            printf("[!] KeyGen failed, rc=%d\n", rc);
            return 1;
        }
        stats.keygen_cycles = end - start;
        printf("[*] KeyGen        : %lu cycles\n", (unsigned long)stats.keygen_cycles);
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ENCAP_ONLY || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        // Generate prerequisites without measuring them in single-stage modes.
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_keypair(pk, sk);
        if (rc != 0) {
            printf("[!] Setup KeyGen failed, rc=%d\n", rc);
            return 1;
        }
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_ENCAP_ONLY) {
        // STEP 3: Encapsulation profiling
        printf("[.] Starting Encapsulation...\n");
        fflush(stdout);
        start = read_cycle();
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_enc(ct, ss_encaps, pk);
        end = read_cycle();
        if (rc != 0) {
            printf("[!] Encapsulation failed, rc=%d\n", rc);
            return 1;
        }
        stats.encaps_cycles = end - start;
        printf("[*] Encapsulation : %lu cycles\n", (unsigned long)stats.encaps_cycles);
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        // Decapsulation requires a valid ciphertext/shared-secret context.
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_enc(ct, ss_encaps, pk);
        if (rc != 0) {
            printf("[!] Setup Encapsulation failed, rc=%d\n", rc);
            return 1;
        }
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        // STEP 4: Decapsulation profiling
        printf("[.] Starting Decapsulation...\n");
        fflush(stdout);
        start = read_cycle();
        rc = PQCLEAN_HQC128_CLEAN_crypto_kem_dec(ss_decaps, ct, sk);
        end = read_cycle();
        if (rc != 0) {
            printf("[!] Decapsulation failed, rc=%d\n", rc);
            return 1;
        }
        stats.decaps_cycles = end - start;
        printf("[*] Decapsulation : %lu cycles\n", (unsigned long)stats.decaps_cycles);
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        // STEP 5: Correctness check
        printf("\n--- Correctness Check ---\n");
        if (compare_arrays(ss_encaps, ss_decaps, PQCLEAN_HQC128_CLEAN_CRYPTO_BYTES)) {
            printf("Shared Secret Verification: PASS\n");
        } else {
            printf("Shared Secret Verification: FAIL\n");
        }
    }

    // STEP 6: Performance summary
    printf("\n--- Performance Summary ---\n");
    uint32_t total_cycles = stats.keygen_cycles + stats.encaps_cycles + stats.decaps_cycles;
    printf("Total KEM Operations Cycles: %lu\n", (unsigned long)total_cycles);

    printf("\n=== Profiling Complete ===\n\n");

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        if (memcmp(ss_encaps, ss_decaps, PQCLEAN_HQC128_CLEAN_CRYPTO_BYTES) == 0) {
            printf("\n>>> SUCCESS: Shared secrets perfectly match! <<<\n");
        } else {
            printf("\n>>> ERROR: Decapsulation failed! <<<\n");
        }
    }
    
    return 0;
}
