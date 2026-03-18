/*
The MIT License

Copyright (c) 2017 Daan Sprenkels <hello@dsprenkels.com>

Modified for RISC-V embedded environment (X-HEEP)
Uses a simple LFSR-based PRNG suitable for performance profiling.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "randombytes.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Simple LFSR-based PRNG for RISC-V embedded systems
 * Uses Galois configuration: polynomial x^32 + x^31 + x^29 + x^25 + 1
 * Suitable for performance profiling, not cryptographically secure.
 */
static uint32_t lfsr_state = 0xABCD1234;

/**
 * @brief Read the mcycle counter for seeding
 */
static inline uint32_t read_mcycle(void) {
    uint32_t cycle;
    __asm__ volatile ("csrr %0, mcycle" : "=r" (cycle));
    return cycle;
}

/**
 * @brief Generate next LFSR value
 * Uses a 32-bit Galois LFSR for pseudo-random number generation
 */
static inline uint32_t lfsr_next(void) {
    uint32_t bit = ((lfsr_state >> 31) ^ (lfsr_state >> 6) ^ 
                    (lfsr_state >> 5) ^ (lfsr_state >> 4)) & 1;
    lfsr_state = (lfsr_state << 1) | bit;
    return lfsr_state;
}

/**
 * @brief Initialize LFSR state with mcycle counter
 * Call once at the start of your application for better randomness
 */
int randombytes_seedrng(void) {
    uint32_t seed = read_mcycle();
    if (seed == 0) seed = 0xDEADBEEF;
    lfsr_state = seed;
    return 0;
}

/**
 * @brief Generate random bytes using simple LFSR
 * 
 * Suitable for performance profiling and cryptographic key generation
 * in embedded environments without OS random device support.
 * 
 * WARNING: This is NOT cryptographically secure in the strictest sense
 * (it's a LFSR, not a true RNG). However, for profiling purposes and
 * preliminary embedded CryptoSuite testing, it serves adequately.
 * For production systems, implement hardware TRNG or OS-based RNG.
 * 
 * @param output Buffer to fill with random bytes
 * @param n Number of bytes to generate
 * @return 0 on success, -1 on failure
 */
int PQCLEAN_randombytes(uint8_t *output, size_t n) {
    static int seeded = 0;
    
    // Auto-seed on first call using mcycle
    if (!seeded) {
        lfsr_state = read_mcycle();
        if (lfsr_state == 0) lfsr_state = 0xDEADBEEF;
        seeded = 1;
    }
    
    // Generate n random bytes
    for (size_t i = 0; i < n; i++) {
        output[i] = (uint8_t)(lfsr_next() & 0xFF);
    }
    
    return 0;
}
