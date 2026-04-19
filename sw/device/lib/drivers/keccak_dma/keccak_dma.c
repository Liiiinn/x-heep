#include "keccak_dma.h"
#include "keccak_dma_regs.h"

void keccak_dma_init(keccak_dma_t *keccak, uintptr_t base_addr) {
    keccak->base_addr = mmio_region_from_addr(base_addr);
}

keccak_dma_result_t keccak_dma_start(const keccak_dma_t *keccak,
                                     uintptr_t src_addr,
                                     uintptr_t dst_addr,
                                     uint32_t data_len_bytes) {
    if ((data_len_bytes == 0u) || (data_len_bytes > KECCAK_DMA_MAX_BYTES)) {
        return kKeccakDmaBadLen;
    }

    mmio_region_write32(keccak->base_addr, KECCAK_DMA_SRC_ADDR_REG_OFFSET,
                        (uint32_t)src_addr);
    mmio_region_write32(keccak->base_addr, KECCAK_DMA_DST_ADDR_REG_OFFSET,
                        (uint32_t)dst_addr);
    mmio_region_write32(keccak->base_addr, KECCAK_DMA_DATA_LEN_REG_OFFSET,
                        data_len_bytes);

    // Use a 0->1 write sequence so repeated operations always generate a start edge.
    mmio_region_write32(keccak->base_addr, KECCAK_DMA_CTRL_REG_OFFSET, 0u);
    mmio_region_write32(keccak->base_addr, KECCAK_DMA_CTRL_REG_OFFSET, 1u);

    return kKeccakDmaOk;
}

uint32_t keccak_dma_get_status(const keccak_dma_t *keccak) {
    return mmio_region_read32(keccak->base_addr, KECCAK_DMA_STATUS_REG_OFFSET);
}

bool keccak_dma_is_busy(const keccak_dma_t *keccak) {
    return (keccak_dma_get_status(keccak) >> KECCAK_DMA_STATUS_BUSY_BIT) & 0x1u;
}

bool keccak_dma_is_done(const keccak_dma_t *keccak) {
    return (keccak_dma_get_status(keccak) >> KECCAK_DMA_STATUS_DONE_BIT) & 0x1u;
}

bool keccak_dma_has_error(const keccak_dma_t *keccak) {
    return (keccak_dma_get_status(keccak) >> KECCAK_DMA_STATUS_ERROR_BIT) & 0x1u;
}

keccak_dma_result_t keccak_dma_wait(const keccak_dma_t *keccak,
                                    uint32_t timeout_cycles) {
    while (timeout_cycles > 0u) {
        const uint32_t status = keccak_dma_get_status(keccak);
        if ((status >> KECCAK_DMA_STATUS_ERROR_BIT) & 0x1u) {
            return kKeccakDmaError;
        }
        if ((status >> KECCAK_DMA_STATUS_DONE_BIT) & 0x1u) {
        return kKeccakDmaOk;
        }
        timeout_cycles--;
    }

    return kKeccakDmaTimeout;
}

keccak_dma_result_t keccak_dma_hash_block(const keccak_dma_t *keccak,
                                          uintptr_t src_addr,
                                          uintptr_t dst_addr,
                                          uint32_t timeout_cycles) {
    keccak_dma_result_t ret = keccak_dma_start(keccak, src_addr, dst_addr, KECCAK_DMA_BLOCK_BYTES);
    if (ret != kKeccakDmaOk) {
        return ret;
    }
    return keccak_dma_wait(keccak, timeout_cycles);
}
