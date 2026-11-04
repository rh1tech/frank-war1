#!/usr/bin/env bash
# build.sh — build the firmware (the whole game on one RP2350B).
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
cd "$(dirname "$0")"
# The shell may export a stale PICO_SDK_PATH; use it only if it exists.
[ -d "${PICO_SDK_PATH:-}" ] || PICO_SDK_PATH="$HOME/pico/pico-sdk"
export PICO_SDK_PATH
[ -f build/master/build.ninja ] || \
    cmake -S master -B build/master -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/master 2>&1 | grep -E "error|warning:|FAILED" || true
test -f build/master/frank-war1.elf
arm-none-eabi-size build/master/frank-war1.elf | tail -1 | \
    awk '{printf "master text %d  data %d  bss %d\n", $1, $2, $3}'
