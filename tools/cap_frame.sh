#!/usr/bin/env bash
# cap_frame.sh <out.png> [WxH] [fps] [skip] — grab one frame from the HDMI capture card.
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
source "$(dirname "$0")/bench.conf"
out="${1:?usage: cap_frame.sh out.png [WxH] [fps] [skip]}"
size="${2:-720x576}"; fps="${3:-50}"; skip="${4:-20}"
ffmpeg -hide_banner -loglevel error -y -f avfoundation -video_size "$size" \
    -framerate "$fps" -pixel_format uyvy422 -i "$CAPTURE_VIDEO:none" \
    -vf "select=gte(n\,$skip)" -frames:v 1 -update 1 "$out"
