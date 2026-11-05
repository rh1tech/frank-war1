#!/usr/bin/env python3
# explore.py DIR [ROUNDS]: walk the whole party (fight2) to the nearest
# reachable tile next to unexplored ground, again and again, until the
# mission ends or nothing is left to explore (local helper)
import sys, subprocess, os, re
from collections import deque
A = sys.argv[1]
rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 20
here = os.path.dirname(os.path.abspath(__file__))

def turn(step, cmds):
    return subprocess.run([f'{here}/turn.sh', A, str(step), *cmds], capture_output=True, text=True).stdout

out = turn(1, [f'fullmap {A}/full.txt'])
for r in range(rounds):
    seq = int(open(f'{A}/ready').read().split()[0])
    state = open(f'{A}/state.{seq}.txt').read()
    if re.search(r'result [12]', state): print(state.splitlines()[0]); break
    mine = [tuple(map(int, m)) for m in re.findall(r'owner 0 at (\d+),(\d+)', state)]
    ids = [i for i, t in re.findall(r'U id (\d+) type (\d+) .+? owner 0 ', state) if int(t) < 32 and int(t) not in (2, 3)]   # (the army: no workers, no buildings)
    mine = [tuple(map(int, m)) for m in re.findall(r'U id \d+ type (?:\d|[12]\d|3[01]) .+? owner 0 at (\d+),(\d+)', state) if True]
    if not mine: break
    cx = sorted(x for x, y in mine)[len(mine) // 2]; cy = sorted(y for x, y in mine)[len(mine) // 2]
    full = open(f'{A}/full.txt').read().split()
    seen = open(f'{A}/map.{seq}.txt').read().split()
    ok = lambda x, y: 0 <= x < 64 and 0 <= y < 64 and full[y][x] in '.E'
    def frontier(x, y):
        return any(0 <= x + dx < 64 and 0 <= y + dy < 64 and seen[y + dy][x + dx] == '?'
                   for dx in (-2, 0, 2) for dy in (-2, 0, 2))
    q = deque([(cx, cy)]); prev = {(cx, cy): None}; goal = None
    while q:
        c = q.popleft()
        if frontier(*c) and max(abs(c[0] - cx), abs(c[1] - cy)) > 6: goal = c; break
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                n = (c[0] + dx, c[1] + dy)
                if n not in prev and ok(*n): prev[n] = c; q.append(n)
    if not goal: print('nothing left to explore'); break
    path = []; c = goal
    while c: path.append(c); c = prev[c]
    path = path[::-1]
    wps = ' '.join(f'{x},{y}' for x, y in (path[6::6] + [goal]))
    env = dict(os.environ, SIGHT=os.environ.get('SIGHT', '7'))
    res = subprocess.run(['python3', f'{here}/fight2.py', A, ' '.join(ids), wps, '60'], capture_output=True, text=True, env=env).stdout
    fl = [l for l in res.splitlines() if l.startswith('t') and ' group ' in l]
    print(f"round {r}: to {goal[0]},{goal[1]} {fl[-1] if fl else ''}", flush=True)
