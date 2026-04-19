#ifndef _DRIVERS_KECCAK_DMA_H_
#define _DRIVERS_KECCAK_DMA_H_

#include <stdbool.h>
#include <stdint.h>

#include "core_v_mini_mcu.h"
#include "mmio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KECCAK_DMA_START_ADDRESS (EXT_PERIPHERAL_START_ADDRESS + 0x06000u)
#define KECCAK_DMA_INTR_NUM EXT_INTR_3
#define KECCAK_DMA_MAX_BYTES 200u
#define KECCAK_DMA_BLOCK_BYTES KECCAK_DMA_MAX_BYTES

typedef enum keccak_dma_result {
    kKeccakDmaOk = 0,
    kKeccakDmaTimeout = -1,
    kKeccakDmaError = -2,
    kKeccakDmaBadLen = -3,
} keccak_dma_result_t;

typedef struct keccak_dma {
    mmio_region_t base_addr;
} keccak_dma_t;

void keccak_dma_init(keccak_dma_t *keccak, uintptr_t base_addr);
keccak_dma_result_t keccak_dma_start(const keccak_dma_t *keccak,
                                     uintptr_t src_addr,
                                     uintptr_t dst_addr,
                                     uint32_t data_len_bytes);
uint32_t keccak_dma_get_status(const keccak_dma_t *keccak);
bool keccak_dma_is_busy(const keccak_dma_t *keccak);
bool keccak_dma_is_done(const keccak_dma_t *keccak);
bool keccak_dma_has_error(const keccak_dma_t *keccak);
keccak_dma_result_t keccak_dma_wait(const keccak_dma_t *keccak,
                                    uint32_t timeout_cycles);
keccak_dma_result_t keccak_dma_hash_block(const keccak_dma_t *keccak,
                                          uintptr_t src_addr,
                                          uintptr_t dst_addr,
                                          uint32_t timeout_cycles);

#ifdef __cplusplus
}
#endif

#endif  // _DRIVERS_KECCAK_DMA_H_
