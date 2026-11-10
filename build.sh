#!/usr/bin/env bash
# build.sh [CORE2|M2] [cdc] — build the firmware (the whole game on one RP2350B).
#   CORE2 (default): FRANK Core 2 master  -> build/master/frank-war1.elf
#   M2:              Murmulator / FRANK M2 -> build/m2/frank-war1.elf
#   cdc: the native USB is a CDC console instead of the keyboard host
#        (USB_HID=OFF)                    -> build/<board>-cdc/frank-war1.elf
# Each board has its own build directory: frank.cmake caches the per-board
# PSRAM defaults.
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
cd "$(dirname "$0")"
# The shell may export a stale PICO_SDK_PATH; use it only if it exists.
[ -d "${PICO_SDK_PATH:-}" ] || PICO_SDK_PATH="$HOME/pico/pico-sdk"
export PICO_SDK_PATH
board="${1:-CORE2}"
case "$board" in
CORE2) dir=build/master ;;
M2)    dir=build/m2 ;;
*)     echo "usage: build.sh [CORE2|M2] [cdc]" >&2; exit 1 ;;
esac
usb_hid=ON
if [ "${2:-}" = cdc ]; then usb_hid=OFF; dir="$dir-cdc"; fi
[ -f "$dir/build.ninja" ] || \
    cmake -S master -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DFRANK_BOARD="$board" \
        -DUSB_HID="$usb_hid" >/dev/null
cmake --build "$dir" 2>&1 | grep -E "error|warning:|FAILED" || true
test -f "$dir/frank-war1.elf"
arm-none-eabi-size "$dir/frank-war1.elf" | tail -1 | \
    awk -v b="$board" '{printf "%s text %d  data %d  bss %d\n", b, $1, $2, $3}'
