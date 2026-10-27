"""Join owned CBVs to unambiguous matching geometry; never authorize draw removal."""
import argparse
from collections import Counter
import json
from pathlib import Path
from read_constants import decode


def compare(draws, bindings, payloads, blob, mapped):
    by_sequence = {d['sequence']: d for d in draws}
    totals, slots = Counter(), {}
    pairs, seen = [], set()
    for item in mapped:
        if item['ambiguous'] or len(item['draws']) != 1:
            totals['ambiguousGeometry'] += 1
            continue
        identity = tuple(item['draws'][0])
        if identity in seen:
            continue
        seen.add(identity)
        a, b = (by_sequence.get(s) for s in identity)
        if not a or not b:
            totals['noLinkedFamily'] += 1
            continue
        if a['family'] != b['family'] or a['frame'] != b['frame'] or a['side'] != 1 or b['side'] != 0:
            totals['familyFrameOrEyeMismatch'] += 1
            continue
        if a['flags'] != 7 or b['flags'] != 7:
            totals['unstableOrMissingConstants'] += 1
            continue
        def table(d):
            rows = bindings[d['first']:d['first'] + d['count']]
            result = {(r['visibility'], r['shaderRegister']): r for r in rows}
            if len(result) != len(rows):
                raise ValueError('Duplicate used CBV identity')
            return result
        left, right = table(a), table(b)
        if left.keys() != right.keys():
            raise ValueError('Same family has different CBV sets')
        equal = different = 0
        for key, x in left.items():
            y = right[key]
            if x['expectedBytes'] != y['expectedBytes']:
                raise ValueError('Same CBV has different declared sizes')
            px, py = payloads[x['payload']], payloads[y['payload']]
            n = x['expectedBytes']
            same = blob[px['offset']:px['offset'] + n] == blob[py['offset']:py['offset'] + n]
            equal += same
            different += not same
            count = slots.setdefault(f'{key[0]}:b{key[1]}', Counter())
            count['equal' if same else 'different'] += 1
        totals['verifiedConstantPairs'] += 1
        pairs.append(dict(draws=list(identity), packets=item['packets'], family=a['family'], equal=equal, different=different))
    return dict(totals=dict(totals), slots={k: dict(v) for k, v in slots.items()}, pairs=pairs,
                runtimeEligible=False, limitation='Owned CBV bytes at record and submit only. No proof for SRVs, UAV side effects, remaining shared state or GPU execution.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('constants', type=Path)
    p.add_argument('geometry', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    source, geometry = [json.loads(path.read_text()) for path in (a.constants, a.geometry)]
    if source['pid'] != geometry['pid'] or source['failure']:
        raise ValueError('Capture PID/failure differs')
    blocks = {name: (a.constants.parent / file).read_bytes() for name, file in source['files'].items()}
    draws, bindings, payloads = decode(blocks)
    result = compare(draws, bindings, payloads, blocks['Blob'], geometry['mappedPairs'])
    result['pid'] = source['pid']
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'pairs'}, indent=2))
