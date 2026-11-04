#!/usr/bin/env bash
# sd_put.sh <local file> <sd path> — copy a file (64 KB at most) onto the
# master's microSD without removing the card: SWD writes it to the top of
# master PSRAM (0x157F0000, kept out of the heap), the console's `sdput`
# saves it. The master firmware must be running.
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/bench.conf"
f="${1:?usage: sd_put.sh file sdpath}"; dst="${2:?usage: sd_put.sh file sdpath}"
size=$(stat -f %z "$f")
[ "$size" -le 65536 ] || { echo "sd_put.sh: $f is over 64 KB" >&2; exit 1; }
log="$(openocd -f interface/cmsis-dap.cfg -c "adapter serial $MASTER_PROBE" \
     -c "adapter speed $MASTER_SWD_KHZ" -f target/rp2350.cfg -c init \
     -c "load_image $f 0x157F0000 bin" -c exit 2>&1)" || true
grep -q "downloaded" <<<"$log" || { echo "$log" | tail -5; exit 1; }
python3 "$HERE/uart.py" master send "sdput $size $dst\r" --seconds 10 \
    --until "^OK|^ERR" | grep -E "sdput:|ERR"
echo "local crc $(python3 -c "import zlib,sys; print('%08x' % zlib.crc32(open(sys.argv[1],'rb').read()))" "$f") (must match)"
