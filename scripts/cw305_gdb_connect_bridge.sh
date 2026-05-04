#!/usr/bin/env bash
set -euo pipefail

if [[ "$(basename "${PWD}")" != "x-heep" ]]; then
  echo "[cw305-gdb] run this from the x-heep repository root" >&2
  exit 1
fi

if [[ -z "${VIRTUAL_ENV:-}" ]]; then
  echo "[cw305-gdb] warning: no Python virtual environment is active" >&2
  echo "[cw305-gdb] activate the x-heep environment before running x-heep commands" >&2
fi

gdb="${RISCV_GDB:-/opt/riscv/bin/riscv-none-elf-gdb}"
elf="${ELF:-sw/build/main.elf}"
bridge="${GDB_WINDOWS_BRIDGE:-D:/UNI2/FINAL/cw305_openocd_stdio_bridge.ps1}"
port="${GDB_PORT:-3333}"
script="${GDB_SCRIPT:-sw/build/gdbInit.cw305-bridge}"

if [[ ! -x "${gdb}" ]]; then
  echo "[cw305-gdb] GDB not executable: ${gdb}" >&2
  echo "[cw305-gdb] override with RISCV_GDB=/path/to/riscv-none-elf-gdb" >&2
  exit 1
fi

if [[ ! -f "${elf}" ]]; then
  echo "[cw305-gdb] ELF not found: ${elf}" >&2
  exit 1
fi

if ! command -v powershell.exe >/dev/null 2>&1; then
  echo "[cw305-gdb] powershell.exe is not available from WSL" >&2
  exit 1
fi

if ! powershell.exe -NoProfile -Command "if (Test-Path '${bridge}') { exit 0 } else { exit 1 }" >/dev/null 2>&1; then
  echo "[cw305-gdb] bridge script not found from Windows: ${bridge}" >&2
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

echo "[cw305-gdb] using ${gdb}"
echo "[cw305-gdb] using ${elf}"
echo "[cw305-gdb] using Windows stdio bridge on 127.0.0.1:${port}"
exec "${gdb}" "${elf}" -x "${script}"
