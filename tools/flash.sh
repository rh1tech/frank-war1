#!/usr/bin/env bash
#
# flash.sh <image.elf> [--swd|--usb]
#
# Program the master and leave it running.
#   (default) --uart: the running firmware's console command `bootsel`
#             restarts it into the USB bootloader, then picotool loads the
#             image (falls back to a board already in BOOTSEL)
#   --usb:    picotool only (hold BOOTSEL first)
#   --swd:    OpenOCD through the master's CMSIS-DAP probe
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/bench.conf"

elf="${1:?usage: flash.sh <image.elf> [--uart|--usb|--swd]}"
mode="${2:---uart}"
[ -f "$elf" ] || { echo "no such image: $elf" >&2; exit 1; }

in_bootsel() { picotool info --ser "$MASTER_CHIP_ID" >/dev/null 2>&1; }

case "$mode" in
--swd)
    log="$(openocd -f interface/cmsis-dap.cfg -c "adapter serial $MASTER_PROBE" \
            -c "adapter speed $MASTER_SWD_KHZ" -f target/rp2350.cfg \
            -c init -c "reset halt" \
            -c "flash write_image erase $elf" -c "verify_image $elf" \
            -c "reset halt" -c resume -c exit 2>&1)" || true
    grep -qE "verified [0-9]+ bytes" <<<"$log" || { echo "$log" | tail -20 >&2; exit 1; }
    ;;
--uart|--usb)
    if [ "$mode" = --uart ] && ! in_bootsel; then
        python3 "$HERE/uart.py" master send "bootsel\r" --seconds 1 >/dev/null 2>&1 || true
        for _ in $(seq 1 30); do in_bootsel && break; sleep 0.2; done
    fi
    in_bootsel || { echo "error: master not in BOOTSEL (hold BOOTSEL and plug in, or use --swd)" >&2; exit 1; }
    picotool load -x "$elf" --ser "$MASTER_CHIP_ID" 2>&1 | grep -vE "^(Loading|Verifying) " | grep -v '^\s*$' || true
    ;;
*) echo "mode: --uart, --usb or --swd" >&2; exit 2 ;;
esac
echo "==> master running $(basename "$elf")"
