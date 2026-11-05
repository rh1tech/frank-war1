#!/bin/bash
# watch.sh RACE MISSION REFSLOT PORTID [TICKS]: both games' tile timeline of one unit
race=$1; m=$2; slot=$3; id=$4; ticks=${5:-600}
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; S="$ROOT/build/ai/ref"; mkdir -p "$S"; REF="${REF:-$ROOT/../war1-reference/recomp}"
y=$([ "$race" = orc ] && echo 100 || echo 117)
seed=$(tail -1 "$S/ra_$race$m.txt.seed" 2>/dev/null | awk '{print $2}')
if [ -n "$seed" ]; then export WAR_RNGSEED="$seed"; fi
secs=$(( ticks / 330 + 44 ))
( cd $REF && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy WAR_MUTE=1 WAR_SPEED_SETTING=3 WAR_AITILES=1 WAR_AIWATCH=$slot WAR_AILOG=$S/wref.txt WAR_INPUT="8:click@160,100,11:click@160,100,14:click@160,$y,18:esc,24:return,25:type:corwin_of_amber,26:return,28:return,29:type:${race}_$m,30:return,36:esc,41:speed:10,$secs:quit" timeout 200 build120/war work/game build120/image.bin >/dev/null 2>&1 )
echo "ref:"; grep "^w " $S/wref.txt | awk -v T=$ticks '$2<=T {print $2, $6, $8}' | uniq -f1 | tr '\n' ' '; echo
grep "^wp" $S/wref.txt | awk -v T=$ticks '$2<=T' | awk '{$1="";print}' | uniq -f1 | head -${6:-6}
printf "%d quit\n" $((ticks*2)) > $S/q.txt
( cd $ROOT/build/host/run && WAR_RNGSEED=$(tail -1 $S/wref.txt.seed | awk "{print \$2}") WAR_AIWATCH=$id WAR_MOVEDEBUG=$id WAR_FIXED=1 WAR_SCRIPT=$S/q.txt timeout 200 ../war1-host --race $race --mission $m 2>$S/wportdbg.txt >/dev/null )
echo "port:"; awk -v T=$ticks '$2<=T {print $2, $6, $8}' $ROOT/build/host/run/aiwatch.txt | uniq -f1 | tr '\n' ' '; echo
grep -A7 "^plan" $S/wportdbg.txt | grep -v "^\[" | head -${7:-16}
