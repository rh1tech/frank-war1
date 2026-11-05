#!/usr/bin/env python3
# toscript.py DIR OUT: an agent game's turns as a frame-stamped host script
# (WAR_SCRIPT; replay with WAR_FIXED=1, add WAR_WINDOW=1 to watch)
import sys, os, re
d, out = sys.argv[1], sys.argv[2]
frame, lines, seq = 2, [], 0
while os.path.exists(f'{d}/cmd.{seq}.txt'):
    nxt = frame
    for l in open(f'{d}/cmd.{seq}.txt'):
        l = l.rstrip('\n')
        if not l: continue
        m = re.match(r'0 (.*)', l)
        if not m: continue   # (a stray line: the game skipped it too)
        cmd = m.group(1)
        if cmd.startswith('run '): nxt = frame + int(cmd.split()[1]); continue
        if cmd.startswith('ai ag state') or cmd.startswith('ai ag map'): continue
        lines.append(f'{frame} {cmd}')
    frame, seq = nxt, seq + 1
lines.append(f'{frame + 600} quit')
open(out, 'w').write('\n'.join(lines) + '\n')
print(f'{seq} turns, {len(lines)} lines, ends at frame {frame}')
