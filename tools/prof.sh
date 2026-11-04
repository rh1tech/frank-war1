#!/usr/bin/env bash
# prof.sh [seconds] — sample the master's core 0 (console `prof`/`profdump`)
# and print the hottest functions.
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ELF="${ELF:-$HERE/../build/master/frank-war1.elf}"
secs="${1:-4}"
python3 "$HERE/uart.py" master send "prof\r" --seconds 1 >/dev/null
sleep "$secs"
out="$(python3 "$HERE/uart.py" master send "profdump\r" --seconds 3)"
total=$(grep -oE "prof: [0-9]+ samples" <<<"$out" | grep -oE "[0-9]+" | head -1)
echo "$total samples"
grep -E "^\[.*\] prof [0-9a-f]{8} " <<<"$out" | awk '{print $(NF-1), $NF}' | while read pc n; do
    fn=$(arm-none-eabi-addr2line -f -e "$ELF" "0x$pc" | head -1)
    echo "$fn $n"
done | awk -v t="$total" '{s[$1]+=$2} END {for (f in s) printf "%5.1f%%  %s\n", 100*s[f]/t, f}' | sort -rn | head -25
