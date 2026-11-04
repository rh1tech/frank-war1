#!/usr/bin/env bash
# record.sh [out.txt] — play War1 in a window on the Mac and record it for
# the board (tools/replay.sh). The game runs on a fixed 1/60 s step; quit
# with the window's close button (or the game's Quit) to finish the file.
#   DATA=/path/to/DATA.WAR  (default: the retail copy in ~/Documents/war1)
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: Zlib
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
out="$(cd "$(dirname "${1:-recording.txt}")" && pwd)/$(basename "${1:-recording.txt}")"
DATA="${DATA:-/Volumes/1TB/Documents/war1/FDATA/DATA.WAR}"
"$ROOT/host/build.sh" >/dev/null
run="$ROOT/build/host/run"
mkdir -p "$run"
ln -sf "$DATA" "$run/DATA.WAR"
cd "$run"
WAR_RECORD="$out" ../war1-host 2>/dev/null || true
echo "recorded $(grep -c . "$out") lines, $(grep -c ' hash ' "$out") state checks -> $out"
echo "check on the host: WAR_FIXED=1 WAR_SCRIPT=$out $run/../war1-host"
echo "replay on the board: tools/replay.sh $out"
