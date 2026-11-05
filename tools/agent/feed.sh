#!/bin/bash
# feed.sh SRC DST LAST: replay an agent game's turns 0..LAST from SRC into the
# running game at DST (same build, same start), turn by turn (local helper)
S=$1; D=$2; L=$3
for n in $(seq 0 $L); do
  for i in $(seq 1 3000); do read s f < $D/ready 2>/dev/null && [ "$s" = "$n" ] && break; sleep 0.02; done
  cp $S/cmd.$n.txt $D/cmd.tmp && mv $D/cmd.tmp $D/cmd.$n.txt
done
n=$((L + 1)); for i in $(seq 1 3000); do read s f < $D/ready 2>/dev/null && [ "$s" = "$n" ] && break; sleep 0.02; done
head -1 $D/state.$n.txt
