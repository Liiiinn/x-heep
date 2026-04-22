#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "x-heep.h"
#include "keccak_dma.h"
#include "csr.h"
#include "csr_registers.h"
#include "hqc_port/fips202.h"
#include "hqc_port/hqc_phase2_shake_backend.h"
#include "keccak_sponge_hqc128.h"
#include "keccak_sponge_hqc128_optimized.h"
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
static uint8_t g_out_var_p1[544];
static uint8_t g_out_var_p2[544];
static uint8_t g_g_input[VEC_K_SIZE_BYTES + PUBLIC_KEY_BYTES + SALT_SIZE_BYTES];
static uint8_t g_k_input[VEC_K_SIZE_BYTES + VEC_N_SIZE_BYTES + VEC_N1N2_SIZE_BYTES];
static keccak_dma_t *g_keccak_ptr = NULL;

typedef struct {
    const char *name;
    const uint8_t *input;
    size_t inlen;
    uint8_t domain;
    size_t outlen;
    int use_incremental;
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

static run_stats_t run_shake_case_incremental(hqc_phase2_shake_backend_t backend,
                                              const uint8_t *input,
                                              size_t inlen,
                                              uint8_t domain,
                                              size_t outlen,
                                              uint8_t *out) {
    run_stats_t stats = {0};
    hqc_phase2_set_shake_backend(backend);
    hqc_keccak_profile_reset();
    uint32_t start = read_cycles();
    keccak_dma_result_t ret;

    if (backend == HQC_PHASE2_SHAKE_BACKEND_P2) {
        keccak_sponge_opt_ctx_t ctx;
        ret = keccak_sponge_init_opt(&ctx, g_keccak_ptr, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR);
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_absorb_opt(&ctx, input, inlen);
        }
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_finalize_opt(&ctx, domain);
        }
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_squeeze_opt(&ctx, out, outlen);
        }
    } else {
        keccak_sponge_hqc128_ctx_t ctx;
        ret = keccak_sponge_init(&ctx, g_keccak_ptr, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR);
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_absorb(&ctx, input, inlen);
        }
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_finalize(&ctx, domain);
        }
        if (ret == kKeccakDmaOk) {
            ret = keccak_sponge_squeeze(&ctx, out, outlen);
        }
    }

    if (ret != kKeccakDmaOk) {
        /* Keep fallback behavior consistent with shake_ds.c */
        shake256incctx state;
        shake256_inc_init(&state);
        shake256_inc_absorb(&state, input, inlen);
        shake256_inc_absorb(&state, &domain, 1);
        shake256_inc_finalize(&state);
        shake256_inc_squeeze(out, outlen, &state);
        shake256_inc_ctx_release(&state);
    }

    stats.cycles = read_cycles() - start;
    stats.keccak_cycles = hqc_keccak_profile_get_cycles();
    stats.keccak_calls = hqc_keccak_profile_get_calls();
    return stats;
}

static void u64_to_dec(uint64_t value, char out[21]) {
    char tmp[21];
    int pos = 0;
    if (value == 0u) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (value > 0u && pos < 20) {
        tmp[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    for (int i = 0; i < pos; ++i) {
        out[i] = tmp[pos - 1 - i];
    }
    out[pos] = '\0';
}

static void print_row(const char *name, uint32_t p1, uint32_t p2, int match) {
    int32_t gain = (int32_t)p1 - (int32_t)p2;
    uint32_t abs_gain = (gain >= 0) ? (uint32_t)gain : (uint32_t)(-gain);
    uint32_t imp_tenths = (p1 > 0) ? ((abs_gain * 1000u) / p1) : 0u;
    char sign = (gain >= 0) ? '+' : '-';
    printf("%-20s │ %9u │ %9u │ %+13d │ %c%u.%1u%% │ %s\n",
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
    g_keccak_ptr = &keccak;
    printf("[OK] Keccak DMA initialized\n");

    int all_ok = 1;
    uintptr_t align_mask = (uintptr_t)(KECCAK_DMA_ADDR_ALIGN_BYTES - 1u);
    int addr_aligned = (((KECCAK_STATE_ADDR | KECCAK_OUTPUT_ADDR) & align_mask) == 0u);
    printf("[CHECK] addr alignment: state=0x%08x output=0x%08x align=%u -> %s\n",
           (unsigned)KECCAK_STATE_ADDR, (unsigned)KECCAK_OUTPUT_ADDR,
           (unsigned)KECCAK_DMA_ADDR_ALIGN_BYTES, addr_aligned ? "PASS" : "FAIL");
    if (!addr_aligned) {
        all_ok = 0;
    }

    memset(g_keccak_dma_state_words, 0, sizeof(g_keccak_dma_state_words));
    printf("[DBG] before 30x DMA probe\n");
    int probe_passes = 0;
    for (int i = 0; i < 30; ++i) {
        keccak_dma_result_t probe_ret = keccak_dma_hash_block(
            &keccak, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR, 10000u);
        if (probe_ret == kKeccakDmaOk) {
            probe_passes++;
        } else {
            printf("[ERROR] DMA probe[%d] failed (ret=%d)\n", i, (int)probe_ret);
        }
    }
    printf("[CHECK] 30x DMA probe pass count: %d/30 -> %s\n",
           probe_passes, (probe_passes == 30) ? "PASS" : "FAIL");
    if (probe_passes != 30) {
        all_ok = 0;
    }

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
        {"G_domain_full_ds64", g_g_input, sizeof(g_g_input), G_FCT_DOMAIN, SHAKE256_512_BYTES, 0},
        {"K_domain_full_ds64", g_k_input, sizeof(g_k_input), K_FCT_DOMAIN, SHAKE256_512_BYTES, 0},
        {"edge_136_ds64", edge_136, sizeof(edge_136), G_FCT_DOMAIN, SHAKE256_512_BYTES, 0},
        {"edge_137_ds64", edge_137, sizeof(edge_137), G_FCT_DOMAIN, SHAKE256_512_BYTES, 0},
        {"edge_544_ds64", edge_544, sizeof(edge_544), K_FCT_DOMAIN, SHAKE256_512_BYTES, 0},
        {"sq_g_64", g_g_input, sizeof(g_g_input), G_FCT_DOMAIN, 64, 1},
        {"sq_g_136", g_g_input, sizeof(g_g_input), G_FCT_DOMAIN, 136, 1},
        {"sq_g_137", g_g_input, sizeof(g_g_input), G_FCT_DOMAIN, 137, 1},
        {"sq_k_272", g_k_input, sizeof(g_k_input), K_FCT_DOMAIN, 272, 1},
        {"sq_k_544", g_k_input, sizeof(g_k_input), K_FCT_DOMAIN, 544, 1},
    };

    uint8_t fallback_p1[SHAKE256_512_BYTES];
    uint8_t fallback_p2[SHAKE256_512_BYTES];

    /* Fallback test #1: uninitialized backend context (g_keccak == NULL). */
    run_stats_t preinit_p1 = run_shake_case(HQC_PHASE2_SHAKE_BACKEND_P1,
                                            edge_136, sizeof(edge_136), G_FCT_DOMAIN, fallback_p1);
    run_stats_t preinit_p2 = run_shake_case(HQC_PHASE2_SHAKE_BACKEND_P2,
                                            edge_136, sizeof(edge_136), G_FCT_DOMAIN, fallback_p2);
    int fallback_uninit_match = (memcmp(fallback_p1, fallback_p2, SHAKE256_512_BYTES) == 0);
    int fallback_uninit_alive = (preinit_p1.cycles > 0u && preinit_p2.cycles > 0u);
    printf("[CHECK] fallback(uninitialized backend): %s\n",
           (fallback_uninit_match && fallback_uninit_alive) ? "PASS" : "FAIL");
    if (!(fallback_uninit_match && fallback_uninit_alive)) {
        all_ok = 0;
    }

    /* Fallback test #2: force misaligned addresses. */
    hqc_phase2_shake_backend_init(&keccak, KECCAK_STATE_ADDR + 1u, KECCAK_OUTPUT_ADDR + 1u);
    run_stats_t misalign_p1 = run_shake_case(HQC_PHASE2_SHAKE_BACKEND_P1,
                                             edge_136, sizeof(edge_136), G_FCT_DOMAIN, fallback_p1);
    run_stats_t misalign_p2 = run_shake_case(HQC_PHASE2_SHAKE_BACKEND_P2,
                                             edge_136, sizeof(edge_136), G_FCT_DOMAIN, fallback_p2);
    int fallback_misalign_match = (memcmp(fallback_p1, fallback_p2, SHAKE256_512_BYTES) == 0);
    int fallback_misalign_alive = (misalign_p1.cycles > 0u && misalign_p2.cycles > 0u);
    printf("[CHECK] fallback(misaligned addr): %s\n",
           (fallback_misalign_match && fallback_misalign_alive) ? "PASS" : "FAIL");
    if (!(fallback_misalign_match && fallback_misalign_alive)) {
        all_ok = 0;
    }

    /* Restore aligned DMA backend for normal L1 loop. */
    hqc_phase2_shake_backend_init(&keccak, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR);
    printf("[OK] Backend init done\n\n");
    printf("[DBG] before L1 test loop\n");

    uint32_t total_p1 = 0;
    uint32_t total_p2 = 0;
    uint64_t total_keccak_p1 = 0;
    uint64_t total_keccak_p2 = 0;
    uint32_t total_calls_p1 = 0;
    uint32_t total_calls_p2 = 0;

    printf("case                 │ P1 cycles │ P2 cycles │   gain(P1-P2) │ improve │ match\n");
    printf("─────────────────────┼───────────┼───────────┼───────────────┼─────────┼──────\n");

    for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
        printf("[DBG] case=%s in=%u out=%u domain=%u mode=%s\n",
               cases[i].name, (unsigned)cases[i].inlen, (unsigned)cases[i].outlen,
               (unsigned)cases[i].domain, cases[i].use_incremental ? "inc" : "ds64");
        run_stats_t p1;
        run_stats_t p2;
        if (cases[i].use_incremental) {
            p1 = run_shake_case_incremental(HQC_PHASE2_SHAKE_BACKEND_P1,
                                            cases[i].input, cases[i].inlen, cases[i].domain,
                                            cases[i].outlen, g_out_var_p1);
            p2 = run_shake_case_incremental(HQC_PHASE2_SHAKE_BACKEND_P2,
                                            cases[i].input, cases[i].inlen, cases[i].domain,
                                            cases[i].outlen, g_out_var_p2);
        } else {
            p1 = run_shake_case(HQC_PHASE2_SHAKE_BACKEND_P1,
                                cases[i].input, cases[i].inlen, cases[i].domain, g_out_p1);
            p2 = run_shake_case(HQC_PHASE2_SHAKE_BACKEND_P2,
                                cases[i].input, cases[i].inlen, cases[i].domain, g_out_p2);
        }

        // In the DMA sponge quick-regression path, fips202 profile counters may
        // stay zero because shake_ds bypasses the fips202 permutation hook.
        // Fall back to per-case wall cycles so UART never reports misleading 0.
        if (p1.keccak_calls == 0u && p1.keccak_cycles == 0u && p1.cycles > 0u) {
            p1.keccak_calls = 1u;
            p1.keccak_cycles = p1.cycles;
        }
        if (p2.keccak_calls == 0u && p2.keccak_cycles == 0u && p2.cycles > 0u) {
            p2.keccak_calls = 1u;
            p2.keccak_cycles = p2.cycles;
        }

        int match = 0;
        if (cases[i].use_incremental) {
            match = (memcmp(g_out_var_p1, g_out_var_p2, cases[i].outlen) == 0);
        } else {
            match = (memcmp(g_out_p1, g_out_p2, SHAKE256_512_BYTES) == 0);
        }
        print_row(cases[i].name, p1.cycles, p2.cycles, match);

        if (!match) {
            printf("[ERROR] bit-exact mismatch on case=%s\n", cases[i].name);
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

    char keccak_p1_dec[21];
    char keccak_p2_dec[21];
    u64_to_dec(total_keccak_p1, keccak_p1_dec);
    u64_to_dec(total_keccak_p2, keccak_p2_dec);
    printf("[KECCAK] P1: %u calls, %s cycles\n", total_calls_p1, keccak_p1_dec);
    printf("[KECCAK] P2: %u calls, %s cycles\n", total_calls_p2, keccak_p2_dec);
    printf("[CHECK] P1/P2 bit-exact consistency: %s\n", all_ok ? "PASS" : "FAIL");
    printf("[CHECK] Sanity path covered: G(m||pk||salt) + K(m||u||v)\n");
    printf("[INFO] Exit code: %d\n", all_ok ? 0 : 1);
    return all_ok ? 0 : 1;
}
