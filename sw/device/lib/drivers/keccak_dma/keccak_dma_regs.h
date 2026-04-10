// Copyright 2026 OpenHW Group
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#ifndef _KECCAK_DMA_REG_DEFS_
#define _KECCAK_DMA_REG_DEFS_

#ifdef __cplusplus
extern "C" {
#endif

#define KECCAK_DMA_PARAM_REG_WIDTH 32

#define KECCAK_DMA_SRC_ADDR_REG_OFFSET 0x0
#define KECCAK_DMA_DST_ADDR_REG_OFFSET 0x4
#define KECCAK_DMA_DATA_LEN_REG_OFFSET 0x8
#define KECCAK_DMA_CTRL_REG_OFFSET 0xc
#define KECCAK_DMA_CTRL_START_BIT 0

#define KECCAK_DMA_STATUS_REG_OFFSET 0x10
#define KECCAK_DMA_STATUS_DONE_BIT 0
#define KECCAK_DMA_STATUS_BUSY_BIT 1
#define KECCAK_DMA_STATUS_ERROR_BIT 2

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _KECCAK_DMA_REG_DEFS_
