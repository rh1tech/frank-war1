# Non-halting PC-sampling profiler: reads a core's DWT_PCSR over SWD many
# times and reports the hottest functions and a per-area breakdown.
# usage: profile.py <elf> [samples] [core 0|1]
import bisect, re, subprocess, sys
from collections import Counter

E = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
core = sys.argv[3] if len(sys.argv) > 3 else '0'

syms = []
for line in subprocess.run(['arm-none-eabi-nm', '-n', '-S', '--defined-only', E],
                           capture_output=True, text=True).stdout.splitlines():
    parts = line.split()
    if len(parts) == 4 and parts[2] in 'tTwW':
        syms.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
syms.sort()
starts = [s[0] for s in syms]

def lookup(pc):
    i = bisect.bisect_right(starts, pc) - 1
    if i >= 0 and pc < syms[i][0] + max(syms[i][1], 2):
        return syms[i][2]
    return '?'

import os
SERIAL = os.environ.get('PROBE', 'E6635C08CB46BB26')
cmds = ['-c', f'adapter serial {SERIAL}', '-c', 'adapter speed ' + os.environ.get('SWD_KHZ','4000') + '', '-c', 'init', '-c', f'targets rp2350.cm{core}']
for _ in range(N):
    cmds += ['-c', 'echo p:[capture {mdw 0xe000101c 1}]']
cmds += ['-c', 'exit']
out = subprocess.run(['openocd', 
                      '-f', 'interface/cmsis-dap.cfg', '-f', 'target/rp2350.cfg']
                     + cmds, capture_output=True, text=True).stderr
pcs = [int(m, 16) for m in re.findall(r'^p:0x[0-9a-f]+: ([0-9a-f]+)', out, re.M)]
funcs = Counter(lookup(pc & ~1) for pc in pcs)

AREAS = [
    ('68000 core (Musashi)', r'^(m68k|m68ki|OPER_|EA_)'),
    ('memory access', r'^(chip(Read|Write|Fetch)|ram_|rom_|slow_|cpu_(read|write))'),
    ('chipset DMA slots', r'^(dma_run|evenCycle|oddCycle|spriteCycle|bitplaneActive|plane\d|loresPlane1|hiresPlane1|copper|dmaUpdate|dmaFetch|dmaEndOfLine|diskCycle|dramCycle|audio\dCycle|hiresDisplay|sprite)'),
    ('blitter', r'^(blitter|Blitter|blit)'),
    ('CIA', r'^(CIA|eclock)'),
    ('video host / ring', r'^(hostDirect|ringPush|hostRaster|host|c1|scan_|dvi_|hstx_)'),
    ('floppy', r'^(floppy|ADF2MFM|mfm)'),
]
areas = Counter()
for f, n in funcs.items():
    for name, pat in AREAS:
        if re.match(pat, f):
            areas[name] += n
            break
    else:
        areas['other'] += n

total = max(len(pcs), 1)
print(f'{len(pcs)} samples, core {core}')
print('-- areas')
for name, n in areas.most_common():
    print(f'{100 * n / total:5.1f}%  {name}')
print('-- top functions')
for f, n in funcs.most_common(20):
    print(f'{100 * n / total:5.1f}%  {f}')
