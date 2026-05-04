#!/usr/bin/env bash
set -euo pipefail

host="${GDB_HOST:-}"
port="${GDB_PORT:-3333}"
bridge="${GDB_WINDOWS_BRIDGE:-D:/UNI2/FINAL/cw305_openocd_stdio_bridge.ps1}"

if [[ "$(basename "${PWD}")" != "x-heep" ]]; then
  echo "[cw305-gdb] run this from the x-heep repository root"
  exit 1
fi

if [[ -z "${VIRTUAL_ENV:-}" ]]; then
  echo "[cw305-gdb] warning: no Python virtual environment is active"
  echo "[cw305-gdb] activate the x-heep environment before building or running x-heep commands"
fi

if [[ -z "${host}" ]]; then
  host="$(ip route 2>/dev/null | awk '/default/ {print $3; exit}')"
fi

if [[ -z "${host}" ]]; then
  host="localhost"
fi

echo "[cw305-gdb] Windows/OpenOCD host: ${host}:${port}"

if timeout 2 bash -c "</dev/tcp/${host}/${port}" 2>/dev/null; then
  echo "[cw305-gdb] reachable"
  echo "[cw305-gdb] next: scripts/cw305_gdb_connect_bridge.sh"
elif command -v powershell.exe >/dev/null 2>&1 &&
     powershell.exe -NoProfile -Command "if ((Test-Path '${bridge}') -and (Test-NetConnection -ComputerName 127.0.0.1 -Port ${port} -InformationLevel Quiet)) { exit 0 } else { exit 1 }" >/dev/null 2>&1; then
  echo "[cw305-gdb] direct WSL TCP is blocked, but Windows stdio bridge is reachable"
  echo "[cw305-gdb] next: scripts/cw305_gdb_connect_bridge.sh"
else
  echo "[cw305-gdb] unreachable"
  echo "[cw305-gdb] start Windows monitor first:"
  echo "[cw305-gdb]   cd D:\\UNI2\\FINAL"
  echo "[cw305-gdb]   python monitor.py"
  echo "[cw305-gdb] then check Windows PowerShell:"
  echo "[cw305-gdb]   Get-NetTCPConnection -LocalPort ${port}"
  exit 1
fi
