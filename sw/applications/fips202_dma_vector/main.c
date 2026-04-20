#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../HQC/common/fips202.h"

enum {
    kOutLen = 64,
};

static const uint8_t kMsgEmpty[1] = {0x00};
static const uint8_t kMsgAbc[] = {'a', 'b', 'c'};
static const uint8_t kMsgCustom[] = "OpenTitan-x-heep-keccak-dma";

static const uint8_t kExpectedShake256Empty[kOutLen] = {
    0x46, 0xb9, 0xdd, 0x2b, 0x0b, 0xa8, 0x8d, 0x13,
    0x23, 0x3b, 0x3f, 0xeb, 0x74, 0x3e, 0xeb, 0x24,
    0x3f, 0xcd, 0x52, 0xea, 0x62, 0xb8, 0x1b, 0x82,
    0xb5, 0x0c, 0x27, 0x64, 0x6e, 0xd5, 0x76, 0x2f,
    0xd7, 0x5d, 0xc4, 0xdd, 0xd8, 0xc0, 0xf2, 0x00,
    0xcb, 0x05, 0x01, 0x9d, 0x67, 0xb5, 0x92, 0xf6,
    0xfc, 0x82, 0x1c, 0x49, 0x47, 0x9a, 0xb4, 0x86,
    0x40, 0x29, 0x2e, 0xac, 0xb3, 0xb7, 0xc4, 0xbe,
};

static const uint8_t kExpectedShake256Abc[kOutLen] = {
    0x48, 0x33, 0x66, 0x60, 0x13, 0x60, 0xa8, 0x77,
    0x1c, 0x68, 0x63, 0x08, 0x0c, 0xc4, 0x11, 0x4d,
    0x8d, 0xb4, 0x45, 0x30, 0xf8, 0xf1, 0xe1, 0xee,
    0x4f, 0x94, 0xea, 0x37, 0xe7, 0x8b, 0x57, 0x39,
    0xd5, 0xa1, 0x5b, 0xef, 0x18, 0x6a, 0x53, 0x86,
    0xc7, 0x57, 0x44, 0xc0, 0x52, 0x7e, 0x1f, 0xaa,
    0x9f, 0x87, 0x26, 0xe4, 0x62, 0xa1, 0x2a, 0x4f,
    0xeb, 0x06, 0xbd, 0x88, 0x01, 0xe7, 0x51, 0xe4,
};

static const uint8_t kExpectedShake256Custom[kOutLen] = {
    0x47, 0x6b, 0x8a, 0xca, 0xfe, 0x93, 0xe0, 0x6d,
    0x73, 0xa7, 0xb6, 0x02, 0xef, 0xb2, 0x69, 0x18,
    0xe5, 0xee, 0x7e, 0xb9, 0xa8, 0xc1, 0x8e, 0x5c,
    0x90, 0x8a, 0x15, 0xb3, 0x19, 0x84, 0xc1, 0xac,
    0x98, 0xc8, 0xf3, 0xa7, 0xb2, 0x67, 0x80, 0xb9,
    0x11, 0x0a, 0x64, 0xaa, 0xcd, 0x4a, 0xff, 0xc0,
    0x24, 0x3d, 0x31, 0xc5, 0x63, 0xd4, 0xc2, 0x8b,
    0x46, 0x98, 0x73, 0x1e, 0x00, 0x3f, 0x93, 0xe9,
};

static void print_hex_prefix(const uint8_t *buf, size_t len) {
    const size_t shown = (len < 16u) ? len : 16u;
    for (size_t i = 0; i < shown; ++i) {
        printf("%02x", buf[i]);
    }
}

static bool run_shake256_vector(const char *name,
                                const uint8_t *msg,
                                size_t mlen,
                                const uint8_t *expected) {
    uint8_t out[kOutLen];
    shake256(out, kOutLen, msg, mlen);

    if (memcmp(out, expected, kOutLen) != 0) {
        printf("[FAIL] %s\n", name);
        printf("       got      : ");
        print_hex_prefix(out, kOutLen);
        printf("...\n");
        printf("       expected : ");
        print_hex_prefix(expected, kOutLen);
        printf("...\n");
        return false;
    }

    printf("[PASS] %s\n", name);
    return true;
}

int main(void) {
    int failures = 0;

    hqc_keccak_profile_reset();

    if (!run_shake256_vector("shake256_empty_64B", kMsgEmpty, 0u,
                             kExpectedShake256Empty)) {
        failures++;
    }

    if (!run_shake256_vector("shake256_abc_64B", kMsgAbc, sizeof(kMsgAbc),
                             kExpectedShake256Abc)) {
        failures++;
    }

    if (!run_shake256_vector("shake256_custom_64B", kMsgCustom,
                             sizeof(kMsgCustom) - 1u,
                             kExpectedShake256Custom)) {
        failures++;
    }

    const uint32_t perm_calls = hqc_keccak_profile_get_calls();
    const uint32_t dma_calls = hqc_keccak_profile_get_dma_calls();
    const uint32_t dma_fallbacks = hqc_keccak_profile_get_dma_fallbacks();

    printf("[INFO] keccak_perm_calls=%u dma_calls=%u dma_fallbacks=%u\n",
           perm_calls, dma_calls, dma_fallbacks);

    if (perm_calls == 0u) {
        printf("[FAIL] no Keccak permutation calls observed\n");
        failures++;
    }
    if (dma_calls == 0u) {
        printf("[FAIL] no DMA-backed permutations observed\n");
        failures++;
    }
    if (dma_fallbacks != 0u) {
        printf("[FAIL] DMA fallback detected (%u)\n", dma_fallbacks);
        failures++;
    }

    if (failures != 0) {
        printf("FIPS202 DMA vector check: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    printf("FIPS202 DMA vector check: all checks passed\n");
    return EXIT_SUCCESS;
}
