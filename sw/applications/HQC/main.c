/**
 * @file main.c
 * @brief HQC-128 Performance Profiling Framework
 * Week 2: Baseline characterization with mcycle counter
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "api.h"

// ============================================================================
// TODO 1: Define mcycle read function
// ============================================================================
// Hints:
// - Use inline asm "rdcycle" instruction for 64-bit RISC-V
// - Handle 32-bit overflow if needed (rdcycle + rdcycleh)
// static inline uint64_t get_mcycle(void) { ... }


// ============================================================================
// TODO 2: Define performance metrics structure
// ============================================================================
// Should track:
// - keygen_cycles
// - encaps_cycles  
// - decaps_cycles
// typedef struct { ... } perf_stats_t;


// ============================================================================
// TODO 3: Utility functions (optional but helpful)
// ============================================================================
// - print_cycles(label, cycles) - print with ms estimation
// - compare_arrays(a, b, len, name) - verify correctness


int main(void) {
    // ========================================================================
    // STEP 1: Print header/banner
    // ========================================================================
    printf("\n=== HQC-128 Profiling on X-HEEP ===\n\n");

    // ========================================================================
    // STEP 2: KeyGen profiling
    // ========================================================================
    // TODO:
    // - Allocate pk[], sk[] buffers (correct sizes from api.h)
    // - Read mcycle BEFORE
    // - Call crypto_kem_keypair(pk, sk)
    // - Read mcycle AFTER
    // - Calculate delta_cycles
    // - Store in stats structure
    // - Print result

    // ========================================================================
    // STEP 3: Encapsulation profiling
    // ========================================================================
    // TODO:
    // - Allocate ct[], ss_encaps[] buffers
    // - Read mcycle BEFORE
    // - Call crypto_kem_enc(ct, ss_encaps, pk)
    // - Read mcycle AFTER
    // - Store cycles
    // - Print result

    // ========================================================================
    // STEP 4: Decapsulation profiling
    // ========================================================================
    // TODO:
    // - Allocate ss_decaps[] buffer
    // - Read mcycle BEFORE
    // - Call crypto_kem_dec(ss_decaps, ct, sk)
    // - Read mcycle AFTER
    // - Store cycles
    // - Print result

    // ========================================================================
    // STEP 5: Correctness check
    // ========================================================================
    // TODO:
    // - Compare ss_encaps[] vs ss_decaps[]
    // - Should be identical
    // - Print PASS/FAIL

    // ========================================================================
    // STEP 6: Performance summary
    // ========================================================================
    // TODO:
    // - Print all three times
    // - Calculate percentages for pie chart
    // - Print architectural insights

    printf("\n=== Profiling Complete ===\n\n");
    
    return 0;
}
