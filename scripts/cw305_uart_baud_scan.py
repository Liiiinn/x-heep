#!/usr/bin/env python3
import argparse
import string
import sys
import time

import chipwhisperer as cw


def to_bytes(raw) -> bytes:
    if raw is None:
        return b""
    if isinstance(raw, str):
        return raw.encode("latin-1", errors="replace")
    if isinstance(raw, (bytes, bytearray)):
        return bytes(raw)
    return bytes(b for b in raw if b is not None)


def printable_score(data: bytes) -> int:
    allowed = set((string.ascii_letters + string.digits + string.punctuation + " \r\n\t").encode())
    return sum(1 for b in data if b in allowed)


def render(data: bytes) -> str:
    text = data.replace(b"\x00", b"").decode("latin-1", errors="replace")
    return "".join(ch if ch in string.printable else "." for ch in text)


def main() -> None:
    parser = argparse.ArgumentParser(description="Scan Husky UART baud rates for CW305 output")
    parser.add_argument("--bauds", nargs="+", type=int, default=[4800, 9600, 12000, 19200, 38400, 57600, 115200])
    parser.add_argument("--seconds", type=float, default=2.0)
    parser.add_argument("--chunk", type=int, default=128)
    args = parser.parse_args()

    scope = cw.scope()
    scope.default_setup()
    scope.io.tio1 = "serial_rx"
    scope.io.tio2 = "serial_tx"

    try:
        target = cw.target(scope)
        for baud in args.bauds:
            target.baud = baud
            target.flush()
            time.sleep(0.1)
            end = time.monotonic() + args.seconds
            data = bytearray()
            while time.monotonic() < end:
                raw = target.read(args.chunk, timeout=50)
                data.extend(to_bytes(raw))
            sample = bytes(data[:80])
            score = printable_score(sample)
            print(f"[baud {baud:6d}] bytes={len(data):4d} printable={score:3d}/{len(sample):3d} hex={sample.hex(' ')}")
            print(f"             text={render(sample)}")
    finally:
        try:
            target.dis()
        except Exception:
            pass
        scope.dis()


if __name__ == "__main__":
    main()
