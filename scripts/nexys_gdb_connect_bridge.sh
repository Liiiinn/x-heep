#!/usr/bin/env bash
set -euo pipefail

if [[ "$(basename "${PWD}")" != "x-heep" ]]; then
  echo "[nexys-gdb] run this from the x-heep repository root" >&2
  exit 1
fi

if [[ -z "${VIRTUAL_ENV:-}" ]]; then
  echo "[nexys-gdb] warning: no Python virtual environment is active" >&2
fi

gdb="${RISCV_GDB:-/opt/riscv/bin/riscv-none-elf-gdb}"
elf="${ELF:-sw/build/main.elf}"
bridge="${GDB_WINDOWS_BRIDGE:-D:/UNI2/FINAL/nexys_openocd_stdio_bridge.ps1}"
port="${GDB_PORT:-13333}"
script="${GDB_SCRIPT:-sw/build/gdbInit.nexys-bridge}"

if [[ ! -x "${gdb}" ]]; then
  echo "[nexys-gdb] GDB not executable: ${gdb}" >&2
  exit 1
fi

if [[ ! -f "${elf}" ]]; then
  echo "[nexys-gdb] ELF not found: ${elf}" >&2
  exit 1
fi

mkdir -p "$(dirname "${script}")"
cat > "${script}" <<EOF
set remotetimeout 2000
target extended-remote | powershell.exe -NoProfile -ExecutionPolicy Bypass -File ${bridge} -HostName 127.0.0.1 -Port ${port}
load
i r pc
continue
EOF

echo "[nexys-gdb] using ${gdb}"
echo "[nexys-gdb] using ${elf}"
exec "${gdb}" "${elf}" -x "${script}"
