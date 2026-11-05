#!/usr/bin/env python3
# fight.py DIR "ID ID ..." "X,Y X,Y ..." [TURNS]: my tactics for an attack group
# (local helper): fight what is in sight, nearest first, units before
# buildings; else walk on to the next waypoint
import sys, subprocess, re, os
A, group, wps = sys.argv[1], sys.argv[2].split(), [tuple(map(int, w.split(','))) for w in sys.argv[3].split()]
turns = int(sys.argv[4]) if len(sys.argv) > 4 else 60
here = os.path.dirname(os.path.abspath(__file__))
def parse(txt):
    head = re.search(r'tick (\d+) .* result (\d)', txt)
    us = []
    for l in txt.splitlines():
        m = re.match(r'U id (\d+) type (\d+) (.+?) owner (\d) at (\d+),(\d+) size (\d+)x(\d+) hp (\d+)/(\d+) state (\S+)', l)
        if m: us.append(dict(id=m[1], type=int(m[2]), name=m[3], owner=int(m[4]), x=int(m[5]), y=int(m[6]),
                             w=int(m[7]), hp=int(m[9]), state=m[11]))
    return head, us
out = subprocess.run([f'{here}/turn.sh', A, '1'], capture_output=True, text=True).stdout
wp = 0; last = None; prev = None; stuck = 0; start = len(group)
for t in range(turns):
    head, us = parse(out)
    if not head: print(out); break
    mine = [u for u in us if u['owner'] == 0 and u['id'] in group]
    group = [u['id'] for u in mine]
    if not mine or int(head[2]): break
    if os.environ.get('STOPAT') and re.search(os.environ['STOPAT'], out): break
    if os.environ.get('RETREAT') and len(group) * 2 < start:
        rx, ry = os.environ['RETREAT'].split(',')
        out = subprocess.run([f'{here}/turn.sh', A, '400', 'sel ' + ' '.join(group), f'move {rx} {ry}'], capture_output=True, text=True).stdout
        print(f"t{head[1]} retreat with {len(group)} of {start}"); break
    cx = sum(u['x'] for u in mine) / len(mine); cy = sum(u['y'] for u in mine) / len(mine)
    foes = [u for u in us if 1 <= u['owner'] <= 3]
    def dist(u): return max(abs(u['x'] + u['w'] / 2 - cx), abs(u['y'] + u['w'] / 2 - cy))
    near = sorted([f for f in foes if dist(f) <= 12], key=lambda f: (f['type'] not in (4, 5), f['type'] >= 32, dist(f)))
    if near:
        tgt = near[0]['id']
        cmds = [] if tgt == last else ['sel ' + ' '.join(group), f'attack {tgt}']
        last = tgt; step = 300
    else:
        stuck = stuck + 1 if prev and abs(prev[0] - cx) + abs(prev[1] - cy) < 0.5 else 0
        prev = (cx, cy)
        while wp < len(wps) and (max(abs(cx - wps[wp][0]), abs(cy - wps[wp][1])) <= 3 or stuck >= 3):
            wp += 1; stuck = 0
        if wp >= len(wps): break
        cmds = ['sel ' + ' '.join(group), f'move {wps[wp][0]} {wps[wp][1]}']; last = None; step = 400
    out = subprocess.run([f'{here}/turn.sh', A, str(step), *cmds], capture_output=True, text=True).stdout
    for l in out.splitlines():
        if 'kernel' in l or 'Assertion' in l: print(l)
    print(f"t{head[1]} group {len(group)} foes {len(foes)} near {len(near)} -> {' '.join(cmds[1:]) if cmds else 'keep'}")
print(out)
