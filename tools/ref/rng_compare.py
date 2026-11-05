#!/usr/bin/env python3
"""Compare the final mission's RNG logs over a required number of ticks."""
import argparse
import sys


def read_run(path):
    ticks = []
    with open(path) as source:
        for number, line in enumerate(source, 1):
            fields = line.split()
            if len(fields) != 2:
                raise ValueError(f'{path}:{number}: expected tick and seed')
            tick, seed = int(fields[0]), int(fields[1], 16)
            if tick == 1:
                ticks = []
            if tick != len(ticks) + 1:
                raise ValueError(f'{path}:{number}: nonconsecutive tick {tick}')
            ticks.append(seed)
    return ticks


def compare(ref, port, ticks):
    if min(len(ref), len(port)) < ticks:
        raise ValueError(f'need {ticks} ticks; original has {len(ref)}, port has {len(port)}')
    return next((i + 1 for i in range(ticks) if ref[i] != port[i]), None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ref')
    parser.add_argument('port')
    parser.add_argument('ticks', type=int)
    parser.add_argument('--mission', default='mission')
    args = parser.parse_args()
    if args.ticks < 1:
        parser.error('ticks must be positive')
    try:
        split = compare(read_run(args.ref), read_run(args.port), args.ticks)
    except (OSError, ValueError) as error:
        print(f'{args.mission} INVALID: {error}', file=sys.stderr)
        return 2
    print(f'{args.mission} {split if split is not None else "=" + str(args.ticks)}')
    return int(split is not None)


if __name__ == '__main__':
    sys.exit(main())
