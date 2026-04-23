#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
  echo "Usage: $0 <serial-device> [baud=9600] [log-file=cw305_uart.log]"
  echo "Example: $0 /dev/ttyUSB2 9600 hello_world_run1.log"
  exit 1
fi

SERIAL_DEV="$1"
BAUD="${2:-9600}"
LOG_FILE="${3:-cw305_uart.log}"

echo "[cw305-uart] listening on ${SERIAL_DEV} at ${BAUD} baud"
echo "[cw305-uart] writing log to ${LOG_FILE}"

picocom -b "${BAUD}" -r -l --imap lfcrlf "${SERIAL_DEV}" | tee "${LOG_FILE}"
