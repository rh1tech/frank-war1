#!/usr/bin/env python3
# route.py FULLMAP X,Y X,Y [STEP]: waypoints along the shortest open path
import sys
from collections import deque
rows = open(sys.argv[1]).read().split()
s = tuple(map(int, sys.argv[2].split(','))); g = tuple(map(int, sys.argv[3].split(',')))
step = int(sys.argv[4]) if len(sys.argv) > 4 else 7
ok = lambda x, y: 0 <= x < 64 and 0 <= y < 64 and rows[y][x] in '.E'
prev = {s: None}; q = deque([s]); c = None
while q:
    c = q.popleft()
    if max(abs(c[0] - g[0]), abs(c[1] - g[1])) <= 1: break
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            n = (c[0] + dx, c[1] + dy)
            if n not in prev and ok(*n): prev[n] = c; q.append(n)
else: sys.exit('no path')
p = []
while c: p.append(c); c = prev[c]
p = p[::-1]
print(' '.join(f'{x},{y}' for x, y in p[step::step] + [p[-1]]))
