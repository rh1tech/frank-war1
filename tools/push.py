#!/usr/bin/env python3
"""push.py <local file> <path on the card> — send a file to the master's
microSD over its UART console (console command `rxfile`).

Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
SPDX-License-Identifier: GPL-2.0-or-later
"""
import os
import sys
import time
import zlib

import serial

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uart import conf, tty_for  # noqa: E402


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    port = serial.Serial(tty_for(conf()["MASTER_PROBE"]), 115200, timeout=5)
    port.reset_input_buffer()
    port.write(f"rxfile {len(data)} {dst}\r".encode())
    buf = b""
    t = time.time() + 10
    while b"rxfile: ready" not in buf and time.time() < t:
        buf += port.read(port.in_waiting or 1)
    if b"rxfile: ready" not in buf:
        sys.exit("push: the board did not answer (is the game running?)")
    while not buf.endswith(b"\n"):
        buf += port.read(1)
    t0 = time.time()
    for i in range(0, len(data), 512):
        port.write(data[i:i + 512])
        ack = port.read(1)
        while ack in (b"\r", b"\n", b" "):
            ack = port.read(1)
        if ack != b".":
            sys.exit(f"push: no ack at byte {i} (got {ack!r})")
        sys.stdout.write(f"\r{i + 512 if i + 512 < len(data) else len(data)}/{len(data)} bytes")
        sys.stdout.flush()
    tail = b""
    t = time.time() + 5
    while b"crc" not in tail and time.time() < t:
        tail += port.read(port.in_waiting or 1)
    tail += port.read(40)
    crc = zlib.crc32(data) & 0xffffffff
    ok = f"crc {crc:08x}".encode() in tail
    print(f"\npush: {dst} {len(data)} bytes in {time.time() - t0:.1f} s, crc {crc:08x} {'OK' if ok else 'MISMATCH'}")
    sys.exit(0 if ok else 1)


main()
