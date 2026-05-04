#!/usr/bin/env bash
set -euo pipefail

if [[ "$(basename "${PWD}")" != "x-heep" ]]; then
  echo "[nexys-bscan-build] run this from the x-heep repository root" >&2
  exit 1
fi

if [[ -z "${VIRTUAL_ENV:-}" ]]; then
  echo "[nexys-bscan-build] warning: no Python virtual environment is active" >&2
fi

board="${FPGA_BOARD:-nexys-a7-100t}"
flags="${FUSESOC_FLAGS:-}"
if [[ "${flags}" != *"--flag=use_bscane_xilinx"* ]]; then
  flags="${flags} --flag=use_bscane_xilinx"
fi

echo "[nexys-bscan-build] FPGA_BOARD=${board}"
echo "[nexys-bscan-build] FUSESOC_FLAGS=${flags}"

# 编译（注意：不再用 exec）
make vivado-fpga FPGA_BOARD="${board}" FUSESOC_FLAGS="${flags}"

# bit 文件路径
bitfile=$(find build -name "*.bit" | head -n 1)

# Windows 目标路径
dst="/mnt/d/UNI2/FINAL"

echo "[nexys-bscan-build] copying bitstream..."

mkdir -p "${dst}"
cp "${bitfile}" "${dst}/"

echo "[nexys-bscan-build] done:"
echo "  -> ${dst}/$(basename "${bitfile}")"
