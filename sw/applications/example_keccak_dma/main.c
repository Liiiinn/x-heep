// Copyright 2026 OpenHW Group
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "csr.h"
#include "rv_plic.h"
#include "keccak_dma.h"

enum {
    kKeccakWordCount = 50,
    kKeccakTimeoutCycles = 1000000u,
    kInterruptEnableBit = (1u << 11),
    kRandomVectorCount = 8u,
};

static const uint32_t kGoldenOutputWords[4] = {
    0x348759c2u,
    0x163e0232u,
    0xfbe5b11au,
    0x809f4df2u,
};

static uint32_t g_input_words[kKeccakWordCount] __attribute__((aligned(4)));
static uint32_t g_output_words[kKeccakWordCount] __attribute__((aligned(4)));
static uint32_t g_output_words_repeat[kKeccakWordCount] __attribute__((aligned(4)));
static uint32_t g_output_words_alt[kKeccakWordCount] __attribute__((aligned(4)));
static volatile uint32_t g_keccak_irq_count;
static volatile uint32_t g_keccak_irq_last_id;

static void fill_pattern_a(uint32_t *buf) {
    for (uint32_t i = 0; i < kKeccakWordCount; ++i) {
        buf[i] = 0xA5A50000u ^ (i * 0x01010101u);
    }
}

static void fill_pattern_b(uint32_t *buf) {
    uint32_t state = 0x12345678u;
    for (uint32_t i = 0; i < kKeccakWordCount; ++i) {
        state = state * 1664525u + 1013904223u;
        buf[i] = state ^ (i * 0x9E3779B9u);
    }
}

static void fill_pattern_random(uint32_t *buf, uint32_t *state) {
    uint32_t s = *state;
    for (uint32_t i = 0; i < kKeccakWordCount; ++i) {
        s = s * 1664525u + 1013904223u;
        buf[i] = s ^ (i * 0x85ebca6bu) ^ (s >> 7);
    }
    *state = s;
}

static void clear_words(uint32_t *buf) {
    for (uint32_t i = 0; i < kKeccakWordCount; ++i) {
        buf[i] = 0u;
    }
}

static void copy_words(uint32_t *dst, const uint32_t *src) {
    for (uint32_t i = 0; i < kKeccakWordCount; ++i) {
        dst[i] = src[i];
    }
}

static bool words_equal(const uint32_t *a, const uint32_t *b) {
    for (uint32_t i = 0; i < kKeccakWordCount; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static bool words_differ(const uint32_t *a, const uint32_t *b) {
    for (uint32_t i = 0; i < kKeccakWordCount; ++i) {
        if (a[i] != b[i]) {
            return true;
        }
    }
    return false;
}

static bool run_hash_and_check(keccak_dma_t *keccak,
                               const uint32_t *input,
                               uint32_t *output,
                               const char *tag) {
    const keccak_dma_result_t ret = keccak_dma_hash_block(
        keccak, (uintptr_t)input, (uintptr_t)output, kKeccakTimeoutCycles);
    const uint32_t status = keccak_dma_get_status(keccak);

    if (ret != kKeccakDmaOk) {
        printf("[FAIL] %s: ret=%d status=0x%08x\n", tag, ret, status);
        return false;
    }
    if (!keccak_dma_is_done(keccak)) {
        printf("[FAIL] %s: done bit not set, status=0x%08x\n", tag, status);
        return false;
    }
    if (keccak_dma_has_error(keccak)) {
        printf("[FAIL] %s: error bit set, status=0x%08x\n", tag, status);
        return false;
    }

    printf("[PASS] %s: status=0x%08x\n", tag, status);
    return true;
}

static void handler_irq_keccak_dma(uint32_t irq_id) {
    g_keccak_irq_last_id = irq_id;
    g_keccak_irq_count++;
}

static bool setup_keccak_irq_path(void) {
    if (plic_Init() != kPlicOk) {
        printf("[FAIL] irq_setup: plic_Init failed\n");
        return false;
    }
    if (plic_irq_set_trigger(KECCAK_DMA_INTR_NUM, kPlicIrqTriggerEdge) !=
        kPlicOk) {
        printf("[FAIL] irq_setup: plic_irq_set_trigger failed\n");
        return false;
    }
    if (plic_irq_set_priority(KECCAK_DMA_INTR_NUM, 1u) != kPlicOk) {
        printf("[FAIL] irq_setup: plic_irq_set_priority failed\n");
        return false;
    }
    if (plic_irq_set_enabled(KECCAK_DMA_INTR_NUM, kPlicToggleEnabled) !=
        kPlicOk) {
        printf("[FAIL] irq_setup: plic_irq_set_enabled failed\n");
        return false;
    }
    if (plic_assign_external_irq_handler(KECCAK_DMA_INTR_NUM,
                                         &handler_irq_keccak_dma) != kPlicOk) {
        printf("[FAIL] irq_setup: handler assignment failed\n");
        return false;
    }

    CSR_SET_BITS(CSR_REG_MSTATUS, 0x8u);
    CSR_SET_BITS(CSR_REG_MIE, kInterruptEnableBit);
    return true;
}

static void teardown_keccak_irq_path(void) {
    (void)plic_irq_set_enabled(KECCAK_DMA_INTR_NUM, kPlicToggleDisabled);
    CSR_CLEAR_BITS(CSR_REG_MIE, kInterruptEnableBit);
}

static bool run_hash_interrupt_path_test(keccak_dma_t *keccak,
                                         const uint32_t *input,
                                         uint32_t *output,
                                         const char *tag) {
    if (!setup_keccak_irq_path()) {
        return false;
    }

    const uint32_t irq_count_before = g_keccak_irq_count;
    g_keccak_irq_last_id = 0xffffffffu;
    clear_words(output);

    const keccak_dma_result_t start_ret =
        keccak_dma_start(keccak, (uintptr_t)input, (uintptr_t)output,
                         KECCAK_DMA_BLOCK_BYTES);
    if (start_ret != kKeccakDmaOk) {
        printf("[FAIL] %s: start ret=%d\n", tag, start_ret);
        teardown_keccak_irq_path();
        return false;
    }

    const keccak_dma_result_t wait_ret =
        keccak_dma_wait(keccak, kKeccakTimeoutCycles);
    const uint32_t status = keccak_dma_get_status(keccak);
    const uint32_t irq_count_after = g_keccak_irq_count;

    teardown_keccak_irq_path();

    if (wait_ret != kKeccakDmaOk) {
        printf("[FAIL] %s: wait ret=%d status=0x%08x\n", tag, wait_ret,
               status);
        return false;
    }
    if (irq_count_after == irq_count_before) {
        printf("[FAIL] %s: no external interrupt observed\n", tag);
        return false;
    }
    if (g_keccak_irq_last_id != KECCAK_DMA_INTR_NUM) {
        printf("[FAIL] %s: unexpected irq id=%u expected=%u\n", tag,
               g_keccak_irq_last_id, (uint32_t)KECCAK_DMA_INTR_NUM);
        return false;
    }
    if (keccak_dma_has_error(keccak)) {
        printf("[FAIL] %s: error bit set, status=0x%08x\n", tag, status);
        return false;
    }

    printf("[PASS] %s: irq_count_delta=%u status=0x%08x\n", tag,
           irq_count_after - irq_count_before, status);
    return true;
}

static bool run_random_batch_regression(keccak_dma_t *keccak,
                                        uint32_t vector_count) {
    uint32_t rng_state = 0x13579bdfu;
    bool saw_output_change = false;

    clear_words(g_output_words_alt);

    for (uint32_t v = 0; v < vector_count; ++v) {
        fill_pattern_random(g_input_words, &rng_state);
        clear_words(g_output_words);
        clear_words(g_output_words_repeat);

        if (!run_hash_and_check(keccak, g_input_words, g_output_words,
                                "random_batch_run1")) {
            printf("[FAIL] random_batch: vector %u run1 failed\n", v);
            return false;
        }

        if (!run_hash_and_check(keccak, g_input_words, g_output_words_repeat,
                                "random_batch_run2")) {
            printf("[FAIL] random_batch: vector %u run2 failed\n", v);
            return false;
        }

        if (!words_equal(g_output_words, g_output_words_repeat)) {
            printf("[FAIL] random_batch: vector %u non-deterministic output\n",
                   v);
            return false;
        }

        if (v > 0u && words_differ(g_output_words, g_output_words_alt)) {
            saw_output_change = true;
        }

        copy_words(g_output_words_alt, g_output_words);
    }

    if (vector_count > 1u && !saw_output_change) {
        printf("[FAIL] random_batch: all vectors produced identical digest\n");
        return false;
    }

    printf("[PASS] random_batch: %u vectors passed\n", vector_count);
    return true;
}

int main(void) {
    keccak_dma_t keccak;
    keccak_dma_init(&keccak, KECCAK_DMA_START_ADDRESS);
    int failures = 0;

    // 1) Driver-side bad length check.
    fill_pattern_a(g_input_words);
    clear_words(g_output_words);
    const keccak_dma_result_t bad_len_ret = keccak_dma_start(
        &keccak, (uintptr_t)g_input_words, (uintptr_t)g_output_words,
        KECCAK_DMA_BLOCK_BYTES - 4u);
    if (bad_len_ret != kKeccakDmaBadLen) {
        printf("[FAIL] bad_len: expected %d, got %d\n", kKeccakDmaBadLen,
               bad_len_ret);
        failures++;
    } else {
        printf("[PASS] bad_len: got expected return code %d\n", bad_len_ret);
    }

    // 2) Golden vector regression check.
    fill_pattern_a(g_input_words);
    clear_words(g_output_words);
    if (!run_hash_and_check(&keccak, g_input_words, g_output_words,
                            "golden_vector")) {
        failures++;
    } else {
        for (int i = 0; i < 4; ++i) {
            if (g_output_words[i] != kGoldenOutputWords[i]) {
                printf(
                    "[FAIL] golden_vector: out[%d]=0x%08x expected=0x%08x\n",
                    i, g_output_words[i], kGoldenOutputWords[i]);
                failures++;
            }
        }
    }

    // 3) Interrupt-path check: completion must also be observable as external IRQ.
    fill_pattern_a(g_input_words);
    if (!run_hash_interrupt_path_test(&keccak, g_input_words,
                                      g_output_words_repeat,
                                      "interrupt_path")) {
        failures++;
    }

    // 4) Determinism check: same input twice must produce identical output.
    clear_words(g_output_words_repeat);
    if (!run_hash_and_check(&keccak, g_input_words, g_output_words_repeat,
                            "repeat_vector")) {
        failures++;
    } else if (!words_equal(g_output_words, g_output_words_repeat)) {
        printf("[FAIL] determinism: repeated run differs from golden output\n");
        failures++;
    } else {
        printf("[PASS] determinism: repeated run matches golden output\n");
    }

    // 5) Input sensitivity check: different input should produce different output.
    fill_pattern_b(g_input_words);
    clear_words(g_output_words_alt);
    if (!run_hash_and_check(&keccak, g_input_words, g_output_words_alt,
                            "alt_vector")) {
        failures++;
    } else if (!words_differ(g_output_words, g_output_words_alt)) {
        printf("[FAIL] sensitivity: alternate input produced identical output\n");
        failures++;
    } else {
        printf("[PASS] sensitivity: alternate input changes output\n");
    }

    // 6) Random multi-vector regression check.
    if (!run_random_batch_regression(&keccak, kRandomVectorCount)) {
        failures++;
    }

    if (failures != 0) {
        printf("Keccak DMA regression: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    printf("Keccak DMA regression: all checks passed\n");
    for (int i = 0; i < 4; ++i) {
        printf("out[%d] = 0x%08x\n", i, g_output_words[i]);
    }

    return EXIT_SUCCESS;
}
