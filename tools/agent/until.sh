#!/bin/bash
# until.sh DIR STEP MAX REGEX: run STEP frames at a time (at most MAX steps)
# until the state matches REGEX (an extended grep), then print the state
A=$1; S=$2; M=$3; R=$4; D="$(dirname "$0")"
for i in $(seq 1 $M); do
  out=$("$D/turn.sh" $A $S)
  if echo "$out" | grep -qE "$R"; then break; fi
done
echo "$out"
