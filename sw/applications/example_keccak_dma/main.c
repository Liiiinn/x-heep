// Copyright 2026 OpenHW Group
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "keccak_dma.h"

static uint32_t g_input_words[50] __attribute__((aligned(4)));
static uint32_t g_output_words[50] __attribute__((aligned(4)));

int main(void) {
    for (uint32_t i = 0; i < 50; ++i) {
        g_input_words[i] = 0xA5A50000u ^ (i * 0x01010101u);
        g_output_words[i] = 0u;
    }

    keccak_dma_t keccak;
    keccak_dma_init(&keccak, KECCAK_DMA_START_ADDRESS);

    keccak_dma_result_t ret =
        keccak_dma_hash_block(&keccak, (uintptr_t)g_input_words,
                                (uintptr_t)g_output_words, 1000000u);

    if (ret != kKeccakDmaOk) {
        printf("keccak_dma failed, ret=%d status=0x%08x\n", ret,
            keccak_dma_get_status(&keccak));
        return EXIT_FAILURE;
    }

    printf("keccak_dma done status=0x%08x\n", keccak_dma_get_status(&keccak));
    for (int i = 0; i < 4; ++i) {
        printf("out[%d] = 0x%08x\n", i, g_output_words[i]);
    }

    return EXIT_SUCCESS;
}
