#!/usr/bin/env python3
# bait.py DIR BAIT_ID "ARMY IDS" PARK=x,y OUT=x,y [CYCLES]: lure the enemy onto a
# parked army (local helper). The bait walks to OUT; once it sees a foe it
# strikes it and runs back to PARK, and fight2 holds PARK (HOLD, LEASH) until
# the pursuers are dead; then again.
import sys, subprocess, re, os
A, bait, army = sys.argv[1], sys.argv[2], sys.argv[3]
px, py = map(int, os.environ['PARK'].split(','))
ox, oy = map(int, os.environ['OUT'].split(','))
cycles = int(sys.argv[4]) if len(sys.argv) > 4 else 5
here = os.path.dirname(os.path.abspath(__file__))

def turn(step, cmds):
    return subprocess.run([f'{here}/turn.sh', A, str(step), *cmds], capture_output=True, text=True).stdout

def units(txt):
    us = []
    for l in txt.splitlines():
        m = re.match(r'U id (\d+) type (\d+) (.+?) owner (\d) at (\d+),(\d+) size \S+ hp (-?\d+)/(\d+) state (\S+)', l)
        if m: us.append(dict(id=m[1], type=int(m[2]), owner=int(m[4]), x=int(m[5]), y=int(m[6]), hp=int(m[7]), maxhp=int(m[8]), state=m[9]))
    return us

out = turn(1, [])
for c in range(cycles):
    us = units(out)
    b = next((u for u in us if u['id'] == bait), None)
    if not b:
        # a new bait: the fastest healthy unit of the army
        mine = [u for u in us if u['owner'] == 0 and u['id'] in army.split()]
        cands = sorted(mine, key=lambda u: (u['type'] not in (6, 7), -u['hp']))
        if not cands: break
        bait = cands[0]['id']
    # out until something is in sight
    out = turn(150, [f'sel {bait}', f'move {ox} {oy}'])
    seen = None
    for k in range(25):
        us = units(out)
        b = next((u for u in us if u['id'] == bait), None)
        if not b: break
        foes = [u for u in us if 1 <= u['owner'] <= 3 and u['type'] < 32]
        if foes:
            seen = min(foes, key=lambda f: max(abs(f['x'] - b['x']), abs(f['y'] - b['y'])))
            break
        out = turn(120, [])
    if not b: print(f"cycle {c}: bait lost"); continue
    if seen:
        out = turn(60, [f'sel {bait}', f'attack {seen["id"]}'])
    out = turn(60, [f'sel {bait}', f'move {px} {py - 2}'])
    env = dict(os.environ, HOLD='1', LEASH=f'{px},{py},8', SIGHT=os.environ.get('SIGHT', '8'))
    r = subprocess.run(['python3', f'{here}/fight2.py', A, army + ' ' + bait, f'{px},{py}', os.environ.get('HOLDTURNS', '30')],
                       capture_output=True, text=True, env=env).stdout
    fl = [l for l in r.splitlines() if l.startswith('t') and ' group ' in l]
    out = r[r.rfind('tick '):]
    hit = sum(1 for l in fl if not re.search(r' near 0 ', l))
    print(f"cycle {c}: saw {seen['id'] if seen else '-'}; {fl[0] if fl else ''} .. {fl[-1] if fl else ''} (fighting turns {hit})", flush=True)
print(out.splitlines()[0] if out else '')
