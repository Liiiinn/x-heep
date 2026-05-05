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
release="${GDB_RELEASE_SCRIPT:-D:/UNI2/FINAL/nexys_release_gdb_port_windows.ps1}"
auto_continue="${GDB_AUTO_CONTINUE:-1}"

if [[ ! -x "${gdb}" ]]; then
  echo "[nexys-gdb] GDB not executable: ${gdb}" >&2
  exit 1
fi

if [[ ! -f "${elf}" ]]; then
  echo "[nexys-gdb] ELF not found: ${elf}" >&2
  exit 1
fi

if [[ "${GDB_RELEASE_STALE:-1}" != "0" ]]; then
  echo "[nexys-gdb] releasing stale Windows GDB bridge clients on port ${port}"
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${release}" -Port "${port}" >&2 || true
fi

mkdir -p "$(dirname "${script}")"
cat > "${script}" <<EOF
set pagination off
set confirm off
set remotetimeout 2000
target extended-remote | powershell.exe -NoProfile -ExecutionPolicy Bypass -File ${bridge} -HostName 127.0.0.1 -Port ${port}
load
i r pc
EOF

if [[ "${auto_continue}" != "0" ]]; then
  cat >> "${script}" <<EOF
continue
EOF
else
  cat >> "${script}" <<EOF
echo \\n[nexys-gdb] loaded ELF and left target halted. Run 'continue' when ready.\\n
EOF
fi

echo "[nexys-gdb] using ${gdb}"
echo "[nexys-gdb] using ${elf}"
echo "[nexys-gdb] using OpenOCD port ${port}"
if [[ "${auto_continue}" == "0" ]]; then
  echo "[nexys-gdb] auto-continue disabled; target will stay halted after load"
fi
exec "${gdb}" "${elf}" -x "${script}"
