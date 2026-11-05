#!/usr/bin/env bash
# ai_ref_compare.sh RACE MISSION [MINUTES] — play a campaign mission with the
# human idle in the original (the war1-reference build of the user's WAR.EXE
# 1.20, local only) and in the port's host build, and compare them unit by
# unit (tools/ai_compare.py). Logs go to build/ai/.
#   REF=/path/to/war1-reference/recomp   (default: next to this repository)
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: Zlib
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="${REF:-$ROOT/../war1-reference/recomp}"
race="${1:?race: human or orc}"; mission="${2:?mission 1-12}"; minutes="${3:-10}"
out="$ROOT/build/ai"; mkdir -p "$out"
ref_log="$out/ref_${race}${mission}.txt"; port_log="$out/port_${race}${mission}.txt"
seed=$(tail -1 "$out/ref/ra_${race}${mission}.txt.seed" 2>/dev/null | awk '{print $2}') || seed=
if [ -n "$seed" ]; then export WAR_RNGSEED="$seed"; fi
# the original: new game, single player, the race's campaign, then the
# cheats to jump to the mission; normal speed, the clock 10x for the run
y=$([ "$race" = orc ] && echo 100 || echo 117)
secs=$(( minutes * 60 / 10 + 45 ))
rm -f "$ref_log.seed"
( cd "$REF" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy WAR_MUTE=1 WAR_SPEED_SETTING=3 WAR_AITILES=1 \
  WAR_AILOG="$ref_log" \
  WAR_INPUT="8:click@160,100,11:click@160,100,14:click@160,$y,18:esc,24:return,25:type:corwin_of_amber,26:return,28:return,29:type:${race}_${mission},30:return,36:esc,41:speed:10,$secs:quit" \
  timeout $(( secs + 60 )) build120/war work/game build120/image.bin > /dev/null 2>&1 )
# the port: the same mission straight away, from the original's random seed
# at the level's start (the reference logs it)
seed=$(tail -1 "$ref_log.seed" 2>/dev/null | awk '{print $2}')
[ -n "$seed" ] || { echo "missing reference seed: $ref_log.seed" >&2; exit 2; }
frames=$(( minutes * 3600 ))
printf "%d quit\n" "$frames" > "$out/quit_${race}${mission}.txt"
( cd "$ROOT/build/host/run" && WAR_AITILES=1 WAR_AILOG="$port_log" WAR_RNGSEED="$seed" WAR_FIXED=1 WAR_SCRIPT="$out/quit_${race}${mission}.txt" \
  timeout 900 ../war1-host --race "$race" --mission "$mission" > /dev/null 2>&1 )
flags=$(grep -m1 -oE "flags [0-9a-f]+" "$port_log" | awk '{print $2}')
coverage=()
if [ -n "${WAR_AIEVERY:-}" ]; then coverage=(--ticks "$(( minutes * 2000 ))"); fi
python3 "$ROOT/tools/ai_compare.py" "$ref_log" "$port_log" --flags "$flags" "${coverage[@]}" "${@:4}"
