"""Rank remaining per-eye packet differences using owned current-frame snapshots.

Dropping a field below is a diagnostic hypothesis, never permission to merge it.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct


def read_capture(path):
    source = json.loads(path.read_text())
    if source['full'] or source.get('inputBytes') != 256 or source.get('inputFailure'):
        raise ValueError('Need a complete resolved 256-byte packet capture')
    packets = (path.parent / source['packetDataFile']).read_bytes()
    inputs = (path.parent / source['inputDataFile']).read_bytes()
    payload = (path.parent / source['inputBlobFile']).read_bytes()
    if len(packets) % 16 or len(inputs) != len(packets) * 16:
        raise ValueError('Packet/input span mismatch')
    groups = defaultdict(lambda: {0: [], 1: []})
    incomplete = Counter()
    for row in source['records']:
        if row['status'] not in (0, 1) or not row['metadataFlags'] & 4 or row['side'] not in (0, 1):
            raise ValueError('Unclassified/failed packet consumer')
        key = (row['frame'], row['node'], row['groupPlane'], row['groupVariant'], row['groupFlags'], tuple(row['modes']))
        if row['offset'] < 0 or row['count'] < 0 or (row['offset'] + row['count']) * 16 > len(packets):
            raise ValueError('Consumer outside packet storage')
        for index in range(row['offset'], row['offset'] + row['count']):
            data = inputs[index * 256:(index + 1) * 256]
            valid, count, program, origin = struct.unpack_from('<4I', data)
            if valid != 127:
                incomplete[str(valid)] += 1
                continue
            at, size, stride, program_at, program_size, _ = struct.unpack_from('<6I', data, 216)
            if count == 0 or size != count * stride or stride not in (48, 64) or at + size > len(payload) or program_size != program or program_at + program_size > len(payload):
                raise ValueError('Malformed owned input payload')
            a, b = struct.unpack_from('<QQ', packets, index * 16)
            a &= ~(((1 << 39) - 1) << 17)
            b &= ~(((1 << 18) - 1) | (((1 << 17) - 1) << 33))
            state = bytearray(data[88:128])
            state[16:24] = bytes(8)  # Owned bytes replace the temporary program pointer.
            fields = dict(flags=struct.pack('<QQ', a, b), draw=data[16:80], shaders=data[80:88],
                          state=bytes(state), object=data[128:152],
                          program=payload[program_at:program_at + program_size], instances=payload[at:at + size])
            groups[key][row['side']].append(dict(index=index, fields=fields, count=count, stride=stride, origin=origin))
    return groups, incomplete


def compare(eyes, fields):
    counters = [Counter(tuple(row['fields'][name] for name in fields) for row in eyes[side]) for side in (1, 0)]
    return sum((counters[0] & counters[1]).values())


def audit(path):
    groups, incomplete = read_capture(path)
    fields = ('flags', 'draw', 'shaders', 'state', 'object', 'program', 'instances')
    report = []
    for key, eyes in sorted(groups.items()):
        base = compare(eyes, fields)
        report.append(dict(frame=key[0], plane=key[2], variant=key[3], flags=key[4], modes=key[5],
                           complete=[len(eyes[s]) for s in (1, 0)], exact=base,
                           omitOne={name: compare(eyes, [f for f in fields if f != name]) for name in fields},
                           onlyOne={name: compare(eyes, [name]) for name in fields}))
    return dict(capture=str(path), incomplete=dict(incomplete), groups=report,
                warning='Relaxed matches identify fields to investigate. No rendering eligibility follows.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.capture)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
