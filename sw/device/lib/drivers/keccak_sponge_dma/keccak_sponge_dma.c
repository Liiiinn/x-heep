#include "keccak_sponge_dma.h"
#include "keccak_sponge_dma_regs.h"

void keccak_sponge_dma_init(keccak_sponge_dma_t *keccak, uintptr_t base_addr) {
    keccak->base_addr = mmio_region_from_addr(base_addr);
}

uint32_t keccak_sponge_dma_get_status(const keccak_sponge_dma_t *keccak) {
    return mmio_region_read32(keccak->base_addr, KECCAK_SPONGE_DMA_STATUS_REG_OFFSET);
}

uint32_t keccak_sponge_dma_get_last_op_cycles(const keccak_sponge_dma_t *keccak) {
    return mmio_region_read32(keccak->base_addr, KECCAK_SPONGE_DMA_LAST_OP_CYCLES_REG_OFFSET);
}

uint32_t keccak_sponge_dma_get_last_core_cycles(const keccak_sponge_dma_t *keccak) {
    return mmio_region_read32(keccak->base_addr, KECCAK_SPONGE_DMA_LAST_CORE_CYCLES_REG_OFFSET);
}

uint32_t keccak_sponge_dma_get_op_count(const keccak_sponge_dma_t *keccak) {
    return mmio_region_read32(keccak->base_addr, KECCAK_SPONGE_DMA_OP_COUNT_REG_OFFSET);
}

uint32_t keccak_sponge_dma_get_last_core_perms(const keccak_sponge_dma_t *keccak) {
    return mmio_region_read32(keccak->base_addr, KECCAK_SPONGE_DMA_LAST_CORE_PERMS_REG_OFFSET);
}

bool keccak_sponge_dma_is_busy(const keccak_sponge_dma_t *keccak) {
    return (keccak_sponge_dma_get_status(keccak) >> KECCAK_SPONGE_DMA_STATUS_BUSY_BIT) & 0x1u;
}

bool keccak_sponge_dma_is_done(const keccak_sponge_dma_t *keccak) {
    return (keccak_sponge_dma_get_status(keccak) >> KECCAK_SPONGE_DMA_STATUS_DONE_BIT) & 0x1u;
}

bool keccak_sponge_dma_has_error(const keccak_sponge_dma_t *keccak) {
    return (keccak_sponge_dma_get_status(keccak) >> KECCAK_SPONGE_DMA_STATUS_ERROR_BIT) & 0x1u;
}

keccak_sponge_dma_result_t keccak_sponge_dma_wait(const keccak_sponge_dma_t *keccak,
                                                  uint32_t timeout_cycles) {
    while (timeout_cycles > 0u) {
        const uint32_t status = keccak_sponge_dma_get_status(keccak);
        if ((status >> KECCAK_SPONGE_DMA_STATUS_ERROR_BIT) & 0x1u) {
            return kKeccakSpongeDmaError;
        }
        if ((status >> KECCAK_SPONGE_DMA_STATUS_DONE_BIT) & 0x1u) {
            return kKeccakSpongeDmaOk;
        }
        timeout_cycles--;
    }

    return kKeccakSpongeDmaTimeout;
}

keccak_sponge_dma_result_t keccak_sponge_dma_shake256(const keccak_sponge_dma_t *keccak,
                                                      uintptr_t src_addr,
                                                      uint32_t src_len_bytes,
                                                      uintptr_t dst_addr,
                                                      uint32_t dst_len_bytes,
                                                      uint8_t domain,
                                                      uint32_t timeout_cycles) {
    if (dst_len_bytes == 0u) {
        return kKeccakSpongeDmaBadLen;
    }
    if (((src_addr | dst_addr) & (KECCAK_SPONGE_DMA_ADDR_ALIGN_BYTES - 1u)) != 0u) {
        return kKeccakSpongeDmaBadAddr;
    }

    mmio_region_write32(keccak->base_addr, KECCAK_SPONGE_DMA_SRC_ADDR_REG_OFFSET,
                        (uint32_t)src_addr);
    mmio_region_write32(keccak->base_addr, KECCAK_SPONGE_DMA_DST_ADDR_REG_OFFSET,
                        (uint32_t)dst_addr);
    mmio_region_write32(keccak->base_addr, KECCAK_SPONGE_DMA_DATA_LEN_REG_OFFSET,
                        src_len_bytes);
    mmio_region_write32(keccak->base_addr, KECCAK_SPONGE_DMA_OUT_LEN_REG_OFFSET,
                        dst_len_bytes);
    mmio_region_write32(keccak->base_addr, KECCAK_SPONGE_DMA_DOMAIN_REG_OFFSET,
                        (uint32_t)domain);

    mmio_region_write32(keccak->base_addr, KECCAK_SPONGE_DMA_CTRL_REG_OFFSET, 0u);
    mmio_region_write32(keccak->base_addr, KECCAK_SPONGE_DMA_CTRL_REG_OFFSET, 1u);

    return keccak_sponge_dma_wait(keccak, timeout_cycles);
}
