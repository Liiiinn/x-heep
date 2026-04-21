#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "x-heep.h"
#include "keccak_dma.h"
#include "csr.h"
#include "csr_registers.h"
#include "hqc_port/fips202.h"
#include "hqc_port/hqc_phase2_shake_backend.h"
#include "../HQC/src/shake_ds.h"
#include "../HQC/src/domains.h"
#include "../HQC/src/parameters.h"

static uint32_t g_keccak_dma_state_words[KECCAK_DMA_BLOCK_BYTES / sizeof(uint32_t)]
    __attribute__((aligned(4)));
static uint32_t g_keccak_dma_out_words[KECCAK_DMA_BLOCK_BYTES / sizeof(uint32_t)]
    __attribute__((aligned(4)));

#define KECCAK_STATE_ADDR  ((uintptr_t)g_keccak_dma_state_words)
#define KECCAK_OUTPUT_ADDR ((uintptr_t)g_keccak_dma_out_words)

static uint8_t g_out_p1[SHAKE256_512_BYTES];
static uint8_t g_out_p2[SHAKE256_512_BYTES];
static uint8_t g_g_input[VEC_K_SIZE_BYTES + PUBLIC_KEY_BYTES + SALT_SIZE_BYTES];
static uint8_t g_k_input[VEC_K_SIZE_BYTES + VEC_N_SIZE_BYTES + VEC_N1N2_SIZE_BYTES];

typedef struct {
    const char *name;
    const uint8_t *input;
    size_t inlen;
    uint8_t domain;
} l1_case_t;

typedef struct {
    uint32_t cycles;
    uint64_t keccak_cycles;
    uint32_t keccak_calls;
} run_stats_t;

static inline void enable_cycle_counter(void) {
    CSR_CLEAR_BITS(CSR_REG_MCOUNTINHIBIT, 0x1u);
}

static inline uint32_t read_cycles(void) {
    uint32_t c;
    asm volatile("csrr %0, mcycle" : "=r"(c));
    return c;
}

static void fill_pattern(uint8_t *dst, size_t len, uint8_t seed) {
    uint8_t x = seed;
    for (size_t i = 0; i < len; ++i) {
        x = (uint8_t)(x * 33u + 17u);
        dst[i] = (uint8_t)(x ^ (uint8_t)i);
    }
}

static run_stats_t run_shake_case(hqc_phase2_shake_backend_t backend,
                                  const uint8_t *input,
                                  size_t inlen,
                                  uint8_t domain,
                                  uint8_t *out) {
    run_stats_t stats = {0};
    shake256incctx state;
    hqc_phase2_set_shake_backend(backend);
    hqc_keccak_profile_reset();
    uint32_t start = read_cycles();
    PQCLEAN_HQC128_CLEAN_shake256_512_ds(&state, out, input, inlen, domain);
    stats.cycles = read_cycles() - start;
    stats.keccak_cycles = hqc_keccak_profile_get_cycles();
    stats.keccak_calls = hqc_keccak_profile_get_calls();
    return stats;
}

static void print_row(const char *name, uint32_t p1, uint32_t p2, int match) {
    int32_t gain = (int32_t)p1 - (int32_t)p2;
    uint32_t abs_gain = (gain >= 0) ? (uint32_t)gain : (uint32_t)(-gain);
    uint32_t imp_tenths = (p1 > 0) ? ((abs_gain * 1000u) / p1) : 0u;
    char sign = (gain >= 0) ? '+' : '-';
    printf("%-20s │ %9u │ %9u │ %+8d │ %c%3u.%1u%% │ %s\n",
           name, p1, p2, gain, sign, imp_tenths / 10u, imp_tenths % 10u, match ? "PASS" : "FAIL");
}

int main(void) {
    printf("[DBG] entered main\n");
    enable_cycle_counter();
    printf("[DBG] cycle counter enabled\n");

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════╗\n");
    printf("║          HQC-128 Phase1/Phase2 SHAKE L1 Quick Regression            ║\n");
    printf("║   Real shake256_512_ds path + G/K domain semantics + edge lengths   ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════╝\n\n");

    keccak_dma_t keccak;
    printf("[DBG] before keccak_dma_init\n");
    keccak_dma_init(&keccak, KECCAK_DMA_START_ADDRESS);
    printf("[OK] Keccak DMA initialized\n");

    memset(g_keccak_dma_state_words, 0, sizeof(g_keccak_dma_state_words));
    printf("[DBG] before DMA probe\n");
    keccak_dma_result_t probe_ret = keccak_dma_hash_block(
        &keccak, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR, 10000u);
    if (probe_ret != kKeccakDmaOk) {
        printf("[ERROR] DMA probe failed (ret=%d)\n", (int)probe_ret);
        printf("[INFO] Exit code: 2\n");
        return 2;
    }
    printf("[OK] DMA probe passed\n");

    hqc_phase2_shake_backend_init(&keccak, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR);
    printf("[OK] Backend init done\n\n");
    printf("[DBG] before L1 test loop\n");

    fill_pattern(g_g_input, sizeof(g_g_input), 0x35u);
    fill_pattern(g_k_input, sizeof(g_k_input), 0x5au);

    /* G-domain input semantics: m || pk || salt */
    fill_pattern(g_g_input, VEC_K_SIZE_BYTES, 0x11u);
    fill_pattern(g_g_input + VEC_K_SIZE_BYTES, PUBLIC_KEY_BYTES, 0x22u);
    fill_pattern(g_g_input + VEC_K_SIZE_BYTES + PUBLIC_KEY_BYTES, SALT_SIZE_BYTES, 0x33u);

    /* K-domain input semantics: m || u || v */
    fill_pattern(g_k_input, VEC_K_SIZE_BYTES, 0x44u);
    fill_pattern(g_k_input + VEC_K_SIZE_BYTES, VEC_N_SIZE_BYTES, 0x55u);
    fill_pattern(g_k_input + VEC_K_SIZE_BYTES + VEC_N_SIZE_BYTES, VEC_N1N2_SIZE_BYTES, 0x66u);

    static uint8_t edge_136[136];
    static uint8_t edge_137[137];
    static uint8_t edge_544[544];
    fill_pattern(edge_136, sizeof(edge_136), 0x80u);
    fill_pattern(edge_137, sizeof(edge_137), 0x90u);
    fill_pattern(edge_544, sizeof(edge_544), 0xa0u);

    l1_case_t cases[] = {
        {"G_domain_full", g_g_input, sizeof(g_g_input), G_FCT_DOMAIN},
        {"K_domain_full", g_k_input, sizeof(g_k_input), K_FCT_DOMAIN},
        {"edge_136_1blk", edge_136, sizeof(edge_136), G_FCT_DOMAIN},
        {"edge_137_cross", edge_137, sizeof(edge_137), G_FCT_DOMAIN},
        {"edge_544_multi", edge_544, sizeof(edge_544), K_FCT_DOMAIN},
    };

    int all_ok = 1;
    uint32_t total_p1 = 0;
    uint32_t total_p2 = 0;
    uint64_t total_keccak_p1 = 0;
    uint64_t total_keccak_p2 = 0;
    uint32_t total_calls_p1 = 0;
    uint32_t total_calls_p2 = 0;

    printf("case                 │  P1 cycle │  P2 cycle │   gain(P1-P2) │ improve │ match\n");
    printf("─────────────────────┼───────────┼───────────┼───────────────┼─────────┼──────\n");

    for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
        printf("[DBG] case=%s len=%u domain=%u\n", cases[i].name, (unsigned)cases[i].inlen, (unsigned)cases[i].domain);
        run_stats_t p1 = run_shake_case(HQC_PHASE2_SHAKE_BACKEND_P1,
                                        cases[i].input, cases[i].inlen, cases[i].domain, g_out_p1);
        run_stats_t p2 = run_shake_case(HQC_PHASE2_SHAKE_BACKEND_P2,
                                        cases[i].input, cases[i].inlen, cases[i].domain, g_out_p2);
        int match = (memcmp(g_out_p1, g_out_p2, SHAKE256_512_BYTES) == 0);
        print_row(cases[i].name, p1.cycles, p2.cycles, match);

        if (!match) {
            all_ok = 0;
        }
        total_p1 += p1.cycles;
        total_p2 += p2.cycles;
        total_keccak_p1 += p1.keccak_cycles;
        total_keccak_p2 += p2.keccak_cycles;
        total_calls_p1 += p1.keccak_calls;
        total_calls_p2 += p2.keccak_calls;
    }

    printf("─────────────────────┼───────────┼───────────┼───────────────┼─────────┼──────\n");
    print_row("TOTAL", total_p1, total_p2, all_ok);
    printf("═════════════════════╧═══════════╧═══════════╧═══════════════╧═════════╧══════\n\n");

    printf("[KECCAK] P1: %u calls, 0x%08x%08x cycles\n",
           total_calls_p1,
           (unsigned)((total_keccak_p1 >> 32) & 0xffffffffu),
           (unsigned)(total_keccak_p1 & 0xffffffffu));
    printf("[KECCAK] P2: %u calls, 0x%08x%08x cycles\n",
           total_calls_p2,
           (unsigned)((total_keccak_p2 >> 32) & 0xffffffffu),
           (unsigned)(total_keccak_p2 & 0xffffffffu));
    printf("[CHECK] P1/P2 bit-exact consistency: %s\n", all_ok ? "PASS" : "FAIL");
    printf("[CHECK] Sanity path covered: G(m||pk||salt) + K(m||u||v)\n");
    printf("[INFO] Exit code: %d\n", all_ok ? 0 : 1);
    return all_ok ? 0 : 1;
}
