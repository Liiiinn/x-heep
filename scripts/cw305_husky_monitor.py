#!/usr/bin/env python3
"""
CW305 x-heep UART monitor via ChipWhisperer Husky.

Uses the Husky's built-in USART (20-pin IO1/IO2) to read printf output from
the x-heep RISC-V core running on the CW305 Artix-7 FPGA, without needing
an external USB-UART converter.

Pin mapping (20-pin connector):
  IO1 / FPGA P16  = uart_tx_o  (Husky reads via tio1 = serial_rx)
  IO2 / FPGA R16  = uart_rx_i  (Husky writes via tio2 = serial_tx)

Usage:
  # Monitor only (FPGA already programmed):
  python cw305_husky_monitor.py

  # Program FPGA then monitor:
  python cw305_husky_monitor.py --bsfile build/.../....bit

  # With logging:
  python cw305_husky_monitor.py --bsfile build/.../....bit --log run.log
"""

import argparse
import sys
import time

import chipwhisperer as cw

DEFAULT_BAUD = 9600   # matches UART_BAUDRATE in sw/device/target/cw305/x-heep.h
READ_CHUNK   = 64     # bytes per poll
POLL_MS      = 50     # poll interval in milliseconds


def uart_text(raw) -> str:
    if isinstance(raw, str):
        return raw.replace("\x00", "")
    if isinstance(raw, (bytes, bytearray)):
        return raw.replace(b"\x00", b"").decode("latin-1", errors="replace")
    return "".join(chr(b) for b in raw if b)


def uart_bytes(raw) -> bytes:
    if isinstance(raw, str):
        return raw.encode("latin-1", errors="replace")
    if isinstance(raw, (bytes, bytearray)):
        return bytes(raw)
    return bytes(b for b in raw if b is not None)


def is_ascii_uart(raw: bytes) -> bool:
    raw = raw.replace(b"\x00", b"")
    if not raw:
        return False
    return all(b in b"\r\n\t" or 0x20 <= b <= 0x7e for b in raw)


def write_console(text: str) -> None:
    try:
        sys.stdout.write(text)
    except UnicodeEncodeError:
        encoding = sys.stdout.encoding or "utf-8"
        safe = text.encode(encoding, errors="backslashreplace").decode(encoding, errors="replace")
        sys.stdout.write(safe)
    sys.stdout.flush()


def program_fpga(scope, bsfile: str) -> None:
    print(f"[husky] Programming CW305 with {bsfile} ...")
    target_cw305 = cw.target(scope, cw.targets.CW305, bsfile=bsfile, force=True)
    # Disconnect the CW305 USB register interface — UART path via 20-pin stays active.
    target_cw305.dis()
    print("[husky] Bitstream loaded.")
    # Give the FPGA a moment to come out of reset.
    time.sleep(0.2)


def monitor(scope, baud: int, log_path: str | None, raw_uart: bool, uart_rx_tio: str) -> None:
    # SimpleSerial target uses scope's internal USART over tio1/tio2.
    # tio1 defaults to "serial_rx" (reads IO1/P16 = uart_tx_o).
    # tio2 defaults to "serial_tx" (drives IO2/R16 = uart_rx_i).
    if uart_rx_tio == "tio2":
        scope.io.tio1 = "serial_tx"
        scope.io.tio2 = "serial_rx"
    else:
        scope.io.tio1 = "serial_rx"
        scope.io.tio2 = "serial_tx"
    target = cw.target(scope)
    target.baud = baud
    target.flush()

    log_f = open(log_path, "w", buffering=1, encoding="utf-8", errors="backslashreplace") if log_path else None
    print(f"[husky-uart] {baud} baud — press Ctrl+C to stop")
    if log_path:
        print(f"[husky-uart] logging to {log_path}")

    try:
        while True:
            raw = target.read(READ_CHUNK, timeout=POLL_MS)
            if raw:
                raw_b = uart_bytes(raw)
                if not raw_uart and not is_ascii_uart(raw_b):
                    continue
                text = uart_text(raw)
                if log_f:
                    log_f.write(text)
                write_console(text)
            else:
                time.sleep(POLL_MS / 1000)
    except KeyboardInterrupt:
        print("\n[husky-uart] stopped.")
    finally:
        if log_f:
            log_f.close()
        target.dis()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Monitor x-heep UART printf output via ChipWhisperer Husky"
    )
    parser.add_argument(
        "--bsfile",
        metavar="FILE",
        help="Path to .bit bitstream (omit to skip programming)",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help=f"UART baud rate (default: {DEFAULT_BAUD})",
    )
    parser.add_argument(
        "--log",
        metavar="FILE",
        help="Also write UART output to this log file",
    )
    parser.add_argument(
        "--raw-uart",
        action="store_true",
        help="Print raw UART data even when it contains non-ASCII bytes",
    )
    parser.add_argument(
        "--uart-rx-tio",
        choices=("tio1", "tio2"),
        default="tio1",
        help="Husky TIO pin used as UART RX from CW305 (default: tio1)",
    )
    args = parser.parse_args()

    scope = cw.scope()
    scope.default_setup()

    try:
        if args.bsfile:
            program_fpga(scope, args.bsfile)
        monitor(scope, args.baud, args.log, args.raw_uart, args.uart_rx_tio)
    finally:
        scope.dis()


if __name__ == "__main__":
    main()
