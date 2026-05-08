/**
 * @file main.c
 * @brief HQC Performance Profiling Framework
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

#ifndef HQC_PROFILE_REPS
#define HQC_PROFILE_REPS 3
#endif

#if HQC_PROFILE_REPS < 1
#error "HQC_PROFILE_REPS must be at least 1"
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
    uint64_t keygen_cycles;
    uint64_t encaps_cycles;
    uint64_t decaps_cycles;
    uint64_t keygen_keccak_cycles;
    uint64_t encaps_keccak_cycles;
    uint64_t decaps_keccak_cycles;
    uint64_t keygen_keccak_calls;
    uint64_t encaps_keccak_calls;
    uint64_t decaps_keccak_calls;
    uint64_t keygen_raw_dma_calls;
    uint64_t encaps_raw_dma_calls;
    uint64_t decaps_raw_dma_calls;
    uint64_t keygen_raw_dma_fallbacks;
    uint64_t encaps_raw_dma_fallbacks;
    uint64_t decaps_raw_dma_fallbacks;
    uint64_t keygen_raw_dma_hw_op_cycles;
    uint64_t encaps_raw_dma_hw_op_cycles;
    uint64_t decaps_raw_dma_hw_op_cycles;
    uint64_t keygen_raw_dma_hw_core_cycles;
    uint64_t encaps_raw_dma_hw_core_cycles;
    uint64_t decaps_raw_dma_hw_core_cycles;
    uint64_t keygen_sponge_dma_calls;
    uint64_t encaps_sponge_dma_calls;
    uint64_t decaps_sponge_dma_calls;
    uint64_t keygen_sponge_dma_fallbacks;
    uint64_t encaps_sponge_dma_fallbacks;
    uint64_t decaps_sponge_dma_fallbacks;
    uint64_t keygen_sponge_dma_cycles;
    uint64_t encaps_sponge_dma_cycles;
    uint64_t decaps_sponge_dma_cycles;
    uint64_t keygen_sponge_dma_hw_op_cycles;
    uint64_t encaps_sponge_dma_hw_op_cycles;
    uint64_t decaps_sponge_dma_hw_op_cycles;
    uint64_t keygen_sponge_dma_hw_core_cycles;
    uint64_t encaps_sponge_dma_hw_core_cycles;
    uint64_t decaps_sponge_dma_hw_core_cycles;
    uint64_t keygen_sponge_dma_hw_core_perms;
    uint64_t encaps_sponge_dma_hw_core_perms;
    uint64_t decaps_sponge_dma_hw_core_perms;
} perf_stats_t;

// Keep large KEM buffers in .bss instead of stack.
static uint8_t pk[HQC_CRYPTO_PUBLICKEYBYTES];
static uint8_t sk[HQC_CRYPTO_SECRETKEYBYTES];
static uint8_t ct[HQC_CRYPTO_CIPHERTEXTBYTES];
static uint8_t ss_encaps[HQC_CRYPTO_BYTES];
static uint8_t ss_decaps[HQC_CRYPTO_BYTES];

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
    printf("[*] %s Raw DMA    : %lu calls, %lu fallbacks, hw_op=%lu, hw_core=%lu\r\n",
           stage,
           (unsigned long)hqc_keccak_profile_get_dma_calls(),
           (unsigned long)hqc_keccak_profile_get_dma_fallbacks(),
           (unsigned long)((uint32_t)hqc_keccak_profile_get_dma_hw_op_cycles()),
           (unsigned long)((uint32_t)hqc_keccak_profile_get_dma_hw_core_cycles()));
    printf("[*] %s Sponge DMA : %lu calls, %lu fallbacks, sw=%lu, hw_op=%lu, hw_core=%lu, hw_perms=%lu, last_ret=%ld status=0x%08lx\r\n",
           stage,
           (unsigned long)hqc_sponge_dma_profile_get_calls(),
           (unsigned long)hqc_sponge_dma_profile_get_fallbacks(),
           (unsigned long)((uint32_t)hqc_sponge_dma_profile_get_cycles()),
           (unsigned long)((uint32_t)hqc_sponge_dma_profile_get_hw_op_cycles()),
           (unsigned long)((uint32_t)hqc_sponge_dma_profile_get_hw_core_cycles()),
           (unsigned long)hqc_sponge_dma_profile_get_hw_core_perms(),
           (long)hqc_sponge_dma_profile_get_last_ret(),
           (unsigned long)hqc_sponge_dma_profile_get_last_status());
}

#define ADD_FIELD(dst, src, field) ((dst)->field += (src)->field)

static void accumulate_stats(perf_stats_t *dst, const perf_stats_t *src) {
    ADD_FIELD(dst, src, keygen_cycles);
    ADD_FIELD(dst, src, encaps_cycles);
    ADD_FIELD(dst, src, decaps_cycles);
    ADD_FIELD(dst, src, keygen_keccak_cycles);
    ADD_FIELD(dst, src, encaps_keccak_cycles);
    ADD_FIELD(dst, src, decaps_keccak_cycles);
    ADD_FIELD(dst, src, keygen_keccak_calls);
    ADD_FIELD(dst, src, encaps_keccak_calls);
    ADD_FIELD(dst, src, decaps_keccak_calls);
    ADD_FIELD(dst, src, keygen_raw_dma_calls);
    ADD_FIELD(dst, src, encaps_raw_dma_calls);
    ADD_FIELD(dst, src, decaps_raw_dma_calls);
    ADD_FIELD(dst, src, keygen_raw_dma_fallbacks);
    ADD_FIELD(dst, src, encaps_raw_dma_fallbacks);
    ADD_FIELD(dst, src, decaps_raw_dma_fallbacks);
    ADD_FIELD(dst, src, keygen_raw_dma_hw_op_cycles);
    ADD_FIELD(dst, src, encaps_raw_dma_hw_op_cycles);
    ADD_FIELD(dst, src, decaps_raw_dma_hw_op_cycles);
    ADD_FIELD(dst, src, keygen_raw_dma_hw_core_cycles);
    ADD_FIELD(dst, src, encaps_raw_dma_hw_core_cycles);
    ADD_FIELD(dst, src, decaps_raw_dma_hw_core_cycles);
    ADD_FIELD(dst, src, keygen_sponge_dma_calls);
    ADD_FIELD(dst, src, encaps_sponge_dma_calls);
    ADD_FIELD(dst, src, decaps_sponge_dma_calls);
    ADD_FIELD(dst, src, keygen_sponge_dma_fallbacks);
    ADD_FIELD(dst, src, encaps_sponge_dma_fallbacks);
    ADD_FIELD(dst, src, decaps_sponge_dma_fallbacks);
    ADD_FIELD(dst, src, keygen_sponge_dma_cycles);
    ADD_FIELD(dst, src, encaps_sponge_dma_cycles);
    ADD_FIELD(dst, src, decaps_sponge_dma_cycles);
    ADD_FIELD(dst, src, keygen_sponge_dma_hw_op_cycles);
    ADD_FIELD(dst, src, encaps_sponge_dma_hw_op_cycles);
    ADD_FIELD(dst, src, decaps_sponge_dma_hw_op_cycles);
    ADD_FIELD(dst, src, keygen_sponge_dma_hw_core_cycles);
    ADD_FIELD(dst, src, encaps_sponge_dma_hw_core_cycles);
    ADD_FIELD(dst, src, decaps_sponge_dma_hw_core_cycles);
    ADD_FIELD(dst, src, keygen_sponge_dma_hw_core_perms);
    ADD_FIELD(dst, src, encaps_sponge_dma_hw_core_perms);
    ADD_FIELD(dst, src, decaps_sponge_dma_hw_core_perms);
}

#undef ADD_FIELD

static uint64_t scale_stat(uint64_t value, uint32_t divisor) {
    return (divisor == 0) ? value : (value / divisor);
}

static void print_u64_dec(uint64_t value) {
    char buf[21];
    int idx = (int)(sizeof(buf) - 1);
    buf[idx] = '\0';

    if (value == 0) {
        putchar('0');
        return;
    }

    while ((value != 0) && (idx > 0)) {
        idx--;
        buf[idx] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    printf("%s", &buf[idx]);
}

static void print_perf_summary(const char *title, const char *label,
                               const perf_stats_t *stats, uint32_t divisor) {
    const uint64_t total_cycles =
        stats->keygen_cycles + stats->encaps_cycles + stats->decaps_cycles;
    const uint64_t total_keccak_cycles =
        stats->keygen_keccak_cycles + stats->encaps_keccak_cycles + stats->decaps_keccak_cycles;
    const uint64_t total_keccak_calls =
        stats->keygen_keccak_calls + stats->encaps_keccak_calls + stats->decaps_keccak_calls;
    const uint64_t total_raw_dma_calls =
        stats->keygen_raw_dma_calls + stats->encaps_raw_dma_calls + stats->decaps_raw_dma_calls;
    const uint64_t total_raw_dma_fallbacks =
        stats->keygen_raw_dma_fallbacks + stats->encaps_raw_dma_fallbacks + stats->decaps_raw_dma_fallbacks;
    const uint64_t total_raw_dma_hw_op_cycles =
        stats->keygen_raw_dma_hw_op_cycles + stats->encaps_raw_dma_hw_op_cycles + stats->decaps_raw_dma_hw_op_cycles;
    const uint64_t total_raw_dma_hw_core_cycles =
        stats->keygen_raw_dma_hw_core_cycles + stats->encaps_raw_dma_hw_core_cycles + stats->decaps_raw_dma_hw_core_cycles;
    const uint64_t total_sponge_dma_calls =
        stats->keygen_sponge_dma_calls + stats->encaps_sponge_dma_calls + stats->decaps_sponge_dma_calls;
    const uint64_t total_sponge_dma_fallbacks =
        stats->keygen_sponge_dma_fallbacks + stats->encaps_sponge_dma_fallbacks + stats->decaps_sponge_dma_fallbacks;
    const uint64_t total_sponge_dma_cycles =
        stats->keygen_sponge_dma_cycles + stats->encaps_sponge_dma_cycles + stats->decaps_sponge_dma_cycles;
    const uint64_t total_sponge_dma_hw_op_cycles =
        stats->keygen_sponge_dma_hw_op_cycles + stats->encaps_sponge_dma_hw_op_cycles + stats->decaps_sponge_dma_hw_op_cycles;
    const uint64_t total_sponge_dma_hw_core_cycles =
        stats->keygen_sponge_dma_hw_core_cycles + stats->encaps_sponge_dma_hw_core_cycles + stats->decaps_sponge_dma_hw_core_cycles;
    const uint64_t total_sponge_dma_hw_core_perms =
        stats->keygen_sponge_dma_hw_core_perms + stats->encaps_sponge_dma_hw_core_perms + stats->decaps_sponge_dma_hw_core_perms;

    printf("\r\n--- %s ---\r\n", title);
    printf("%s KEM Operations Cycles: ", label);
    print_u64_dec(scale_stat(total_cycles, divisor));
    printf("\r\n");
    printf("%s Keccak Cycles: ", label);
    print_u64_dec(scale_stat(total_keccak_cycles, divisor));
    printf("\r\n");
    printf("%s Keccak Permutations: ", label);
    print_u64_dec(scale_stat(total_keccak_calls, divisor));
    printf("\r\n");
    printf("%s Raw DMA Calls/Fallbacks: ", label);
    print_u64_dec(scale_stat(total_raw_dma_calls, divisor));
    printf("/");
    print_u64_dec(scale_stat(total_raw_dma_fallbacks, divisor));
    printf("\r\n");
    printf("%s Raw DMA HW Op/Core Cycles: ", label);
    print_u64_dec(scale_stat(total_raw_dma_hw_op_cycles, divisor));
    printf("/");
    print_u64_dec(scale_stat(total_raw_dma_hw_core_cycles, divisor));
    printf("\r\n");
    printf("%s Sponge DMA Calls/Fallbacks/SW Cycles: ", label);
    print_u64_dec(scale_stat(total_sponge_dma_calls, divisor));
    printf("/");
    print_u64_dec(scale_stat(total_sponge_dma_fallbacks, divisor));
    printf("/");
    print_u64_dec(scale_stat(total_sponge_dma_cycles, divisor));
    printf("\r\n");
    printf("%s Sponge DMA HW Op/Core Cycles/Perms: ", label);
    print_u64_dec(scale_stat(total_sponge_dma_hw_op_cycles, divisor));
    printf("/");
    print_u64_dec(scale_stat(total_sponge_dma_hw_core_cycles, divisor));
    printf("/");
    print_u64_dec(scale_stat(total_sponge_dma_hw_core_perms, divisor));
    printf("\r\n");
}

static int run_profile_iteration(uint32_t iter, uint32_t reps, perf_stats_t *stats) {
    uint64_t start, end;
    int rc;

    memset(stats, 0, sizeof(*stats));
    randombytes_seedrng();

    printf("\r\n--- Iteration %lu/%lu ---\r\n",
           (unsigned long)iter,
           (unsigned long)reps);
    fflush(stdout);

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_KEYGEN_ONLY) {
        printf("[.] Starting KeyGen...\r\n");
        fflush(stdout);
        hqc_keccak_profile_reset();
        start = read_cycle64();
        rc = hqc_crypto_kem_keypair(pk, sk);
        end = read_cycle64();
        if (rc != 0) {
            printf("[!] KeyGen failed, rc=%d\r\n", rc);
            return 1;
        }
        stats->keygen_cycles = end - start;
        stats->keygen_keccak_cycles = hqc_keccak_profile_get_cycles();
        stats->keygen_keccak_calls = hqc_keccak_profile_get_calls();
        stats->keygen_raw_dma_calls = hqc_keccak_profile_get_dma_calls();
        stats->keygen_raw_dma_fallbacks = hqc_keccak_profile_get_dma_fallbacks();
        stats->keygen_raw_dma_hw_op_cycles = hqc_keccak_profile_get_dma_hw_op_cycles();
        stats->keygen_raw_dma_hw_core_cycles = hqc_keccak_profile_get_dma_hw_core_cycles();
        stats->keygen_sponge_dma_calls = hqc_sponge_dma_profile_get_calls();
        stats->keygen_sponge_dma_fallbacks = hqc_sponge_dma_profile_get_fallbacks();
        stats->keygen_sponge_dma_cycles = hqc_sponge_dma_profile_get_cycles();
        stats->keygen_sponge_dma_hw_op_cycles = hqc_sponge_dma_profile_get_hw_op_cycles();
        stats->keygen_sponge_dma_hw_core_cycles = hqc_sponge_dma_profile_get_hw_core_cycles();
        stats->keygen_sponge_dma_hw_core_perms = hqc_sponge_dma_profile_get_hw_core_perms();
        printf("[*] KeyGen        : ");
        print_u64_dec(stats->keygen_cycles);
        printf(" cycles\r\n");
        print_stage_profile("KeyGen");
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ENCAP_ONLY || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        rc = hqc_crypto_kem_keypair(pk, sk);
        if (rc != 0) {
            printf("[!] Setup KeyGen failed, rc=%d\r\n", rc);
            return 1;
        }
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_ENCAP_ONLY) {
        printf("[.] Starting Encapsulation...\r\n");
        fflush(stdout);
        hqc_keccak_profile_reset();
        start = read_cycle64();
        rc = hqc_crypto_kem_enc(ct, ss_encaps, pk);
        end = read_cycle64();
        if (rc != 0) {
            printf("[!] Encapsulation failed, rc=%d\r\n", rc);
            return 1;
        }
        stats->encaps_cycles = end - start;
        stats->encaps_keccak_cycles = hqc_keccak_profile_get_cycles();
        stats->encaps_keccak_calls = hqc_keccak_profile_get_calls();
        stats->encaps_raw_dma_calls = hqc_keccak_profile_get_dma_calls();
        stats->encaps_raw_dma_fallbacks = hqc_keccak_profile_get_dma_fallbacks();
        stats->encaps_raw_dma_hw_op_cycles = hqc_keccak_profile_get_dma_hw_op_cycles();
        stats->encaps_raw_dma_hw_core_cycles = hqc_keccak_profile_get_dma_hw_core_cycles();
        stats->encaps_sponge_dma_calls = hqc_sponge_dma_profile_get_calls();
        stats->encaps_sponge_dma_fallbacks = hqc_sponge_dma_profile_get_fallbacks();
        stats->encaps_sponge_dma_cycles = hqc_sponge_dma_profile_get_cycles();
        stats->encaps_sponge_dma_hw_op_cycles = hqc_sponge_dma_profile_get_hw_op_cycles();
        stats->encaps_sponge_dma_hw_core_cycles = hqc_sponge_dma_profile_get_hw_core_cycles();
        stats->encaps_sponge_dma_hw_core_perms = hqc_sponge_dma_profile_get_hw_core_perms();
        printf("[*] Encapsulation : ");
        print_u64_dec(stats->encaps_cycles);
        printf(" cycles\r\n");
        print_stage_profile("Encap");
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        rc = hqc_crypto_kem_enc(ct, ss_encaps, pk);
        if (rc != 0) {
            printf("[!] Setup Encapsulation failed, rc=%d\r\n", rc);
            return 1;
        }
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        printf("[.] Starting Decapsulation...\r\n");
        fflush(stdout);
        hqc_keccak_profile_reset();
        start = read_cycle64();
        rc = hqc_crypto_kem_dec(ss_decaps, ct, sk);
        end = read_cycle64();
        if (rc != 0) {
            printf("[!] Decapsulation failed, rc=%d\r\n", rc);
            return 1;
        }
        stats->decaps_cycles = end - start;
        stats->decaps_keccak_cycles = hqc_keccak_profile_get_cycles();
        stats->decaps_keccak_calls = hqc_keccak_profile_get_calls();
        stats->decaps_raw_dma_calls = hqc_keccak_profile_get_dma_calls();
        stats->decaps_raw_dma_fallbacks = hqc_keccak_profile_get_dma_fallbacks();
        stats->decaps_raw_dma_hw_op_cycles = hqc_keccak_profile_get_dma_hw_op_cycles();
        stats->decaps_raw_dma_hw_core_cycles = hqc_keccak_profile_get_dma_hw_core_cycles();
        stats->decaps_sponge_dma_calls = hqc_sponge_dma_profile_get_calls();
        stats->decaps_sponge_dma_fallbacks = hqc_sponge_dma_profile_get_fallbacks();
        stats->decaps_sponge_dma_cycles = hqc_sponge_dma_profile_get_cycles();
        stats->decaps_sponge_dma_hw_op_cycles = hqc_sponge_dma_profile_get_hw_op_cycles();
        stats->decaps_sponge_dma_hw_core_cycles = hqc_sponge_dma_profile_get_hw_core_cycles();
        stats->decaps_sponge_dma_hw_core_perms = hqc_sponge_dma_profile_get_hw_core_perms();
        printf("[*] Decapsulation : ");
        print_u64_dec(stats->decaps_cycles);
        printf(" cycles\r\n");
        print_stage_profile("Decap");
        fflush(stdout);
    }

    if (HQC_PROFILE_STAGE == HQC_STAGE_ALL || HQC_PROFILE_STAGE == HQC_STAGE_DECAP_ONLY) {
        printf("\r\n--- Correctness Check ---\r\n");
        if (compare_arrays(ss_encaps, ss_decaps, HQC_CRYPTO_BYTES)) {
            printf("Shared Secret Verification: PASS\r\n");
        } else {
            printf("Shared Secret Verification: FAIL\r\n");
            return 1;
        }
    }

    return 0;
}

int main(void) {
    printf("\r\n=== %s Profiling on X-HEEP ===\r\n\r\n", HQC_CRYPTO_ALGNAME);
    printf("[.] Profile mode: %s (HQC_PROFILE_STAGE=%d)\r\n", profile_stage_name(HQC_PROFILE_STAGE), HQC_PROFILE_STAGE);
    printf("[.] Profile reps: %d (HQC_PROFILE_REPS=%d)\r\n", HQC_PROFILE_REPS, HQC_PROFILE_REPS);
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

    enable_cycle_counter();

    perf_stats_t total_stats = {0};
    for (uint32_t iter = 1; iter <= HQC_PROFILE_REPS; iter++) {
        perf_stats_t iter_stats = {0};
        if (run_profile_iteration(iter, HQC_PROFILE_REPS, &iter_stats) != 0) {
            printf("\r\n>>> ERROR: Profiling iteration %lu failed! <<<\r\n",
                   (unsigned long)iter);
            return 1;
        }
        accumulate_stats(&total_stats, &iter_stats);
    }

    print_perf_summary("Average Performance Summary", "Average", &total_stats, HQC_PROFILE_REPS);
    print_perf_summary("Cumulative Performance Summary", "Cumulative", &total_stats, 1);
    printf("Total iterations: %lu\r\n", (unsigned long)HQC_PROFILE_REPS);

    printf("\r\n=== Profiling Complete ===\r\n\r\n");
    printf("\r\n>>> SUCCESS: All profiling iterations completed! <<<\r\n");

    return 0;
}
