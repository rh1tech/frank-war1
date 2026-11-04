#!/usr/bin/env python3
"""profile_lines.py <elf> [samples] [probe serial] [core] — non-halting PC
sampling over SWD (DWT_PCSR), reported per function and per source line."""
import re, subprocess, collections, sys
E = sys.argv[1]; N = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
SER = sys.argv[3] if len(sys.argv) > 3 else 'E6635C08CB46BB26'
core = sys.argv[4] if len(sys.argv) > 4 else '0'
speed = '1000' if SER == 'E6616407E335BB29' else '4000'
cmds = ['-c', f'adapter serial {SER}', '-c', f'adapter speed {speed}', '-c', 'init', '-c', f'targets rp2350.cm{core}']
for _ in range(N): cmds += ['-c', 'echo p:[capture {mdw 0xe000101c 1}]']
cmds += ['-c', 'exit']
out = subprocess.run(['openocd', '-f', 'interface/cmsis-dap.cfg', '-f', 'target/rp2350.cfg'] + cmds,
                     capture_output=True, text=True).stderr
pcs = [int(m, 16) & ~1 for m in re.findall(r'^p:0x[0-9a-f]+: ([0-9a-f]+)', out, re.M)]
c = collections.Counter(pcs)
top = [pc for pc, _ in c.most_common(600)]
res = subprocess.run(['arm-none-eabi-addr2line', '-f', '-e', E] + ['0x%x' % p for p in top],
                     capture_output=True, text=True).stdout.split('\n')
lines = collections.Counter(); funcs = collections.Counter()
for i, pc in enumerate(top):
    fn = res[2 * i]; loc = res[2 * i + 1].split('/')[-1]
    lines[fn + ' ' + loc] += c[pc]; funcs[fn] += c[pc]
n = max(len(pcs), 1)
print(n, 'samples')
for k, v in funcs.most_common(15): print('%5.1f%% %s' % (100 * v / n, k))
print('--- lines')
for k, v in lines.most_common(20): print('%5.1f%% %s' % (100 * v / n, k))
