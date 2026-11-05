#!/usr/bin/env python3
# fight2.py DIR "ID ID ..." "X,Y X,Y ..." [TURNS]: smarter tactics for an attack
# group (local helper). Each unit picks its own target (catapults, casters,
# archers, melee, then buildings; at most CAP on one foe, nearest to it);
# the wounded fall back to RALLY=x,y (if given); clerics heal the most hurt
# friend near them; without foes the group regroups when strung out, else
# attack-moves to the next waypoint. STOPAT=regex ends the fight early;
# RETREAT=x,y pulls everyone back when half the group is lost.
import sys, subprocess, re, os
A, group = sys.argv[1], sys.argv[2].split()
wps = [tuple(map(int, w.split(','))) for w in sys.argv[3].split()]
turns = int(sys.argv[4]) if len(sys.argv) > 4 else 60
here = os.path.dirname(os.path.abspath(__file__))
CAP = int(os.environ.get('CAP', '3'))
SIGHT = int(os.environ.get('SIGHT', '9'))
RALLY = tuple(map(int, os.environ['RALLY'].split(','))) if os.environ.get('RALLY') else None
CATAPULT, CASTER, ARCHER = (4, 5), (10, 11, 12, 13), (8, 9)
CLERIC = 12
SIEGE = tuple(map(int, os.environ['SIEGE'].split(','))) if os.environ.get('SIEGE') else None   # catapults' standing target
LEASH = tuple(map(int, os.environ['LEASH'].split(','))) if os.environ.get('LEASH') else None   # x,y,r

def parse(txt):
    head = re.search(r'tick (\d+) .* result (\d)', txt)
    us = []
    for l in txt.splitlines():
        m = re.match(r'U id (\d+) type (\d+) (.+?) owner (\d) at (\d+),(\d+) size (\d+)x(\d+) hp (-?\d+)/(\d+) state (\S+)(?: mana (\d+))?', l)
        if m: us.append(dict(id=m[1], type=int(m[2]), name=m[3], owner=int(m[4]), x=int(m[5]), y=int(m[6]),
                             w=int(m[7]), hp=int(m[9]), maxhp=max(1, int(m[10])), state=m[11], mana=int(m[12] or 0)))
    return head, us

def turn(step, cmds):
    return subprocess.run([f'{here}/turn.sh', A, str(step), *cmds], capture_output=True, text=True).stdout

def d(a, b):
    return max(abs(a['x'] + a['w'] / 2 - b['x'] - b['w'] / 2), abs(a['y'] + a['w'] / 2 - b['y'] - b['w'] / 2))

def prio(f):
    t = f['type']
    if t in CATAPULT or t == 14: return 0   # (and Medivh)
    if t in CASTER: return 1
    if t in ARCHER: return 2
    if t < 32: return 3
    return 6

out = turn(1, [])
wp, start, orders, back = 0, len(group), {}, set()
prev, stuck = None, 0
for t in range(turns):
    head, us = parse(out)
    if not head: print(out); break
    mine = [u for u in us if u['owner'] == 0 and u['id'] in group]
    group = [u['id'] for u in mine]
    if not mine or int(head[2]): break
    if os.environ.get('STOPAT') and re.search(os.environ['STOPAT'], out): break
    if os.environ.get('RETREAT') and len(group) * 2 < start:
        rx, ry = os.environ['RETREAT'].split(',')
        out = turn(400, ['sel ' + ' '.join(group), f'move {rx} {ry}'])
        print(f"t{head[1]} retreat with {len(group)} of {start}"); break
    foes = [u for u in us if 1 <= u['owner'] <= 3 and u['hp'] > 0 and str(u['type']) not in os.environ.get('KEEP', '').split()]   # (KEEP=types: never attacked)
    want = {}
    # the wounded go back (and come again when healed)
    for u in mine:
        if RALLY and u['type'] != CLERIC and u['hp'] * 10 < u['maxhp'] * 3 and any(d(u, f) <= SIGHT for f in foes):
            back.add(u['id'])
        if u['hp'] * 10 >= u['maxhp'] * 8: back.discard(u['id'])
        if u['id'] in back: want[u['id']] = f'move {RALLY[0]} {RALLY[1]}'
    # the group's centre: the median of its fighters (clerics and the
    # wounded follow it, stragglers come back to it)
    core = [u for u in mine if u['type'] != CLERIC and u['id'] not in back] or mine
    cx = sorted(u['x'] for u in core)[len(core) // 2]; cy = sorted(u['y'] for u in core)[len(core) // 2]
    centre = dict(x=cx, y=cy, w=1)
    # clerics heal the most hurt friend in reach, else keep up with the group
    for c in [u for u in mine if u['type'] == CLERIC]:
        hurt = sorted([u for u in us if u['owner'] == 0 and u['hp'] < u['maxhp'] - 8 and d(u, c) <= 6],
                      key=lambda u: u['hp'] / u['maxhp'])
        if hurt and c['mana'] >= 6: want[c['id']] = f'heal {hurt[0]["id"]}'
        elif d(c, centre) > 3: want[c['id']] = f'move {cx} {cy}'
    # conjurers: rain on a cluster clear of us; summon scorpions when idle
    for c in [u for u in mine if u['type'] == 10 and u['id'] not in want]:
        best = None
        for f in foes:
            if f['type'] >= 32 or d(c, f) > 9: continue
            n = sum(1 for g in foes if g['type'] < 32 and max(abs(g['x'] - f['x']), abs(g['y'] - f['y'])) <= 1)
            if n >= 3 and not any(max(abs(m['x'] - f['x']), abs(m['y'] - f['y'])) <= 3 for m in mine):
                if not best or n > best[0]: best = (n, f)
        if best and c['mana'] >= 50: want[c['id']] = f"rainxy {best[1]['x']} {best[1]['y']}"
        elif not foes and c['mana'] >= 200: want[c['id']] = f"summon {os.environ.get('SUMMON', '30')}"
    # targets: each fighter its own, by priority then distance
    load = {}
    fighters = [u for u in mine if u['id'] not in want]
    near = [f for f in foes if d(centre, f) <= SIGHT or any(d(u, f) <= 5 for u in mine)]
    # stragglers (far from the centre, nothing to fight there) come back
    for u in fighters:
        if d(u, centre) > SIGHT - 2 and not any(d(u, f) <= 3 for f in near):
            want[u['id']] = f'move {cx} {cy}'
    fighters = [u for u in fighters if u['id'] not in want]
    # a catapult in range of us is ridden down at once by the fastest three
    # (it outranges the leash, and its rocks take a packed army apart)
    for c in [f for f in foes if f['type'] in CATAPULT and d(centre, f) <= 18]:
        riders = sorted([u for u in fighters if u['type'] not in ARCHER and u['type'] not in CASTER],
                        key=lambda u: (u['type'] not in (6, 7), d(u, c)))[:3]
        for u in riders: want[u['id']] = f'attack {c["id"]}'
        fighters = [u for u in fighters if u['id'] not in want]
        if c not in near: near.append(c)
    if LEASH:   # a defence: nothing beyond the leash around its point
        near = [f for f in foes if max(abs(f['x'] - LEASH[0]), abs(f['y'] - LEASH[1])) <= LEASH[2]
                or any(d(u, f) <= 4 for u in mine)]   # (whatever shoots at us is fought)
    for u in sorted(fighters, key=lambda u: min([d(u, f) for f in near] or [99])):
        cands = sorted(near, key=lambda f: (prio(f) + d(u, f) / 4, d(u, f)))
        if u['type'] in CATAPULT:   # (our rocks hit our own: only foes clear of us)
            cands = [f for f in cands if not any(d(m, f) <= 2 for m in mine if m['type'] not in CATAPULT)]
            if not cands:
                if SIEGE: want[u['id']] = f'attackxy {SIEGE[0]} {SIEGE[1]}'
                continue
        cands = [f for f in cands if load.get(f['id'], 0) < max(CAP, f['hp'] // 40 + 1)] or cands   # (a daemon takes many)
        if cands:
            f = cands[0]; load[f['id']] = load.get(f['id'], 0) + 1
            want[u['id']] = f'attack {f["id"]}'
    cmds = []
    if near:
        for u in fighters:
            if u['id'] in want and (orders.get(u['id']) != want[u['id']] or u['state'] == 'idle'):
                cmds += ['sel ' + u['id'], want[u['id']]]
        for i, w in want.items():
            if i not in [u['id'] for u in fighters] and orders.get(i) != w:
                cmds += ['sel ' + i, w]
        step = 150
    else:
        if SIEGE:
            for u in mine:
                if u['type'] in CATAPULT and u['id'] not in want: want[u['id']] = f'attackxy {SIEGE[0]} {SIEGE[1]}'
        walk = [u for u in mine if u['id'] not in want]
        for i, w in want.items():
            if orders.get(i) != w: cmds += ['sel ' + i, w]
        if walk:
            cx = sorted(u['x'] for u in walk)[len(walk) // 2]; cy = sorted(u['y'] for u in walk)[len(walk) // 2]
            spread = max(max(abs(u['x'] - cx), abs(u['y'] - cy)) for u in walk)
            stuck = stuck + 1 if prev and abs(prev[0] - cx) + abs(prev[1] - cy) < 1 else 0
            prev = (cx, cy)
            while wp < len(wps) and (max(abs(cx - wps[wp][0]), abs(cy - wps[wp][1])) <= 3 or stuck >= 3):
                wp += 1; stuck = 0
            if wp >= len(wps) and os.environ.get('HOLD'):
                wp = len(wps) - 1   # (hold the last point: regroup there, wait for what comes)
                if os.environ.get('SPREAD'):
                    # SPREAD=n: stand in a grid n tiles apart round the point (a cloud or
                    # a rock takes few at a time)
                    n = int(os.environ['SPREAD']); side = max(1, int(len(walk) ** 0.5))
                    px, py = wps[-1]
                    for k, u in enumerate(sorted(walk, key=lambda u: u['id'])):
                        gx2 = px + (k % side - side // 2) * n; gy2 = py + (k // side - side // 2) * n
                        if max(abs(u['x'] - gx2), abs(u['y'] - gy2)) > 1 and orders.get(u['id']) != f'move {gx2} {gy2}':
                            cmds += [f"sel {u['id']}", f"move {gx2} {gy2}"]; want[u['id']] = f'move {gx2} {gy2}'
                    for i, w in want.items(): orders[i] = w
                    out = turn(300, cmds); print(f"t{head[1]} spread hold"); continue
            if wp >= len(wps): print(f"t{head[1]} waypoints done"); break
            if os.environ.get('SPREADWALK') and spread <= 3 * int(os.environ['SPREADWALK']) * max(1, int(len(walk) ** 0.5)):
                # SPREADWALK=n: march in a loose grid n tiles apart (a rain of fire takes few)
                n = int(os.environ['SPREADWALK']); side = max(1, int(len(walk) ** 0.5))
                for k, u in enumerate(sorted(walk, key=lambda u: u['id'])):
                    gx2 = min(63, max(0, wps[wp][0] + (k % side - side // 2) * n)); gy2 = min(63, max(0, wps[wp][1] + (k // side - side // 2) * n))
                    g = f'attackxy {gx2} {gy2}'; want[u['id']] = g
                    if orders.get(u['id']) != g or u['state'] == 'idle': cmds += ['sel ' + u['id'], g]
            else:
                if spread > 6:
                    g = f'attackxy {cx} {cy}'
                else:
                    g = f'attackxy {wps[wp][0]} {wps[wp][1]}'
                for u in walk: want[u['id']] = g
                if any(orders.get(u['id']) != g or u['state'] == 'idle' for u in walk):
                    cmds += ['sel ' + ' '.join(u['id'] for u in walk), g]
        step = 300
    for i, w in want.items(): orders[i] = w
    out = turn(step, cmds)
    for l in out.splitlines():
        if 'kernel' in l or 'Assertion' in l or l.startswith('agent:'): print(l)
    hp = sum(u['hp'] for u in mine)
    print(f"t{head[1]} group {len(group)} hp {hp} foes {len(foes)} near {len(near)} back {len(back)} cmds {len(cmds) // 2}")
print(out)
