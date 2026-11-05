#!/usr/bin/env python3
# macro.py DIR HALL_ID BARRACKS_ID MINE_ID ARMY_TARGET [TURNS]: my build-up
# policy, one agent turn at a time (local helper)
import sys, subprocess, re, os
A, hall, barracks, mines, target = sys.argv[1], sys.argv[2], sys.argv[3].split(','), sys.argv[4].split(','), int(sys.argv[5])
mine = mines[0]
turns = int(sys.argv[6]) if len(sys.argv) > 6 else 200
here = os.path.dirname(os.path.abspath(__file__))
trained = 0
jobs = {}
upgrades = [tuple(u.split(':')) for u in os.environ.get('UPGRADES', '').split() if u.count(':') == 1 and u[0] != ':']
upg_sent = {}
building_kinds = []
cmds = []
def parse(txt):
    head = re.search(r'tick (\d+) gold (-?\d+) wood (-?\d+) units (\d+) farms (\d+) food_ok (\d) result (\d)', txt)
    units = []
    for l in txt.splitlines():
        m = re.match(r'U id (\d+) type (\d+) (.+?) owner (\d) at (\d+),(\d+) size \S+ hp (\d+)/(\d+) state (\S+)(.*)', l)
        if m: units.append(dict(id=m[1], type=int(m[2]), name=m[3], owner=int(m[4]), x=int(m[5]), y=int(m[6]),
                                hp=int(m[7]), state=m[9], rest=m[10]))
    return head, units
def trees(seq):
    rows = open(f'{A}/map.{seq}.txt').read().split()
    return [(x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c == 'T']
out = subprocess.run([f'{here}/turn.sh', A, '1'], capture_output=True, text=True).stdout
for t in range(turns):
    head, units = parse(out)
    orc = ' race 2 dudes' in out
    if not head: print(out); break
    tick, gold, wood, n, farms, food, result = map(int, head.groups())
    seq = int(open(f'{A}/ready').read().split()[0])
    mine_units = [u for u in units if u['owner'] == 0]
    hallu = next((u for u in mine_units if u['id'] == hall), None)
    hx, hy = (hallu['x'] + 1, hallu['y'] + 1) if hallu else (0, 0)
    army = [u for u in mine_units if u['type'] in (0, 1, 6, 7, 8, 9, 4, 5, 12, 13, 10, 11)]
    if os.environ.get('ELEMTARGET'): army = [u for u in army if u['type'] not in (10, 11, 27, 28)]
    # PARK=x,y: catapults stay there, out of the army (their rocks hit friends in melee)
    if os.environ.get('PARK'):
        px, py = map(int, os.environ['PARK'].split(','))
        for c in [u for u in army if u['type'] in (4, 5)]:
            if c['state'] in ('idle', 'attack') and max(abs(c['x'] - px), abs(c['y'] - py)) > 1:
                cmds_park = [f"sel {c['id']}", f"move {px} {py}"]
                subprocess.run([f'{here}/turn.sh', A, '1', *cmds_park], capture_output=True, text=True)
        army = [u for u in army if u['type'] not in (4, 5)]
    peasants = [u for u in mine_units if u['type'] in (2, 3)]
    enemies = [u for u in units if 1 <= u['owner'] <= 3]
    cmds.clear()
    near = [e for e in enemies if max(abs(e['x'] - hx), abs(e['y'] - hy)) <= int(os.environ.get('DEFEND', '14'))]
    # every hall is defended, and every worker under attack
    for hh in [u for u in mine_units if u['type'] in (40, 41)]:
        near += [e for e in enemies if e not in near and max(abs(e['x'] - hh['x'] - 1), abs(e['y'] - hh['y'] - 1)) <= 10]
    for pz in peasants:
        close = [e for e in enemies if e['type'] < 32 and max(abs(e['x'] - pz['x']), abs(e['y'] - pz['y'])) <= 5]
        if close and os.environ.get('WORKERSFLEE'):
            cmds += [f"sel {pz['id']}", f"move {hx} {hy + 3}"]   # (to the hall, not the army to the trees)
        elif close: near += [e for e in close if e not in near]
    # SENTRY=x,y: an archer posted there (seeing what comes); it counts as a guard too
    sentry = None
    if os.environ.get('SENTRY') and army:
        sx, sy = map(int, os.environ['SENTRY'].split(','))
        archers = [u for u in army if u['type'] in (8, 9)] or army
        sentry = min(archers, key=lambda u: max(abs(u['x'] - sx), abs(u['y'] - sy)))
        near += [e for e in enemies if e not in near and max(abs(e['x'] - sx), abs(e['y'] - sy)) <= 10]
    if os.environ.get('GUARD'):   # (and whatever can reach the guard: a catapult shells from 8)
        gx0, gy0 = map(int, os.environ['GUARD'].split(','))
        near += [e for e in enemies if e not in near and max(abs(e['x'] - gx0), abs(e['y'] - gy0)) <= 13]
    rode = set()
    cats = [e for e in near if e['type'] in (4, 5)]
    others = [e for e in near if e['type'] not in (4, 5)]
    if cats and not others and army:
        # a catapult alone: knights ride it down one by one (a column dies to its rocks),
        # the rest hold the guard
        for c in cats:
            # four riders each: knights first, else the nearest footmen
            melee = [u for u in army if u['type'] in (6, 7, 0, 1)]
            riders = sorted(melee, key=lambda u: (u['type'] not in (6, 7), max(abs(c['x'] - u['x']), abs(c['y'] - u['y']))))[:4]
            for k in riders: cmds += [f"sel {k['id']}", f"attack {c['id']}"]; rode.add(k['id'])
        near = []
    if near and army:
        # the defence is fight2's: each soldier its own target, catapults first
        gx, gy = map(int, os.environ.get('GUARD', f'{hx},{hy - 3}').split(','))
        env = dict(os.environ, RALLY=f'{hx},{hy + 2}', SIGHT=os.environ.get('SIGHT', '10'))
        env['LEASH'] = f"{gx},{gy},{os.environ.get('LEASHR', '10')}"
        e0 = min(near, key=lambda e: max(abs(e['x'] - gx), abs(e['y'] - gy)))
        assets = [u for u in mine_units if u['type'] in (40, 41) or (u['type'] in (2, 3) and not os.environ.get('WORKERSFLEE'))]
        threat = [e for e in near if any(max(abs(e['x'] - a['x']), abs(e['y'] - a['y'])) <= 6 for a in assets)]
        if threat:   # raiders at a hall or the workers: go there, leashed round them
            e0 = min(threat, key=lambda e: max(abs(e['x'] - gx), abs(e['y'] - gy)))
            env['LEASH'] = f"{e0['x']},{e0['y']},7"
        elif max(abs(e0['x'] - gx), abs(e0['y'] - gy)) > 8: e0 = dict(x=gx, y=gy)   # (hold: let it come)
        fighters_ids = [u['id'] for u in army] + [u['id'] for u in mine_units if u['type'] in (10, 11, 28)]   # (magic too)
        r = subprocess.run(['python3', f'{here}/fight2.py', A, ' '.join(dict.fromkeys(fighters_ids)), f"{e0['x']},{e0['y']} {gx},{gy}", os.environ.get('DEFTURNS', '8')],
                           capture_output=True, text=True, env=env).stdout
        fl = [l for l in r.splitlines() if l.startswith('t') and ' group ' in l]
        print(f"t{tick} defence: {fl[0] if fl else ''} .. {fl[-1] if fl else ''}", flush=True)
        out = r[r.rfind('tick '):]
        if any(not re.search(r' near 0 ', l) for l in fl):   # (it fought: a fresh look, the economy too)
            head, units = parse(out)
            if not head: print(out); break
            tick, gold, wood, n, farms, food, result = map(int, head.groups())
            mine_units = [u for u in units if u['owner'] == 0]
            army = [u for u in mine_units if u['type'] in (0, 1, 6, 7, 8, 9, 4, 5, 12, 13, 10, 11)]
            peasants = [u for u in mine_units if u['type'] in (2, 3)]
            if result: break
    elif os.environ.get('GUARD') and army:
        gx, gy = map(int, os.environ['GUARD'].split(','))
        if os.environ.get('SPREAD'):
            # each soldier its own spot in a grid round the guard (rocks and clouds take few)
            gap = int(os.environ['SPREAD']); guards = sorted([u for u in army if u is not sentry and u['id'] not in rode], key=lambda u: u['id'])
            side = max(1, int(len(guards) ** 0.5))
            for k, u in enumerate(guards):
                sx2 = min(63, max(0, gx + (k % side - side // 2) * gap)); sy2 = min(63, max(0, gy + (k // side - side // 2) * gap))
                if u['state'] == 'idle' and max(abs(u['x'] - sx2), abs(u['y'] - sy2)) > 1:
                    cmds += [f"sel {u['id']}", f"move {sx2} {sy2}"]
        else:
            idle = [u for u in army if u['state'] == 'idle' and max(abs(u['x'] - gx), abs(u['y'] - gy)) > 4 and u is not sentry
                    and u['id'] not in rode]
            if idle: cmds += ['sel ' + ' '.join(u['id'] for u in idle), f"attackxy {gx} {gy}"]
        if sentry and sentry['state'] == 'idle' and max(abs(sentry['x'] - sx), abs(sentry['y'] - sy)) > 1:
            cmds += [f"sel {sentry['id']}", f"move {sx} {sy}"]
    # the first mine of the list that still has gold
    try:
        ml = [l.split() for l in open(f'{A}/mines.txt')]
        alive = {l[2] for l in ml}
        # (all listed ones dry: the nearest mine left)
        near_mine = min(ml, key=lambda l: max(abs(int(l[4].split(',')[0]) - hx), abs(int(l[4].split(',')[1]) - hy)))[2] if ml else mine
        # (only the listed mines: the nearest one left may be the enemy's)
        nxt = next((m for m in mines if m in alive), near_mine if os.environ.get('ANYMINE') else mine)
        if nxt != mine:
            mine = nxt
            for k in list(jobs):
                if jobs[k] == 'gold': jobs[k] = None
    except FileNotFoundError: pass
    # each peasant keeps its job (gold or wood) between turns
    jobs = {k: v for k, v in jobs.items() if any(p['id'] == k for p in peasants)}
    def send(p, job):
        global cmds
        jobs[p['id']] = job
        if job == 'gold': cmds += [f"sel {p['id']}", f"harvest {mine}"]
        else:
            tr = sorted(trees(seq), key=lambda c: max(abs(c[0] - hx), abs(c[1] - hy)))
            pref = [tuple(map(int, t.split(','))) for t in os.environ.get('TREES', '').split()]
            pref = [t for t in pref if t in set(trees(seq))]
            if pref: tr = pref
            if tr: cmds += [f"sel {p['id']}", f"harvestxy {tr[0][0]} {tr[0][1]}"]
    for p in peasants:
        # (the job is what it does: a worker goes back to its last resource after a delivery)
        if p['state'] in ('wood', 'chopping'): jobs[p['id']] = 'wood'
        elif p['state'] in ('gold', 'mining'): jobs[p['id']] = 'gold'
        elif p['id'] not in jobs and p['state'] != 'idle': jobs[p['id']] = None
    if os.environ.get('GOLDERS'):
        # fixed roles: the first GOLDERS peasants (by id) mine, the rest chop; a
        # worker on the wrong resource (or idle) is sent to its own
        k = int(os.environ['GOLDERS'])
        if wood > int(os.environ.get('WOODCAP', '1500')): k = max(k, len(peasants) - 2)   # (lumber to spare: to the gold)
        if gold > int(os.environ.get('GOLDCAP', '5000')) and wood < 1000: k = min(k, 2)   # (gold to spare: to the trees)
        for i, p in enumerate(sorted(peasants, key=lambda p: int(p['id']))):
            role = 'gold' if i < k else 'wood'
            st = p['state']
            wrong = (role == 'gold' and st in ('wood', 'chopping')) or (role == 'wood' and st in ('gold', 'mining'))
            if st == 'idle' or wrong:
                if st == 'idle' and 'carries' in p['rest']: cmds += [f"sel {p['id']}", 'cmd 4']; continue
                send(p, role)
            else: jobs[p['id']] = role
        peasants_roles = True
    for p in peasants:
        if os.environ.get('GOLDERS'): break
        if p['state'] != 'idle': continue
        if 'carries' in p['rest']: cmds += [f"sel {p['id']}", 'cmd 4']; continue
        nw = sum(1 for v in jobs.values() if v == 'wood'); ng = sum(1 for v in jobs.values() if v == 'gold')
        send(p, jobs.get(p['id']) or ('wood' if nw < int(os.environ.get('CHOPPERS', '2')) and (nw <= ng or wood < 400) and wood < int(os.environ.get('WOODMIN', '800')) else 'gold'))
    miners = [k for k, v in jobs.items() if v == 'gold']; choppers = [k for k, v in jobs.items() if v == 'wood']
    # while lumber is short, miners go to the trees (at most 3 choppers)
    while not os.environ.get('GOLDERS') and wood < int(os.environ.get('WOODMIN', '800')) and len(choppers) < int(os.environ.get('CHOPPERS', '2')) and len(miners) > 1:
        k = miners.pop(); p = next(p for p in peasants if p['id'] == k)
        send(p, 'wood'); choppers.append(k)
    building_farms = sum(1 for u in mine_units if u['type'] in (32, 33) and 'building' in u['rest'])
    free = [p for p in peasants if p['state'] in ('chopping', 'wood', 'idle')]
    free += [p for p in peasants if p['state'] == 'gold' and p not in free]   # (walking miners last)
    free += [p for p in peasants if p['state'] in ('deliver', 'move') and p not in free]
    # a construction nobody works on gets a worker (repair continues it)
    for b in mine_units:
        if 'building' not in b['rest'] or b['state'] not in ('build', 'idle'): continue
        busy = any(p['state'] in ('repairing', 'repair') and abs(p['x'] - b['x']) <= 3 and abs(p['y'] - b['y']) <= 3 for p in peasants)
        if not busy and free:
            w = free.pop(0); cmds += [f"sel {w['id']}", f"repair {b['id']}"]; jobs.pop(w['id'], None)
    if os.environ.get('DEBUGTRAIN'): print(f"farm? n {n} farms {farms} bf {building_farms} gold {gold} wood {wood} free {len(free)} food {food}", flush=True)
    while not os.environ.get('NOFARM') and (not food or n + int(os.environ.get('FOODAHEAD', '2')) >= (farms + building_farms) * 4) and gold >= 500 and wood >= 300 and building_farms < int(os.environ.get('FARMSAT', '2')) and free:
        cands = free.pop(0)
        cmds += [f"sel {cands['id']}", f"buildnear 32 {hx} {hy - 4}"]; gold -= 500; wood -= 300; building_farms += 1
    # HALL2=x,y: a second town hall there once the first mine runs low (or is gone)
    if os.environ.get('HALL2') and free and gold >= 1200 and wood >= 450:
        try:
            left = {l.split()[2]: int(l.split()[6]) for l in open(f'{A}/mines.txt')}
        except (FileNotFoundError, IndexError, ValueError): left = {}
        if left.get(mines[0], 0) < 8000 and sum(1 for u in mine_units if u['type'] in (40, 41)) < 2:
            w = free.pop(0); bx, by = os.environ['HALL2'].split(',')
            cmds += [f"sel {w['id']}", f"build 40 {bx} {by}"]; gold -= 1200; wood -= 800
    # peasants first (the economy), then the WANT buildings in order at their
    # real price; what the next one needs is kept back from the units
    COST = {32: (500, 300), 34: (600, 500), 36: (800, 500), 38: (1400, 300), 40: (400, 400),
            42: (600, 500), 44: (1000, 400), 46: (900, 400)}
    np = len(peasants)
    for hh in [u for u in mine_units if u['type'] in (40, 41) and u['state'] == 'idle']:
        if food and gold >= 400 and np < int(os.environ.get('PEASANTS', '10')):
            cmds += [f"sel {hh['id']}", "train 2"]; gold -= 400; np += 1   # (every hall trains)
    if t % 4 == 0: building_kinds.clear()   # (an order that never started may go again)
    reserve_g = reserve_w = 0
    for kv in os.environ.get('WANT', '34:1').split():
        bt, cnt = map(int, kv.split(':'))
        have = sum(1 for u in mine_units if u['type'] in (bt, bt + 1))
        if have >= cnt or bt in building_kinds: continue
        cg, cw = COST.get(bt, (1000, 500))
        if gold >= cg and wood >= cw and free:
            w = free.pop(0); cmds += [f"sel {w['id']}", f"buildnear {bt} {os.environ.get('WANTAT', f'{max(1, hx - 6)} {max(1, hy - 6)}').replace(',', ' ')}"]
            gold -= cg; wood -= cw; building_kinds.append(bt)
        else:
            reserve_g, reserve_w = cg, cw   # (saving for it)
        break
    # (food tight and no farm going up: the next farm's price is kept back too)
    if n + 4 >= (farms + building_farms) * 4 and building_farms == 0:
        reserve_g += 500; reserve_w += 300
    gold -= reserve_g; wood -= reserve_w
    # UPGRADES="27:53 26:57 ..." (building:command), in order per building;
    # one is done once its building has been seen upgrading
    for b in mine_units:
        if b['state'] == 'upgrade' and b['id'] in upg_sent: upgrades.remove(upg_sent.pop(b['id']))
    for b in mine_units:
        if b['state'] != 'idle': continue
        mine_upg = [u for u in upgrades if u[0] == b['id'] or u[0] == f"t{b['type']}"]
        if mine_upg and gold >= 1000:
            cmds += [f"sel {b['id']}", f"cmd {mine_upg[0][1]}"]; upg_sent[b['id']] = mine_upg[0]; gold -= 1000
    barracks = list(dict.fromkeys(barracks + [u['id'] for u in mine_units if u['type'] in (34, 35, 36, 37, 38, 39, 44, 45) and u['state'] != 'build']))
    for bid in barracks:
        bar = next((u for u in mine_units if u['id'] == bid), None)
        # (a building with research still to start trains nothing: the research first)
        if bar and any(u[0] == bar['id'] or u[0] == f"t{bar['type']}" for u in upgrades): continue
        if os.environ.get('DEBUGTRAIN') and bar: print(f"train? {bid} {bar['type']} {bar['state']} food {food} gold {gold} army {len(army)}/{target} peas {len(peasants)}", flush=True)
        if bar and bar['state'] == 'idle' and food and gold >= 450 and len(army) < target and (len(peasants) >= int(os.environ.get('PEASANTS', '10')) or gold >= 1300 or near):
            # TRAIN="0 8" (default): the unit types to train in turn, per building
            kinds = [int(k) for k in os.environ.get('TRAIN', '0 8').split()]
            if bar['type'] == 44: kinds = [6]
            elif bar['type'] in (38, 39):
                kinds = [10]
                if sum(1 for u in mine_units if u['type'] in (10, 11)) >= int(os.environ.get('CONJURERS', '0')): continue
            elif bar['type'] in (36, 37):
                kinds = [12]
                if sum(1 for u in army if u['type'] in (12, 13)) >= int(os.environ.get('CLERICS', '6')): continue
            kind = kinds[trained % len(kinds)]
            cost = {6: 850, 12: 700, 10: 900}.get(kind, 450)
            if gold < cost: continue
            cmds += [f"sel {bid}", f"train {kind}"]; trained += 1; gold -= cost
    # ELEMTARGET=x,y: idle conjurers summon water elementals (out of the army),
    # and every four idle elementals are sent at the target
    if os.environ.get('ELEMTARGET'):
        tx, ty = os.environ['ELEMTARGET'].split(',')
        # (warlocks summon daemons, one caster a turn: a second order in the same turn is lost)
        for c in [u for u in mine_units if u['type'] in (10, 11) and u['state'] == 'idle' and 'mana 255' in u['rest']][:1]:
            cmds += [f"sel {c['id']}", "summon 32" if c['type'] == 10 else "summon 31"]
        el = [u for u in mine_units if u['type'] in (27, 28) and u['state'] == 'idle']
        if len(el) >= int(os.environ.get('ELEMGROUP', '4')): cmds += ['sel ' + ' '.join(u['id'] for u in el), f"attackxy {tx} {ty}"]
    cmds += [f'mines {A}/mines.txt']
    if orc:   # (the human type numbers above, for an orc player: the orc one is the next)
        cmds = [re.sub(r'^(train|buildnear|build) (\d+)', lambda m: f"{m[1]} {int(m[2]) + 1 if int(m[2]) % 2 == 0 and int(m[2]) != 28 else int(m[2])}", c) for c in cmds]
    out = subprocess.run([f'{here}/turn.sh', A, '600', *cmds], capture_output=True, text=True).stdout
    errs = [l for l in out.splitlines() if 'kernel' in l or 'Assertion' in l]
    if errs: print('\n'.join(errs))
    print(f"t{tick} gold {gold} wood {wood} units {n} farms {farms} army {len(army)} miners {len(miners)} choppers {len(choppers)} enemies {len(enemies)}", flush=True)
    if result or (len(army) >= target and not near):
        break
print(out)
