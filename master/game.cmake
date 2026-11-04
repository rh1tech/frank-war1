# frank-war1 — the game as a static library: war1 (one translation unit,
# war1.c) on the port layer (port/fsdl.c, the SDL3 subset it uses). Its
# .bss goes to PSRAM (memmap_war1.ld matches libwar1.a).
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later

set(W1 ${REPO_ROOT}/third_party/war1)

# The game's memory budget, all of it in the PSRAM heap: war1's own zones
# (global, per frame, music) are made once at start; libc's malloc keeps the
# rest (fsdl textures, FatFs, stdio).
set(WAR_PERM_SIZE  "6815744" CACHE STRING "war1 global zone, bytes")   # 6.5 MB
set(WAR_FRAME_SIZE "262144"  CACHE STRING "war1 per-frame zone, bytes")
set(WAR_AUDIO_SIZE "393216"  CACHE STRING "war1 music (MIDI) zone, bytes")

add_library(war1 STATIC
    ${W1}/src/war1.c
    ${REPO_ROOT}/port/fsdl.c)
set_source_files_properties(${REPO_ROOT}/port/fsdl.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
target_include_directories(war1 PRIVATE
    ${REPO_ROOT}/port/include ${REPO_ROOT}/port ${W1}/src ${W1}/deps/include src src/game)
target_compile_definitions(war1 PRIVATE
    WAR_FRANK=1 FRANK_BOARD=1 NDEBUG_WAR=1
    WAR_PERM_SIZE=${WAR_PERM_SIZE} WAR_FRAME_SIZE=${WAR_FRAME_SIZE} WAR_AUDIO_SIZE=${WAR_AUDIO_SIZE}
    FSDL_LOG_MIN=SDL_LOG_PRIORITY_WARN $<$<BOOL:${WAR_ZONECHECK}>:WAR_ZONECHECK=1>
    ${FRANK_DEFINITIONS})
# war1 was written where int32_t is int and enums are int-sized (it asserts
# its structs' layouts): arm-none-eabi has long and short enums. Same sizes
# for int32_t, so make it int (as frank-blood does for NBlood).
# -ffp-contract=off: no fused multiply-adds, so the floats match the host
# build's bit for bit (recorded games replay, port/wdet_math.h).
target_compile_options(war1 PRIVATE -O2 -fno-strict-aliasing -std=gnu11 -fno-short-enums -ffp-contract=off
    -U__INT32_TYPE__ "-D__INT32_TYPE__=int" -U__UINT32_TYPE__ "-D__UINT32_TYPE__=unsigned int"
    -U__INT_LEAST32_TYPE__ "-D__INT_LEAST32_TYPE__=int" -U__UINT_LEAST32_TYPE__ "-D__UINT_LEAST32_TYPE__=unsigned int"
    -Wno-unused -Wno-sign-compare -Wno-implicit-fallthrough -Wno-missing-field-initializers
    -Wno-override-init -Wno-pointer-sign -Wno-char-subscripts -Wno-format)
target_link_libraries(war1 PUBLIC pico_stdlib)

# Music: Apogee's AL_MIDI + GM timbre bank (GPL-2.0-or-later, from Chocolate
# Blood) on emu8950 (MIT), driven by opl_war.c on core 1.
add_library(opl STATIC
    ${REPO_ROOT}/third_party/apogee_opl/al_midi.c
    ${REPO_ROOT}/third_party/apogee_opl/ll_man.c
    ${REPO_ROOT}/third_party/apogee_opl/gmtimbre.c
    ${REPO_ROOT}/third_party/emu8950/emu8950.c
    src/game/opl_war.c)
target_include_directories(opl PUBLIC src/game PRIVATE
    ${REPO_ROOT}/third_party/apogee_opl ${REPO_ROOT}/third_party/emu8950)
target_compile_definitions(opl PRIVATE
    USE_EMU8950_OPL=1 EMU8950_NO_TIMER=1 EMU8950_NO_RATECONV=1 EMU8950_SLOT_RENDER=0)
target_compile_options(opl PRIVATE -O2 -Wno-unused -Wno-sign-compare)
target_link_libraries(opl PUBLIC pico_stdlib)
