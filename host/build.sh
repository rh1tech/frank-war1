#!/usr/bin/env bash
# host/build.sh — build the host test build of war1 on the frank port layer
# (the same game code and renderer as the board, SDL3 only for the window).
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: Zlib
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
# Same budget as the board (master/game.cmake): PSRAM heap minus libc's share.
BUDGET="-DWAR_PERM_SIZE=${WAR_PERM_SIZE:-6815744} -DWAR_FRAME_SIZE=${WAR_FRAME_SIZE:-262144} -DWAR_AUDIO_SIZE=${WAR_AUDIO_SIZE:-393216}"
CFLAGS="-std=c11 -g -O${OPT:-1} -ffp-contract=off -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable ${SAN:-}"
SDL_CFLAGS=$(pkg-config --cflags sdl3)
SDL_LIBS=$(pkg-config --libs sdl3)
# The game and fsdl see only the fake SDL3 (port/include); plat_host.c the real one.
clang $CFLAGS -DWAR_FRANK=1 $BUDGET -Iport/include -Iport -Ithird_party/war1/src -Ithird_party/war1/deps/include \
    -c third_party/war1/src/war1.c -o $OUT/war1.o
clang $CFLAGS -Iport/include -Iport -c port/fsdl.c -o $OUT/fsdl.o
clang $CFLAGS $SDL_CFLAGS -Iport -c host/plat_host.c -o $OUT/plat_host.o
clang $CFLAGS $OUT/war1.o $OUT/fsdl.o $OUT/plat_host.o $SDL_LIBS -lm -o $OUT/war1-host
echo "built $OUT/war1-host"
