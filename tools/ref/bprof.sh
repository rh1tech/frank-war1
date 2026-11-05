#!/bin/bash
# bprof.sh race mission [wait_s]: start that mission on the board, wait, report fps and the profile
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"
S="$ROOT/build/ai/ref"; mkdir -p "$S"
printf "race=$1\nmission=$2\n" > $S/frank.cfg
python3 tools/push.py $S/frank.cfg /war1/frank.cfg >/dev/null 2>&1 || { echo push failed; exit 1; }
python3 tools/uart.py master send "reboot\r" --seconds 1 >/dev/null
sleep ${3:-45}
python3 tools/uart.py master send "stat\r" --seconds 1 >/dev/null
python3 tools/uart.py master capture --seconds 10 2>&1 | grep -oE "game [0-9.]+" | tr '\n' ' '; echo
python3 tools/uart.py master send "stat off\r" --seconds 1 >/dev/null
bash tools/prof.sh 8 2>&1 | head -${4:-16}
