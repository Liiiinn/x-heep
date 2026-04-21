#ifndef HQC_PHASE2_SHAKE_BACKEND_H_
#define HQC_PHASE2_SHAKE_BACKEND_H_

#include <stdint.h>
#include "keccak_dma.h"

typedef enum {
    HQC_PHASE2_SHAKE_BACKEND_P1 = 1,
    HQC_PHASE2_SHAKE_BACKEND_P2 = 2,
} hqc_phase2_shake_backend_t;

void hqc_phase2_shake_backend_init(keccak_dma_t *keccak,
                                   uintptr_t state_addr,
                                   uintptr_t output_addr);
void hqc_phase2_set_shake_backend(hqc_phase2_shake_backend_t backend);
hqc_phase2_shake_backend_t hqc_phase2_get_shake_backend(void);

#endif
