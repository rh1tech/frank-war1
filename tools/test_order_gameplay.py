#!/usr/bin/env python3
"""Compare the live kernel with the original, preserving the five-minute parity floor."""
# SPDX-License-Identifier: Zlib
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from ref.rng_compare import compare, read_run
from ai_compare import parse

ROOT = Path(__file__).resolve().parents[1]
TICKS = 10000
# Complete reference sweep after terrain commands, research and invisibility.
# Omitted missions match the whole window.
TILE_FLOORS = {}
RNG_FLOORS = {}


def first_tile_split(report, ticks=TICKS):
    first = report.splitlines()[0]
    match = re.search(r'\(tick (\d+)\)', first)
    if match:
        return int(match[1])
    if first == f'no divergence in {ticks} frames':
        return None
    raise ValueError(f'invalid tile report: {first}')


def last_seed(path):
    fields = path.read_text().splitlines()[-1].split()
    if len(fields) != 2 or fields[0] != 'seed':
        raise ValueError(f'invalid seed file: {path}')
    return int(fields[1], 16)


def play(mission, race, number, env, run_log, host):
    with run_log.open('w') as log:
        result = subprocess.run(
            [str(host), '--race', race, '--mission', number],
            cwd=ROOT / 'build/host/run', env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    if result.returncode:
        raise ValueError(f'{mission}: game exited {result.returncode}; see {run_log}')
    text = run_log.read_text()
    errors = [line for line in text.splitlines()
              if 'original kernel:' in line or 'AddressSanitizer' in line or 'runtime error:' in line]
    if errors:
        raise ValueError(f'{mission}: {errors[0]}; see {run_log}')
    terminal = re.search(r'campaign result: (win|lose) at tick (\d+)', text)
    return (terminal[1], int(terminal[2])) if terminal else None


def replay_digest(path, flags, ticks):
    frames = parse(path, flags)
    if [t for t, _ in frames] != list(range(1, ticks + 1)):
        raise ValueError(f'{path}: incomplete gameplay trace')
    digest = hashlib.sha256()
    for tick, units in frames:
        if not units:
            raise ValueError(f'{path}: missing units at tick {tick}')
        # Audio entities can change native IDs; compare simulation content.
        values = sorted(unit[:5] + unit[6:] for unit in units.values())
        digest.update(repr((tick, values)).encode())
    return digest.hexdigest()


MIN_ENDED = 20000   # the shortest original run accepted as an ended mission


def reference_window(mission, race, number, seed, ticks):
    ai = ROOT / 'build/ai'
    if ticks <= TICKS:
        return ai / f'ref_{mission}.txt', ai / f'ref/rr_{mission}.txt'
    out = ai / f'reference-{ticks}'
    out.mkdir(exist_ok=True)
    tiles, rng = out / f'{mission}.tiles', out / f'{mission}.rng'
    manifest = Path(str(tiles) + '.seed')
    if tiles.exists() and rng.exists() and manifest.exists():
        if last_seed(manifest) == seed and len(read_run(rng)) >= MIN_ENDED:
            return tiles, rng
    reference = Path(os.environ.get('REF', ROOT.parent / 'war1-reference/recomp'))
    y = 100 if race == 'orc' else 117
    quit_second = ticks // 300 + 60   # (the original runs about 330 ticks a second at speed 10; slower on busy maps)
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', WAR_MUTE='1', WAR_AIFIRST='1',
               WAR_RNGSEED=f'{seed:x}', WAR_SPEED_SETTING='3', WAR_AITILES='1', WAR_AIEVERY='1',
               WAR_AILOG=str(tiles), WAR_RNGLOG=str(rng),
               WAR_INPUT=f'8:click@160,100,11:click@160,100,14:click@160,{y},18:esc,'
                         f'24:return,25:type:corwin_of_amber,26:return,28:return,'
                         f'29:type:{race}_{number},30:return,36:esc,41:speed:10,{quit_second}:quit')
    run_log = out / f'{mission}.log'
    with run_log.open('w') as log:
        result = subprocess.run(
            [str(reference / 'build120/war'), 'work/game', 'build120/image.bin'],
            cwd=reference, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    if result.returncode:
        raise ValueError(f'{mission}: original exited {result.returncode}; see {run_log}')
    if last_seed(manifest) != seed:
        raise ValueError(f'{mission}: extended reference seed changed')
    # A mission can end inside a long window (the computer wins); the
    # comparison then stops there, and the port must end too.
    if len(read_run(rng)) < MIN_ENDED:
        raise ValueError(f'{mission}: extended reference lacks {MIN_ENDED} RNG ticks; see {run_log}')
    return tiles, rng


def output_directory(self_play, ticks, host):
    name = 'kernel-self-play' if self_play else 'kernel'
    if host != ROOT / 'build/host/war1-host':
        name += f'-{host.stem}'
    if ticks != TICKS:
        name += f'-{ticks}'
    return ROOT / 'build/ai' / name


def run_mission(mission, self_play=False, check_unlogged=False, ticks=TICKS,
                host=ROOT / 'build/host/war1-host'):
    match = re.fullmatch(r'(human|orc)([1-9]|1[0-2])', mission)
    if not match:
        raise ValueError(f'invalid mission: {mission}')
    race, number = match.groups()
    ai = ROOT / 'build/ai'
    seed = last_seed(ai / f'ref/ra_{mission}.txt.seed')
    requested = ticks
    if not self_play:
        reference, reference_rng = reference_window(mission, race, number, seed, ticks)
        original_rng = read_run(reference_rng)
        if len(original_rng) < ticks:
            ticks = len(original_rng)   # (the original's mission ended)
        baseline_rng = RNG_FLOORS.get(mission)
        baseline_tile = TILE_FLOORS.get(mission)
        if last_seed(Path(str(reference) + '.seed')) != seed:
            raise ValueError(f'{mission}: tile and RNG reference seeds differ')

    out = output_directory(self_play, requested, host)
    out.mkdir(exist_ok=True)
    script = out / f'{mission}.input'
    script.write_text(f'{(requested + 1000) * 9 // 5} quit\n')
    run_log = out / f'{mission}.log'
    tile_log = out / f'{mission}.tiles'
    rng_log = out / f'{mission}.rng'
    env = os.environ.copy()
    for name in ('WAR_WINDOW', 'WAR_RECORD', 'WAR_AI_HUMAN'):
        env.pop(name, None)
    env.update(WAR_FIXED='1', WAR_SCRIPT=str(script), WAR_RNGSEED=f'{seed:x}',
               WAR_AITILES='1', WAR_AIEVERY='1', WAR_AILOG=str(tile_log), WAR_RNGLOG=str(rng_log))
    if self_play:
        env['WAR_AI_HUMAN'] = '1'
    terminal = play(mission, race, number, env, run_log, host)

    flags, counts, samples = None, (0, 0, 0, 0, 0), 0
    with tile_log.open() as log:
        for line in log:
            if flags is None and line.startswith('ms '):
                found = re.search(r'flags (\w+)', line)
                if found:
                    flags = found[1]
            if line.startswith('K '):
                fields = line.split()
                if len(fields) != 11 or fields[1::2] != ['idle', 'move', 'attack', 'work', 'spell']:
                    raise ValueError(f'{mission}: invalid kernel counters')
                current = tuple(int(value) for value in fields[2::2])
                if any(new < old for new, old in zip(current, counts)):
                    raise ValueError(f'{mission}: kernel counters went backwards')
                counts, samples = current, samples + 1
    end_split = None
    if not self_play and ticks < requested:
        # The original's mission ended: the port must end too, no later (the
        # original checks every 321 polls of its main loop, a few ticks late
        # on a fast machine; the port checks every tick). Otherwise the end
        # itself is a split, reported after any earlier one.
        if terminal and terminal[1] <= ticks:
            ticks = terminal[1]
        else:
            end_split = ticks
    elif not self_play and terminal and terminal[1] < ticks:
        end_split = ticks = terminal[1]   # (the port ended, the original did not)
    if flags is None or not samples or not counts[0] or (
            samples < ticks and not terminal):
        raise ValueError(f'{mission}: missing live-kernel coverage')
    kernel = dict(zip(('idle', 'move', 'attack', 'work', 'spell'), counts))
    if check_unlogged:
        digest = replay_digest(tile_log, flags, samples)
        unlogged_tiles = out / f'{mission}.without-rng-log.tiles'
        unlogged_env = env.copy()
        unlogged_env.pop('WAR_RNGLOG', None)
        unlogged_env['WAR_AILOG'] = str(unlogged_tiles)
        unlogged_terminal = play(
            mission, race, number, unlogged_env, out / f'{mission}.without-rng-log.log', host)
        if (unlogged_terminal != terminal or
                replay_digest(unlogged_tiles, flags, samples) != digest):
            raise ValueError(f'{mission}: disabling RNG logging changed gameplay')
    if self_play:
        rng = read_run(rng_log)
        if len(rng) != samples:
            raise ValueError(f'{mission}: missing self-play RNG coverage')
        digest = replay_digest(tile_log, flags, samples)
        replay_tiles = out / f'{mission}.replay.tiles'
        replay_rng = out / f'{mission}.replay.rng'
        env.update(WAR_AILOG=str(replay_tiles), WAR_RNGLOG=str(replay_rng))
        replay_terminal = play(mission, race, number, env, out / f'{mission}.replay.log', host)
        if (replay_terminal != terminal or read_run(replay_rng) != rng or
                replay_digest(replay_tiles, flags, samples) != digest):
            raise ValueError(f'{mission}: AI-versus-AI replay is not deterministic')
        return dict(mission=mission, seed=f'{seed:x}', ticks=samples,
                    terminal=terminal, replay='matched', kernel=kernel)

    result = subprocess.run(
        [sys.executable, str(ROOT / 'tools/ai_compare.py'), str(reference), str(tile_log),
         '--flags', flags, '--tolerance', '0', '--ticks', str(ticks)],
        text=True, capture_output=True, timeout=120)
    if result.returncode not in (0, 1):
        raise ValueError(f'{mission}: {result.stderr.strip()}')
    (out / f'{mission}.comparison').write_text(result.stdout)
    tile = first_tile_split(result.stdout, ticks) or end_split
    rng = compare(original_rng, read_run(rng_log), ticks)
    for label, old, new in (('tiles', baseline_tile, tile), ('RNG', baseline_rng, rng)):
        if (new or ticks + 1) < (old or min(ticks, TICKS) + 1):
            raise ValueError(f'{mission}: {label} regressed from {old or "="} to {new}')
    return dict(mission=mission, seed=f'{seed:x}', ticks=ticks, tile=tile, rng=rng,
                baseline_tile=baseline_tile, baseline_rng=baseline_rng,
                terminal=terminal, kernel=kernel)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-play', action='store_true',
                        help='paid AI-versus-AI stress and deterministic replay, not original parity')
    parser.add_argument('--check-unlogged', action='store_true',
                        help='also replay without RNG logging and require identical unit-state traces')
    parser.add_argument('--ticks', type=int, default=TICKS,
                        help='comparison window; longer runs cache fresh original traces separately')
    parser.add_argument('--host', type=Path, default=ROOT / 'build/host/war1-host',
                        help='host executable; permits isolated builds during a frozen baseline run')
    parser.add_argument('missions', nargs='*', help='e.g. human1 human6 orc5; default: all 24')
    args = parser.parse_args()
    if args.ticks < TICKS:
        parser.error(f'--ticks must be at least {TICKS}')
    args.host = args.host.resolve()
    if not args.host.is_file() or not os.access(args.host, os.X_OK):
        parser.error(f'--host is not an executable file: {args.host}')
    missions = args.missions or [f'{race}{n}' for race in ('human', 'orc') for n in range(1, 13)]
    results = []
    for mission in missions:
        try:
            result = run_mission(mission, args.self_play, args.check_unlogged, args.ticks, args.host)
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            print(f'FAILED: {error}', file=sys.stderr)
            return 1
        results.append(result)
        if args.self_play:
            print(f'{mission}: {result["ticks"]} ticks, {result["terminal"] or "window complete"}, '
                  f'replay matched; kernel {result["kernel"]}', flush=True)
        else:
            print(f'{mission}: tile {result["tile"] or "="}, RNG {result["rng"] or "="}, '
                  f'window {result["ticks"]}, end {result["terminal"]}; '
                  f'kernel {result["kernel"]}', flush=True)
    out = output_directory(args.self_play, args.ticks, args.host)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    if not args.missions and any(not sum(r['kernel'][kind] for r in results)
                                 for kind in ('idle', 'move', 'attack', 'work', 'spell')):
        print('FAILED: a live script path was not exercised', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
