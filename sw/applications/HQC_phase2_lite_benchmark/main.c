#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "x-heep.h"
#include "keccak_dma.h"
#include "keccak_sponge_hqc128.h"
#include "keccak_sponge_hqc128_optimized.h"

static uint32_t g_keccak_dma_state_words[KECCAK_DMA_BLOCK_BYTES / sizeof(uint32_t)]
    __attribute__((aligned(4)));
static uint32_t g_keccak_dma_out_words[KECCAK_DMA_BLOCK_BYTES / sizeof(uint32_t)]
    __attribute__((aligned(4)));

#define KECCAK_STATE_ADDR  ((uintptr_t)g_keccak_dma_state_words)
#define KECCAK_OUTPUT_ADDR ((uintptr_t)g_keccak_dma_out_words)

static inline uint32_t read_cycles(void) {
    uint32_t c;
    asm volatile("csrr %0, mcycle" : "=r"(c));
    return c;
}

typedef struct {
    const char *name;
    size_t size;
    uint8_t domain;
} test_t;

static void gen_data(uint8_t *d, size_t len) {
    for (size_t i = 0; i < len; i++) {
        d[i] = ((i * 17 + 53) ^ (i >> 3)) & 0xFF;
    }
}

static keccak_dma_result_t bench_p1(const uint8_t *d, size_t len, uint8_t domain,
                                    keccak_dma_t *k, uint32_t *cycles) {
    uint8_t o[64];
    uint32_t s = read_cycles();
    keccak_sponge_hqc128_ctx_t ctx;
    keccak_dma_result_t ret = keccak_sponge_init(&ctx, k, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_absorb(&ctx, d, len);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_finalize(&ctx, domain);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_squeeze(&ctx, o, 64);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    *cycles = read_cycles() - s;
    return kKeccakDmaOk;
}

static keccak_dma_result_t bench_p2(const uint8_t *d, size_t len, uint8_t domain,
                                    keccak_dma_t *k, uint32_t *cycles) {
    uint8_t o[64];
    uint32_t s = read_cycles();
    keccak_sponge_opt_ctx_t ctx;
    keccak_dma_result_t ret = keccak_sponge_init_opt(&ctx, k, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_absorb_opt(&ctx, d, len);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_finalize_opt(&ctx, domain);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_squeeze_opt(&ctx, o, 64);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    *cycles = read_cycles() - s;
    return kKeccakDmaOk;
}

static keccak_dma_result_t verify(const uint8_t *d, size_t len, uint8_t domain,
                                  keccak_dma_t *k, int *match) {
    uint8_t o1[64], o2[64];
    keccak_dma_result_t ret;
    
    keccak_sponge_hqc128_ctx_t ctx1;
    ret = keccak_sponge_init(&ctx1, k, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_absorb(&ctx1, d, len);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_finalize(&ctx1, domain);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_squeeze(&ctx1, o1, 64);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    keccak_sponge_opt_ctx_t ctx2;
    ret = keccak_sponge_init_opt(&ctx2, k, KECCAK_STATE_ADDR, KECCAK_OUTPUT_ADDR);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_absorb_opt(&ctx2, d, len);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_finalize_opt(&ctx2, domain);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    ret = keccak_sponge_squeeze_opt(&ctx2, o2, 64);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    
    *match = (memcmp(o1, o2, 64) == 0);
    if (!(*match)) {
        printf("[DBG] len=%u domain=0x%02x\n", (unsigned)len, domain);
        printf("[DBG] p1[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x\n",
               o1[0], o1[1], o1[2], o1[3], o1[4], o1[5], o1[6], o1[7]);
        printf("[DBG] p2[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x\n",
               o2[0], o2[1], o2[2], o2[3], o2[4], o2[5], o2[6], o2[7]);
    }
    return kKeccakDmaOk;
}

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║         Phase 2 Sponge Optimization Lightweight Benchmark     ║\n");
    printf("║              (HQC-128 Call Pattern Simulation)                 ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    keccak_dma_t keccak;
    keccak_dma_init(&keccak, KECCAK_DMA_START_ADDRESS);
    printf("[OK] Keccak DMA initialized\n\n");

    memset(g_keccak_dma_state_words, 0, sizeof(g_keccak_dma_state_words));
    keccak_dma_result_t probe_ret = keccak_dma_hash_block(
        &keccak,
        KECCAK_STATE_ADDR,
        KECCAK_OUTPUT_ADDR,
        10000u
    );
    if (probe_ret != kKeccakDmaOk) {
        printf("[ERROR] DMA probe failed (ret=%d)\n", (int)probe_ret);
        printf("[INFO] Exit code: 2\n");
        return 2;
    }
    printf("[OK] DMA probe passed\n\n");
    
    test_t tests[] = {
        {.name = "Empty (0B)", .size = 0, .domain = 0x03},
        {.name = "Small (64B)", .size = 64, .domain = 0x03},
        {.name = "1 rate block (136B)", .size = 136, .domain = 0x03},
        {.name = "2 rate blocks (272B)", .size = 272, .domain = 0x03},
        {.name = "10 blocks (1360B)", .size = 1360, .domain = 0x03},
        {.name = "HQC G-func (2281B)", .size = 2281, .domain = 0x03},
        {.name = "HQC K-func (4433B)", .size = 4433, .domain = 0x04},
    };
    
    size_t num_tests = sizeof(tests) / sizeof(tests[0]);
    static uint8_t data[4433] __attribute__((aligned(4)));
    
    printf("test case              │   Phase 1   │   Phase 2   │  gain  │ improve\n");
    printf("───────────────────────┼─────────────┼─────────────┼────────┼──────────\n");
    
    uint32_t total_p1 = 0, total_p2 = 0;
    int all_ok = 1;
    int abort_on_error = 0;
    
    for (size_t i = 0; i < num_tests; i++) {
        gen_data(data, tests[i].size);

        int match = 0;
        keccak_dma_result_t verify_ret = verify(data, tests[i].size, tests[i].domain, &keccak, &match);
        if (verify_ret != kKeccakDmaOk) {
            printf("%-23s │ [ERROR verify=%d]\n", tests[i].name, (int)verify_ret);
            all_ok = 0;
            abort_on_error = 1;
            break;
        }
        
        if (!match) {
            printf("%-23s │ [MISMATCH]\n", tests[i].name);
            all_ok = 0;
            continue;
        }
        
        uint32_t p1 = 0;
        uint32_t p2 = 0;
        keccak_dma_result_t p1_ret = bench_p1(data, tests[i].size, tests[i].domain, &keccak, &p1);
        if (p1_ret != kKeccakDmaOk) {
            printf("%-23s │ [ERROR p1=%d]\n", tests[i].name, (int)p1_ret);
            all_ok = 0;
            abort_on_error = 1;
            break;
        }
        keccak_dma_result_t p2_ret = bench_p2(data, tests[i].size, tests[i].domain, &keccak, &p2);
        if (p2_ret != kKeccakDmaOk) {
            printf("%-23s │ [ERROR p2=%d]\n", tests[i].name, (int)p2_ret);
            all_ok = 0;
            abort_on_error = 1;
            break;
        }
        
        total_p1 += p1;
        total_p2 += p2;
        
        int32_t gain = (int32_t)p1 - (int32_t)p2;
        float imp = (gain > 0) ? (100.0f * gain / p1) : 0.0f;
        
        printf("%-23s │ %11u │ %11u │ %+6d │ %6.1f%%\n",
               tests[i].name, p1, p2, gain, imp);
    }
    
    printf("───────────────────────┼─────────────┼─────────────┼────────┼──────────\n");
    uint32_t total_gain = (uint32_t)((int32_t)total_p1 - (int32_t)total_p2);
    float total_imp = (total_p1 > 0) ? (100.0f * total_gain / total_p1) : 0.0f;
    printf("%-23s │ %11u │ %11u │ %+6u │ %6.1f%%\n",
           "TOTAL", total_p1, total_p2, total_gain, total_imp);
    printf("═══════════════════════╧═════════════╧═════════════╧════════╧══════════\n\n");
    
    printf("[RESULT] Correctness: %s\n", all_ok ? "PASS" : "FAIL");
    printf("[RESULT] Overall improvement: %.1f%% (%u cycles saved)\n", total_imp, total_gain);
    printf("[INFO] Exit code: %d\n", (all_ok && !abort_on_error) ? 0 : 1);
    
    return (all_ok && !abort_on_error) ? 0 : 1;
}
