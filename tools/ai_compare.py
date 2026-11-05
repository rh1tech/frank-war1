#!/usr/bin/env python3
"""ai_compare.py REF.log PORT.log [--flags 0002] [--every 60]

Compare a mission played by the original (war1-reference runtime, WAR_AILOG
with WAR_AITILES) with the same mission in the port (host build, WAR_AILOG
with WAR_AITILES), unit by unit, tile by tile.

Both logs hold a line "ms ... t TICK ... flags F" every 33 game ticks, then
one "U id type owner x y order target hp" per unit (the original's numbers
for types). Units are paired at the mission's first line by type, owner and
tile; units born later by type and owner, nearest first. Reports the first
tick where a pair stands on different tiles (or exists on one side only),
and a per-minute summary of how far the two games have drifted.

Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
SPDX-License-Identifier: Zlib
"""
import argparse, re, collections, sys

def parse(path, flags):
    frames = []          # (tick, {id: (type, owner, x, y, order, target, hp)})
    cur = None
    with open(path) as source:
        for line in source:
            if line.startswith('ms '):
                m = re.search(r' t (\d+) .*flags (\w+)', line)
                if not m:
                    continue
                if m.group(2).lower() != flags.lower():
                    cur = None
                    continue
                t = int(m.group(1))
                if frames and (cur is None or t <= frames[-1][0]):
                    frames = []  # the cheat may restart mission 1 with the same flags
                cur = {}
                frames.append((t, cur))
            elif line.startswith('U ') and cur is not None:
                p = line.split()
                if len(p) >= 9:
                    cur[int(p[1])] = tuple(int(v) for v in p[2:9])
    return frames

def identities(frames):
    """Per frame {key: unit}, the key a unit keeps for its life: (id, birth)."""
    out, birth, last = [], {}, {}
    for t, units in frames:
        cur = {}
        for i, u in units.items():
            if i not in last or last[i][:2] != u[:2]:
                birth[i] = t
            cur[(i, birth[i])] = u
        last = units
        out.append((t, cur))
    return out

def paired_extras(pair, reference, port):
    live_pairs = {pair[k] for k in reference if k in pair}
    return [pk for pk in port if pk not in live_pairs]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref'); ap.add_argument('port')
    ap.add_argument('--flags', default='0002')
    ap.add_argument('--every', type=int, default=60, help='summary period, seconds')
    ap.add_argument('--tolerance', type=int, default=1, help='tiles apart that still count as the same place')
    ap.add_argument('--ticks', type=int, help='require every tick from 1 through this tick')
    a = ap.parse_args()
    ref = identities(parse(a.ref, a.flags))
    port = identities(parse(a.port, a.flags))
    if a.ticks is not None:
        if a.ticks < 1:
            ap.error('--ticks must be positive')
        required = list(range(1, a.ticks + 1))
        for label, frames in (('original', ref), ('port', port)):
            if [t for t, _ in frames[:a.ticks]] != required:
                ap.error(f'{label}: incomplete per-tick coverage through tick {a.ticks}')
        ref, port = ref[:a.ticks], port[:a.ticks]
    n = min(len(ref), len(port))
    if not n:
        ap.error('no common frames')
    if [t for t, _ in ref[:n]] != [t for t, _ in port[:n]]:
        ap.error('sample ticks differ between original and port')
    if any(not units for _, units in ref[:n] + port[:n]):
        ap.error('missing unit records in a comparison frame')
    pair = {}            # ref key -> port key
    used = set()
    # the level's units: same type, owner, tile
    _, r0 = ref[0]; _, p0 = port[0]
    bypos = collections.defaultdict(list)
    for k, u in p0.items():
        bypos[(u[0], u[1], u[2], u[3])].append(k)
    for k, u in sorted(r0.items()):
        c = bypos.get((u[0], u[1], u[2], u[3]))
        if c:
            pair[k] = c.pop(0); used.add(pair[k])
    first = None
    summary = []
    for f in range(n):
        t, ru = ref[f]; _, pu = port[f]
        # newborns: by type and owner, nearest
        for k, u in ru.items():
            if k in pair:
                continue
            best, bd = None, 1 << 30
            for pk, v in pu.items():
                if pk in used or v[0] != u[0] or v[1] != u[1]:
                    continue
                d = max(abs(v[2] - u[2]), abs(v[3] - u[3]))
                if d < bd:
                    best, bd = pk, d
            if best is not None:
                pair[k] = best; used.add(best)
        devs, missing = [], 0
        for k, u in ru.items():
            pk = pair.get(k)
            v = pu.get(pk) if pk else None
            if v is None:
                missing += 1
                if first is None:
                    first = (t, 'only in the original', k, u, None, None)
                continue
            d = max(abs(v[2] - u[2]), abs(v[3] - u[3]))
            devs.append(d)
            if d > a.tolerance and first is None:
                first = (t, '%d tiles apart' % d, k, u, pk, v)
        extras = paired_extras(pair, ru, pu)
        extra = len(extras)
        if extra and first is None:
            pk = extras[0]
            first = (t, 'only in the port', None, None, pk, pu[pk])
        summary.append((t, devs, missing, extra))
    if first:
        t, what, k, u, pk, v = first
        seconds = (t - 1) * 30 // 1000
        print(f'first divergence at {seconds // 60}:{seconds % 60:02d} (tick {t}): {what}')
        fmt = lambda x: 'type %d owner %d at %d,%d order/state %d target %d hp %d' % x if x else '-'
        print('  original', k, fmt(u))
        print('  port    ', pk, fmt(v))
    else:
        print(f'no divergence in {n} frames')
    next_summary = 0
    for t, devs, missing, extra in summary:
        milliseconds = (t - 1) * 30
        if milliseconds < next_summary:
            continue
        next_summary = milliseconds + max(a.every, 1) * 1000
        seconds = milliseconds // 1000
        far = sum(1 for d in devs if d > a.tolerance)
        mean = sum(devs) / len(devs) if devs else 0
        print(f'{seconds // 60:3d}:{seconds % 60:02d}  paired {len(devs):3d}  apart {far:3d}  mean {mean:5.2f}  max {max(devs) if devs else 0:3d}  only-original {missing:3d}  only-port {extra:3d}')
    return int(first is not None)

if __name__ == '__main__':
    sys.exit(main())
