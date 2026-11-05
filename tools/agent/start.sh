#!/bin/bash
# start.sh DIR RACE MISSION [BIN]: a fresh agent game in build/agent/DIR, on
# build/host/BIN (default war1-agent39; copy a fresh build to a new name
# first, macOS kills a re-written binary). NOTURN=1: no first turn (feed.sh)
R="$(cd "$(dirname "$0")/../.." && pwd)"; A=$R/build/agent/$1; B=${4:-war1-agent39}
rm -rf $A; mkdir -p $A
cd $R/build/host/run && (WAR_AILOG=/dev/null WAR_FIXED=1 WAR_AGENT=$A ../$B --race $2 --mission $3 > $A/game.log 2>&1 &)
for i in $(seq 1 200); do [ -f $A/ready ] && break; sleep 0.05; done
[ -n "$NOTURN" ] && exit 0; "$(dirname "$0")/turn.sh" $A 1 "mines $A/mines.txt" "fullmap $A/full.txt"
cat $A/mines.txt
