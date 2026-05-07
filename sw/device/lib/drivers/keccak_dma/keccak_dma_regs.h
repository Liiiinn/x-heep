// Generated register defines for keccak

#ifndef _KECCAK_REG_DEFS_
#define _KECCAK_REG_DEFS_

#ifdef __cplusplus
extern "C" {
#endif
// Register width
#define KECCAK_PARAM_REG_WIDTH 32

// Source base address in system memory for DMA reads
#define KECCAK_SRC_ADDR_REG_OFFSET 0x0

// Destination base address in system memory for DMA writes
#define KECCAK_DST_ADDR_REG_OFFSET 0x4

// Number of bytes to transfer/process
#define KECCAK_DATA_LEN_REG_OFFSET 0x8

// Control register for DMA Keccak transaction
#define KECCAK_CTRL_REG_OFFSET 0xc
#define KECCAK_CTRL_START_BIT 0

// Status register for DMA Keccak transaction
#define KECCAK_STATUS_REG_OFFSET 0x10
#define KECCAK_STATUS_DONE_BIT 0
#define KECCAK_STATUS_BUSY_BIT 1
#define KECCAK_STATUS_ERROR_BIT 2

// Cycles from accepted start to transaction completion or error
#define KECCAK_LAST_OP_CYCLES_REG_OFFSET 0x14

// Keccak permutation core active cycles in the last transaction
#define KECCAK_LAST_CORE_CYCLES_REG_OFFSET 0x18

// Number of completed or errored hardware transactions
#define KECCAK_OP_COUNT_REG_OFFSET 0x1c

// Compatibility aliases used by the Keccak DMA driver.
#define KECCAK_DMA_PARAM_REG_WIDTH KECCAK_PARAM_REG_WIDTH
#define KECCAK_DMA_WORD_BYTES 4u
#define KECCAK_DMA_DATA_LEN_MAX_BYTES 256u
#define KECCAK_DMA_SRC_ADDR_REG_OFFSET KECCAK_SRC_ADDR_REG_OFFSET
#define KECCAK_DMA_DST_ADDR_REG_OFFSET KECCAK_DST_ADDR_REG_OFFSET
#define KECCAK_DMA_DATA_LEN_REG_OFFSET KECCAK_DATA_LEN_REG_OFFSET
#define KECCAK_DMA_CTRL_REG_OFFSET KECCAK_CTRL_REG_OFFSET
#define KECCAK_DMA_CTRL_START_BIT KECCAK_CTRL_START_BIT
#define KECCAK_DMA_STATUS_REG_OFFSET KECCAK_STATUS_REG_OFFSET
#define KECCAK_DMA_STATUS_DONE_BIT KECCAK_STATUS_DONE_BIT
#define KECCAK_DMA_STATUS_BUSY_BIT KECCAK_STATUS_BUSY_BIT
#define KECCAK_DMA_STATUS_ERROR_BIT KECCAK_STATUS_ERROR_BIT
#define KECCAK_DMA_LAST_OP_CYCLES_REG_OFFSET KECCAK_LAST_OP_CYCLES_REG_OFFSET
#define KECCAK_DMA_LAST_CORE_CYCLES_REG_OFFSET KECCAK_LAST_CORE_CYCLES_REG_OFFSET
#define KECCAK_DMA_OP_COUNT_REG_OFFSET KECCAK_OP_COUNT_REG_OFFSET

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // _KECCAK_REG_DEFS_
// End generated register defines for keccak
