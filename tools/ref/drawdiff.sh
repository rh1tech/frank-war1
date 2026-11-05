#!/bin/bash
# drawdiff.sh RACE N TICK [SPAN]: both games' draws (callers) for ticks TICK-SPAN..TICK, the reference traced fresh if missing
race=$1; m=$2; T=$3; span=${4:-3}
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; S="$ROOT/build/ai/ref"; mkdir -p "$S"; REF="${REF:-$ROOT/../war1-reference/recomp}"
y=$([ "$race" = orc ] && echo 100 || echo 117)
ref=$S/reft_$race$m.txt
seed=$(tail -1 "$S/ra_$race$m.txt.seed" 2>/dev/null | awk '{print $2}')
[ -n "$seed" ] || { echo "$race$m: run rngall.sh or rngcmp.sh first to establish a seed" >&2; exit 2; }
secs=$(( T / 33 + 20 ))
if [ ! -s "$ref" ] || [ "$(cat "$ref.secs" 2>/dev/null)" -lt "$secs" ] || [ "$(tail -1 "$ref.seed" 2>/dev/null | awk '{print $2}')" != "$seed" ]; then
  ( cd "$REF" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy WAR_MUTE=1 WAR_RNGSEED=$seed WAR_SPEED_SETTING=3 RECOMP_TRACE=1 WAR_WATCH=50d7c,4 WAR_AILOG=$S/trace_$race$m.txt WAR_INPUT="8:click@160,100,11:click@160,100,14:click@160,$y,18:esc,24:return,25:type:corwin_of_amber,26:return,28:return,29:type:${race}_$m,30:return,36:esc,41:speed:10,$(( secs / 10 + 43 )):quit" timeout 900 build120/warT work/game build120/image.bin > "$ref" 2>&1 ) || { echo "$race$m: reference trace failed" >&2; exit 2; }
  echo $secs > $ref.secs
  cp "$S/trace_$race$m.txt.seed" "$ref.seed" || exit 2
fi
echo "== ref"
grep -oE "tick [0-9]+, unit -?[0-9]+\); call stack \(innermost first\): 19478 [0-9a-f]+ [0-9a-f]+" $ref | awk -v a=$((T-span)) -v b=$T '{t=$2+0} t>=a && t<=b {print t, $4, $(NF-1), $NF}' | uniq -c
printf "%d quit\n" $(( (T + 40) * 2 )) > "$S/qd_$race$m.txt"
echo "== port"
( cd "$ROOT/build/host/run" && WAR_RNGLOG=$S/draw_$race$m.txt WAR_RNGTRACE=1 WAR_FIGHTDEBUG=1 WAR_RNGSEED=$seed WAR_FIXED=1 WAR_SCRIPT=$S/qd_$race$m.txt timeout 300 ../war1-host --race $race --mission $m 2>&1 >/dev/null ) | grep -v "^\[" | awk -v a=$((T-span)) -v b=$T '/^draw tick/{t=$3+0} t>=a && t<=b' | uniq -c
