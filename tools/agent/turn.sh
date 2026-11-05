#!/bin/bash
# turn.sh DIR FRAMES [command ...]: one agent turn (local helper): send the
# commands, run FRAMES frames, wait for the next stop, print its state
A=$1; F=$2; shift 2
read seq frame < $A/ready
{ for c in "$@"; do echo "0 ai ag $c"; done; echo "0 run $F"; } > $A/cmd.tmp
mv $A/cmd.tmp $A/cmd.$seq.txt
next=$((seq + 1))
for i in $(seq 1 3000); do
  [ -f $A/state.$next.txt ] && read s f < $A/ready && [ "$s" = "$next" ] && break
  sleep 0.05
done
grep -E "^agent:|original kernel|AddressSanitizer|Assertion" $A/game.log | tail -5 > $A/errs.now; cmp -s $A/errs.now $A/errs.last || cat $A/errs.now; cp $A/errs.now $A/errs.last
cat $A/state.$next.txt
