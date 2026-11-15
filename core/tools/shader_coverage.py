#!/usr/bin/env python3
"""Compare recorded and prepared input IDs offline; never launches the game.

The difference is historical recording coverage, not a particular frame's misses.
Usage: python3 core/tools/shader_coverage.py /path/to/cache-directory
"""
import argparse
import json
import struct
from pathlib import Path


def read_ids(path, magic, size, offset):
    data = Path(path).read_bytes()
    if len(data) < 16 or struct.unpack_from('<8sII', data) != (magic, 1, size):
        raise ValueError(f'{path}: invalid magic, version or record size')
    if (len(data) - 16) % size:
        raise ValueError(f'{path}: truncated record')
    ids = set()
    for start in range(16, len(data), size):
        stage = data[start]
        iid = data[start + offset:start + offset + 32]
        if stage >= 6 or not any(iid):
            raise ValueError(f'{path}: invalid stage or zero input ID at {start}')
        ids.add(iid.hex())
    return (len(data) - 16) // size, ids


def coverage(directory):
    directory = Path(directory)
    nr, recorded = read_ids(directory / 'shader-answers.bin', b'TOTKSANS', 216, 36)
    np, prepared = read_ids(directory / 'shader-prepared.bin', b'TOTKSPRP', 36, 4)
    return {
        'recorded_records': nr, 'prepared_records': np,
        'recorded_unique_inputs': len(recorded), 'prepared_unique_inputs': len(prepared),
        'recorded_inputs_in_prepared': len(recorded & prepared),
        'recorded_inputs_not_in_prepared': sorted(recorded - prepared),
        'scope': 'historical input IDs; not per-frame misses or proof of missing SPIR-V',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        result = coverage(args.directory)
    except (OSError, ValueError) as exc:
        parser.exit(2, f'{exc}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
