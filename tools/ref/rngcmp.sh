#!/bin/bash
# rngcmp.sh RACE MISSION SECONDS: first tick where the random state differs
race=$1; m=$2; secs=${3:-30}
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; S="$ROOT/build/ai/ref"; mkdir -p "$S"; REF="${REF:-$ROOT/../war1-reference/recomp}"
y=$([ "$race" = orc ] && echo 100 || echo 117)
seed=$(tail -1 "$S/ra_$race$m.txt.seed" 2>/dev/null | awk '{print $2}')
if [ -n "$seed" ]; then export WAR_RNGSEED="$seed"; fi
q=$(( secs / 10 + 43 ))
( cd "$REF" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy WAR_MUTE=1 WAR_AIFIRST=1 WAR_SPEED_SETTING=3 WAR_RNGLOG=$S/rr_$race$m.txt WAR_AILOG=$S/ra_$race$m.txt WAR_INPUT="8:click@160,100,11:click@160,100,14:click@160,$y,18:esc,24:return,25:type:corwin_of_amber,26:return,28:return,29:type:${race}_$m,30:return,36:esc,41:speed:10,$q:quit" timeout 300 build120/war work/game build120/image.bin >/dev/null 2>&1 ) || { echo "$race$m: reference run failed" >&2; exit 2; }
seed=$(tail -1 $S/ra_$race$m.txt.seed | awk '{print $2}')
[ -n "$seed" ] || { echo "$race$m: missing reference seed" >&2; exit 2; }
printf "%d quit\n" $(( secs * 60 )) > $S/q_$race$m.txt
( cd "$ROOT/build/host/run" && WAR_RNGLOG=$S/rp_$race$m.txt WAR_RNGSEED=$seed WAR_FIXED=1 WAR_SCRIPT=$S/q_$race$m.txt timeout 300 ../war1-host --race $race --mission $m >/dev/null 2>&1 ) || { echo "$race$m: port run failed" >&2; exit 2; }
python3 "$ROOT/tools/ref/rng_compare.py" "$S/rr_$race$m.txt" "$S/rp_$race$m.txt" "$(( (secs * 100 + 2) / 3 ))" --mission "$race$m"
