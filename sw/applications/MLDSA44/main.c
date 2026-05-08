#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "csr.h"
#include "csr_registers.h"
#include "fips202.h"
#include "keccak_dma.h"
#include "randombytes.h"

#if __has_include("pqclean_app_config.h")
#include "pqclean_app_config.h"
#endif

#ifndef PQCLEAN_PROFILE_REPS
#define PQCLEAN_PROFILE_REPS 3
#endif

#if PQCLEAN_PROFILE_REPS < 1
#error "PQCLEAN_PROFILE_REPS must be at least 1"
#endif

#ifndef PQCLEAN_USE_KECCAK_DMA
#define PQCLEAN_USE_KECCAK_DMA 1
#endif

#ifndef PQCLEAN_KECCAK_DMA_PROBE_TIMEOUT_CYCLES
#define PQCLEAN_KECCAK_DMA_PROBE_TIMEOUT_CYCLES 1000000u
#endif

typedef struct {
    uint64_t keygen_cycles;
    uint64_t sign_cycles;
    uint64_t verify_cycles;
    uint64_t keccak_cycles;
    uint64_t keccak_calls;
    uint64_t raw_dma_calls;
    uint64_t raw_dma_fallbacks;
    uint64_t raw_dma_hw_op_cycles;
    uint64_t raw_dma_hw_core_cycles;
} perf_stats_t;

static const uint8_t message[] = {
    'X', '-', 'H', 'E', 'E', 'P', ' ', 'P', 'Q', 'C', ' ', 'p', 'r', 'o', 'f', 'i', 'l', 'e'
};

static uint8_t pk[PQ_APP_PUBLICKEYBYTES];
static uint8_t sk[PQ_APP_SECRETKEYBYTES];
static uint8_t sig[PQ_APP_SIGNATUREBYTES];
static size_t siglen;

static inline void enable_cycle_counter(void) {
    CSR_CLEAR_BITS(CSR_REG_MCOUNTINHIBIT, 0x1);
}

static inline uint64_t read_cycle64(void) {
    uint32_t hi0, lo, hi1;
    do {
        CSR_READ(CSR_REG_MCYCLEH, &hi0);
        CSR_READ(CSR_REG_MCYCLE, &lo);
        CSR_READ(CSR_REG_MCYCLEH, &hi1);
    } while (hi0 != hi1);
    return ((uint64_t)hi1 << 32) | lo;
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

static uint64_t avg_u64(uint64_t value) {
    return value / (uint32_t)PQCLEAN_PROFILE_REPS;
}

static int raw_dma_probe(uint32_t *status_out) {
#if PQCLEAN_USE_KECCAK_DMA
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
        PQCLEAN_KECCAK_DMA_PROBE_TIMEOUT_CYCLES);
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

static void capture_keccak_stats(perf_stats_t *stats) {
    stats->keccak_cycles += pqclean_keccak_profile_get_cycles();
    stats->keccak_calls += pqclean_keccak_profile_get_calls();
    stats->raw_dma_calls += pqclean_keccak_profile_get_dma_calls();
    stats->raw_dma_fallbacks += pqclean_keccak_profile_get_dma_fallbacks();
    stats->raw_dma_hw_op_cycles += pqclean_keccak_profile_get_dma_hw_op_cycles();
    stats->raw_dma_hw_core_cycles += pqclean_keccak_profile_get_dma_hw_core_cycles();
}

static void add_stats(perf_stats_t *total, const perf_stats_t *iter) {
    total->keygen_cycles += iter->keygen_cycles;
    total->sign_cycles += iter->sign_cycles;
    total->verify_cycles += iter->verify_cycles;
    total->keccak_cycles += iter->keccak_cycles;
    total->keccak_calls += iter->keccak_calls;
    total->raw_dma_calls += iter->raw_dma_calls;
    total->raw_dma_fallbacks += iter->raw_dma_fallbacks;
    total->raw_dma_hw_op_cycles += iter->raw_dma_hw_op_cycles;
    total->raw_dma_hw_core_cycles += iter->raw_dma_hw_core_cycles;
}

static void print_stage_profile(const char *stage) {
    printf("[*] %s Keccak     : %lu cycles (%lu perms)\r\n",
           stage,
           (unsigned long)((uint32_t)pqclean_keccak_profile_get_cycles()),
           (unsigned long)pqclean_keccak_profile_get_calls());
    printf("[*] %s Raw DMA    : %lu calls, %lu fallbacks, hw_op=%lu, hw_core=%lu\r\n",
           stage,
           (unsigned long)pqclean_keccak_profile_get_dma_calls(),
           (unsigned long)pqclean_keccak_profile_get_dma_fallbacks(),
           (unsigned long)((uint32_t)pqclean_keccak_profile_get_dma_hw_op_cycles()),
           (unsigned long)((uint32_t)pqclean_keccak_profile_get_dma_hw_core_cycles()));
}

static int run_iteration(uint32_t iter, uint32_t reps, perf_stats_t *stats) {
    uint64_t start, end;
    int rc;

    memset(stats, 0, sizeof(*stats));
    randombytes_seedrng();

    printf("\r\n--- Iteration %lu/%lu ---\r\n", (unsigned long)iter, (unsigned long)reps);

    printf("[.] Starting KeyGen...\r\n");
    pqclean_keccak_profile_reset();
    start = read_cycle64();
    rc = pq_app_sign_keypair(pk, sk);
    end = read_cycle64();
    if (rc != 0) {
        printf("[!] KeyGen failed, rc=%d\r\n", rc);
        return 1;
    }
    stats->keygen_cycles = end - start;
    capture_keccak_stats(stats);
    printf("[*] KeyGen        : ");
    print_u64_dec(stats->keygen_cycles);
    printf(" cycles\r\n");
    print_stage_profile("KeyGen");

    printf("[.] Starting Sign...\r\n");
    pqclean_keccak_profile_reset();
    start = read_cycle64();
    rc = pq_app_sign_signature(sig, &siglen, message, sizeof(message), sk);
    end = read_cycle64();
    if (rc != 0) {
        printf("[!] Sign failed, rc=%d\r\n", rc);
        return 1;
    }
    stats->sign_cycles = end - start;
    capture_keccak_stats(stats);
    printf("[*] Sign          : ");
    print_u64_dec(stats->sign_cycles);
    printf(" cycles (siglen=%lu)\r\n", (unsigned long)siglen);
    print_stage_profile("Sign");

    printf("[.] Starting Verify...\r\n");
    pqclean_keccak_profile_reset();
    start = read_cycle64();
    rc = pq_app_sign_verify(sig, siglen, message, sizeof(message), pk);
    end = read_cycle64();
    stats->verify_cycles = end - start;
    capture_keccak_stats(stats);
    printf("[*] Verify        : ");
    print_u64_dec(stats->verify_cycles);
    printf(" cycles\r\n");
    print_stage_profile("Verify");

    printf("\r\n--- Correctness Check ---\r\n");
    if (rc != 0) {
        printf("Signature Verification: FAIL (rc=%d)\r\n", rc);
        return 1;
    }
    printf("Signature Verification: PASS\r\n");
    return 0;
}

static void print_summary(const char *title, const char *label, const perf_stats_t *stats,
                          int average) {
    const uint64_t total_cycles =
        stats->keygen_cycles + stats->sign_cycles + stats->verify_cycles;
    printf("\r\n--- %s ---\r\n", title);
    printf("%s Signature Operations Cycles: ", label);
    print_u64_dec(average ? avg_u64(total_cycles) : total_cycles);
    printf("\r\n%s Keccak Cycles: ", label);
    print_u64_dec(average ? avg_u64(stats->keccak_cycles) : stats->keccak_cycles);
    printf("\r\n%s Keccak Permutations: ", label);
    print_u64_dec(average ? avg_u64(stats->keccak_calls) : stats->keccak_calls);
    printf("\r\n%s Raw DMA Calls/Fallbacks: ", label);
    print_u64_dec(average ? avg_u64(stats->raw_dma_calls) : stats->raw_dma_calls);
    printf("/");
    print_u64_dec(average ? avg_u64(stats->raw_dma_fallbacks) : stats->raw_dma_fallbacks);
    printf("\r\n%s Raw DMA HW Op/Core Cycles: ", label);
    print_u64_dec(average ? avg_u64(stats->raw_dma_hw_op_cycles) : stats->raw_dma_hw_op_cycles);
    printf("/");
    print_u64_dec(average ? avg_u64(stats->raw_dma_hw_core_cycles) : stats->raw_dma_hw_core_cycles);
    printf("\r\n");
}

int main(void) {
    printf("\r\n=== %s Profiling on X-HEEP ===\r\n\r\n", PQ_APP_ALGNAME);
    printf("[.] Profile reps: %d (PQCLEAN_PROFILE_REPS=%d)\r\n",
           PQCLEAN_PROFILE_REPS, PQCLEAN_PROFILE_REPS);
    printf("[.] Keccak backend: %s\r\n",
           PQCLEAN_USE_KECCAK_DMA ? "raw DMA permutation" : "software KeccakF1600");
    printf("[.] Raw DMA base: 0x%08lx (enabled=%d)\r\n",
           (unsigned long)KECCAK_DMA_START_ADDRESS, PQCLEAN_USE_KECCAK_DMA);

    uint32_t raw_status = 0;
    const int raw_probe = raw_dma_probe(&raw_status);
#if PQCLEAN_USE_KECCAK_DMA
    printf("[CHECK] raw DMA probe: %s (ret=%d status=0x%08lx)\r\n",
           (raw_probe == 0) ? "PASS" : "FAIL",
           raw_probe,
           (unsigned long)raw_status);
#else
    printf("[CHECK] raw DMA probe: SKIP (disabled)\r\n");
#endif

    enable_cycle_counter();

    perf_stats_t total = {0};
    for (uint32_t iter = 1; iter <= PQCLEAN_PROFILE_REPS; iter++) {
        perf_stats_t current = {0};
        if (run_iteration(iter, PQCLEAN_PROFILE_REPS, &current) != 0) {
            printf("\r\n>>> ERROR: Profiling iteration %lu failed! <<<\r\n",
                   (unsigned long)iter);
            return 1;
        }
        add_stats(&total, &current);
    }

    print_summary("Average Performance Summary", "Average", &total, 1);
    print_summary("Cumulative Performance Summary", "Cumulative", &total, 0);
    printf("Total iterations: %lu\r\n", (unsigned long)PQCLEAN_PROFILE_REPS);
    printf("\r\n=== Profiling Complete ===\r\n\r\n");
    printf("\r\n>>> SUCCESS: All profiling iterations completed! <<<\r\n");
    return 0;
}
