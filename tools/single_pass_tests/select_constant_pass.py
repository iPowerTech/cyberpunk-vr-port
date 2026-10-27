"""Choose owned constants for unambiguous geometry pairs from one native pass."""
import base64
from collections import defaultdict


def pass_key(row):
    group = bytearray(base64.b64decode(row['groupBase64'], validate=True))
    if len(group) != 48:
        raise ValueError('Invalid native pass metadata')
    for offset in (13, 21, 33):
        group[offset:offset+3] = bytes(3)
    return (row['frame'], row['node'], row['argumentPlane'], tuple(row['modes']), bytes(group[:40]))


def select_pass(draws, eligible, mapped_pairs, records):
    sources = {d['sequence']: d for d in draws}
    passes = {}
    for row in records:
        identity = pass_key(row)
        for packet in range(row['offset'], row['offset'] + row['count']):
            if packet in passes:
                raise ValueError('Overlapping captured packet ranges')
            passes[packet] = (identity, row)
    groups = defaultdict(list)
    seen = set()
    for item in mapped_pairs:
        if item['ambiguous'] or len(item['draws']) != 1:
            continue
        sequences = tuple(item['draws'][0])
        if sequences in seen:
            continue
        a, b = (sources.get(s) for s in sequences)
        if not a or not b or a['flags'] != 7 or b['flags'] != 7 or not a['count'] or a['count'] != b['count']:
            continue
        if a['family'] != b['family'] or a['family'] not in eligible or a['frame'] != b['frame'] or a['side'] != 1 or b['side'] != 0:
            continue
        left, right = (passes[p] for p in item['packets'])
        if left[0] != right[0] or left[1]['side'] != 1 or right[1]['side'] != 0 or left[1]['frame'] != a['frame']:
            raise ValueError('Paired geometry does not belong to one two-eye pass')
        seen.add(sequences)
        groups[left[0]].append(dict(draws=list(sequences), packets=item['packets']))
    if not groups:
        return None
    key, pairs = max(groups.items(), key=lambda item: len(item[1]))
    if len(pairs) > 2048:
        raise ValueError('Constant bank pair limit exceeded')
    return dict(frame=key[0], node=key[1], argumentPlane=key[2], modes=list(key[3]),
                groupBase64=base64.b64encode(key[4]).decode(), pairs=pairs,
                eligiblePairsAcrossPasses=sum(map(len, groups.values())), runtimeEligible=False)
