#!/usr/bin/env python3
# doors.py DIR [ROUNDS]: break the nearest door (a W tile next to explored
# ground) with the army, explore, again (local helper, mazes with doors)
import sys, subprocess, os, re
A = sys.argv[1]; rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 10
here = os.path.dirname(os.path.abspath(__file__))
def turn(step, cmds=()): return subprocess.run([f'{here}/turn.sh', A, str(step), *cmds], capture_output=True, text=True).stdout
done = set()
for r in range(rounds):
    turn(1, [f'fullmap {A}/full.txt'])
    seq = int(open(f'{A}/ready').read().split()[0]); st = open(f'{A}/state.{seq}.txt').read()
    if re.search(r'result [12]', st): print(st.splitlines()[0]); break
    full = open(f'{A}/full.txt').read().split(); seen = open(f'{A}/map.{seq}.txt').read().split()
    us = [(int(i), int(t), int(x), int(y)) for i, t, x, y in re.findall(r'U id (\d+) type (\d+) .+? owner 0 at (\d+),(\d+)', st)]
    army = [u for u in us if u[1] < 32 and u[1] not in (2, 3)]
    if not army: break
    cx = sorted(u[2] for u in army)[len(army) // 2]; cy = sorted(u[3] for u in army)[len(army) // 2]
    doors = [(x, y) for y in range(64) for x in range(64) if full[y][x] == 'W' and (x, y) not in done and
             any(0 <= x + dx < 64 and 0 <= y + dy < 64 and seen[y + dy][x + dx] != '?' and full[y + dy][x + dx] in '.E'
                 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    if not doors: print('no door left'); break
    d = min(doors, key=lambda p: max(abs(p[0] - cx), abs(p[1] - cy))); done.add(d)
    ids = ' '.join(str(u[0]) for u in army if u[1] in (1, 9, 0, 8, 7, 6))
    turn(1, [f'sel {ids}', f'attackxy {d[0]} {d[1]}'])
    for i in range(14):
        out = turn(300)
        if re.search(r'result [12]', out): break
    ex = subprocess.run(['python3', f'{here}/explore.py', A, '30'], capture_output=True, text=True).stdout.strip().splitlines()
    print(f"door {d}: {ex[-2] if len(ex) > 1 else ''} | {ex[-1] if ex else ''}", flush=True)
