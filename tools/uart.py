#!/usr/bin/env python3
"""uart.py — read / write a half's console by role.

  uart.py <master|slave> capture [--seconds N] [--until REGEX]
  uart.py <master|slave> send "text\\r"  [--seconds N]

The probe's CDC port is found by the probe's USB serial number (bench.conf),
never by /dev name. Exit status 0 when --until matched (or no --until),
1 on timeout.

Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))


def conf():
    out = {}
    for line in open(os.path.join(HERE, "bench.conf")):
        line = line.split("#", 1)[0].strip()
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip().strip('"')
    return out


def tty_for(serial_no):
    # macOS names CDC ports after the USB location; map serial -> location.
    out = subprocess.run(["ioreg", "-p", "IOUSB", "-l", "-w0"],
                         capture_output=True, text=True).stdout
    loc = None
    cur_loc = None
    for line in out.splitlines():
        m = re.search(r'"locationID" = (\d+)', line)
        if m:
            cur_loc = int(m.group(1))
        m = re.search(r'"USB Serial Number" = "([^"]+)"', line)
        if m and m.group(1) == serial_no:
            loc = cur_loc
    if loc is None:
        sys.exit(f"probe {serial_no} not found on USB")
    # cu.usbmodem<hex location prefix><interface>
    prefix = "%x" % loc
    prefix = prefix.rstrip("0")
    cands = [p for p in glob.glob("/dev/cu.usbmodem*")
             if p[len("/dev/cu.usbmodem"):].startswith(prefix)]
    if len(cands) != 1:
        sys.exit(f"cannot pick tty for {serial_no}: {cands}")
    return cands[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("role", choices=["master", "slave"])
    ap.add_argument("cmd", choices=["capture", "send", "tty"])
    ap.add_argument("text", nargs="?", default="")
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--until")
    ap.add_argument("--baud", type=int, default=115200)
    a = ap.parse_args()

    c = conf()
    tty = tty_for(c["MASTER_PROBE" if a.role == "master" else "SLAVE_PROBE"])
    if a.cmd == "tty":
        print(tty)
        return 0
    s = serial.Serial(tty, a.baud, timeout=0.1)
    if a.cmd == "send":
        s.write(a.text.encode().decode("unicode_escape").encode("latin-1"))
        s.flush()
    pat = re.compile(a.until) if a.until else None
    buf = b""
    t_end = time.time() + a.seconds
    t0 = time.time()
    while time.time() < t_end:
        d = s.read(4096)
        if d:
            buf += d
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                txt = line.decode("utf-8", "replace").rstrip("\r")
                print(f"[{time.time() - t0:7.3f}] {txt}", flush=True)
                if pat and pat.search(txt):
                    return 0
    if buf:
        print(f"[{time.time() - t0:7.3f}] {buf.decode('utf-8', 'replace')}")
    return 1 if pat else 0


if __name__ == "__main__":
    sys.exit(main())
