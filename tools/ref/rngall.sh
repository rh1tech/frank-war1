#!/bin/bash
# rngall.sh [SECS]: per mission, the first tick the port's random state leaves the original's (reference logs cached)
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; S="$ROOT/build/ai/ref"; mkdir -p "$S"; REF="${REF:-$ROOT/../war1-reference/recomp}"
secs=${1:-300}
one() {
  race=$1; m=$2
  y=$([ "$race" = orc ] && echo 100 || echo 117)
  rr=$S/rr_$race$m.txt
  seed=$(tail -1 "$S/ra_$race$m.txt.seed" 2>/dev/null | awk '{print $2}')
  if [ -n "$seed" ]; then export WAR_RNGSEED="$seed"; fi
  if [ ! -s $rr ] || [ $(wc -l < $rr) -lt $(( (secs * 100 + 2) / 3 )) ]; then
    q=$(( secs / 10 + 43 ))
    ( cd "$REF" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy WAR_MUTE=1 WAR_AIFIRST=1 WAR_SPEED_SETTING=3 WAR_RNGLOG=$rr WAR_AILOG=$S/ra_$race$m.txt WAR_INPUT="8:click@160,100,11:click@160,100,14:click@160,$y,18:esc,24:return,25:type:corwin_of_amber,26:return,28:return,29:type:${race}_$m,30:return,36:esc,41:speed:10,$q:quit" timeout 600 build120/war work/game build120/image.bin >/dev/null 2>&1 ) || { echo "$race$m INVALID: reference run failed"; return 2; }
  fi
  seed=$(tail -1 $S/ra_$race$m.txt.seed | awk '{print $2}')
  [ -n "$seed" ] || { echo "$race$m INVALID: missing reference seed"; return 2; }
  printf "%d quit\n" $(( secs * 66 )) > $S/qa_$race$m.txt
  ( cd "$ROOT/build/host/run" && WAR_RNGLOG=$S/pa_$race$m.txt WAR_RNGSEED=$seed WAR_FIXED=1 WAR_SCRIPT=$S/qa_$race$m.txt timeout 600 ../war1-host --race $race --mission $m >/dev/null 2>&1 ) || { echo "$race$m INVALID: port run failed"; return 2; }
  python3 "$ROOT/tools/ref/rng_compare.py" "$rr" "$S/pa_$race$m.txt" "$(( (secs * 100 + 2) / 3 ))" --mission "$race$m" 2>&1
}
for r in human orc; do for m in 1 2 3 4 5 6 7 8 9 10 11 12; do one $r $m > $S/ra_out_$r$m.txt & done; wait; done
for r in human orc; do for m in 1 2 3 4 5 6 7 8 9 10 11 12; do cat $S/ra_out_$r$m.txt; done; done | tr '\n' ' '; echo
