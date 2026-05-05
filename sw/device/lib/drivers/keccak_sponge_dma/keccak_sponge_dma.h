#ifndef _DRIVERS_KECCAK_SPONGE_DMA_H_
#define _DRIVERS_KECCAK_SPONGE_DMA_H_

#include <stdbool.h>
#include <stdint.h>

#include "core_v_mini_mcu.h"
#include "mmio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KECCAK_SPONGE_DMA_START_ADDRESS (EXT_PERIPHERAL_START_ADDRESS + 0x07000u)
#define KECCAK_SPONGE_DMA_INTR_NUM EXT_INTR_3
#define KECCAK_SPONGE_DMA_SHAKE256_RATE_BYTES 136u
#define KECCAK_SPONGE_DMA_ADDR_ALIGN_BYTES 4u

typedef enum keccak_sponge_dma_result {
    kKeccakSpongeDmaOk = 0,
    kKeccakSpongeDmaTimeout = -1,
    kKeccakSpongeDmaError = -2,
    kKeccakSpongeDmaBadLen = -3,
    kKeccakSpongeDmaBadAddr = -4,
} keccak_sponge_dma_result_t;

typedef struct keccak_sponge_dma {
    mmio_region_t base_addr;
} keccak_sponge_dma_t;

void keccak_sponge_dma_init(keccak_sponge_dma_t *keccak, uintptr_t base_addr);
uint32_t keccak_sponge_dma_get_status(const keccak_sponge_dma_t *keccak);
bool keccak_sponge_dma_is_busy(const keccak_sponge_dma_t *keccak);
bool keccak_sponge_dma_is_done(const keccak_sponge_dma_t *keccak);
bool keccak_sponge_dma_has_error(const keccak_sponge_dma_t *keccak);
keccak_sponge_dma_result_t keccak_sponge_dma_wait(const keccak_sponge_dma_t *keccak,
                                                  uint32_t timeout_cycles);
keccak_sponge_dma_result_t keccak_sponge_dma_shake256(const keccak_sponge_dma_t *keccak,
                                                      uintptr_t src_addr,
                                                      uint32_t src_len_bytes,
                                                      uintptr_t dst_addr,
                                                      uint32_t dst_len_bytes,
                                                      uint8_t domain,
                                                      uint32_t timeout_cycles);

#ifdef __cplusplus
}
#endif

#endif  // _DRIVERS_KECCAK_SPONGE_DMA_H_
