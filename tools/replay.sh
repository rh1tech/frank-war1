#!/usr/bin/env bash
# replay.sh <recording.txt> — send a recording (tools/record.sh) to the
# board's card, replay it there and report the speed and the state checks.
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
rec="${1:?usage: replay.sh recording.txt}"
python3 "$HERE/push.py" "$rec" /war1/replay.txt
python3 "$HERE/uart.py" master send "replay\r" --seconds 1 >/dev/null
frames=$(tail -1 "$rec" | awk '{print $1}')
python3 "$HERE/uart.py" master capture --seconds $(( frames / 30 + 60 )) --until "replay: done" \
    | grep -E "replay|HARD FAULT|assert" || true
