"""Correlate candidate packet pairs with actual native D3D geometry, never approve bindings."""
import argparse
import hashlib
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct


def geometry_signature(data):
    if len(data) != 1096:
        raise ValueError('Invalid native draw ABI')
    flags, = struct.unpack_from('<I', data, 84)
    input_mask, = struct.unpack_from('<I', data, 1092)
    vertex_mask, = struct.unpack_from('<I', data, 72)
    if flags & 191 != 191 or flags & 64 or input_mask & vertex_mask != input_mask:
        return None
    used_vertices = tuple((slot, data[112 + slot * 16:128 + slot * 16]) for slot in range(16)
                          if slot != 7 and input_mask & (1 << slot))
    return data[24:40], data[52:56], data[60:68], data[96:112], used_vertices


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('plan', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    capture, plan = json.loads(args.capture.read_text()), json.loads(args.plan.read_text())
    if capture['full'] or capture.get('drawsFull') or capture['inputBytes'] != 256 or capture['drawRecordBytes'] != 1096:
        raise ValueError('Invalid capture ABI/completeness')
    inputs = (args.capture.parent / capture['inputDataFile']).read_bytes()
    payload = (args.capture.parent / capture['inputBlobFile']).read_bytes()
    raw = (args.capture.parent / capture['drawDataFile']).read_bytes()
    if len(raw) % 1096 or len(inputs) % 256:
        raise ValueError('Truncated capture')
    draws = defaultdict(list)
    for at in range(0, len(raw), 1096):
        data = raw[at:at + 1096]
        qpc, = struct.unpack_from('<Q', data, 8)
        side, = struct.unpack_from('<I', data, 48)
        count, = struct.unpack_from('<I', data, 56)
        start, = struct.unpack_from('<I', data, 68)
        va, size, stride = struct.unpack_from('<QII', data, 112 + 7 * 16)
        resource, = struct.unpack_from('<Q', data, 992)
        valid, = struct.unpack_from('<I', data, 1016)
        thread, = struct.unpack_from('<I', data, 1088)
        if valid > 64:
            raise ValueError('Invalid instance data length')
        # Exclude instance count/offset and its vertex buffer; a native batch
        # can include multiple packet instances around this exact input range.
        geometry = geometry_signature(data)
        draws[side, thread].append(dict(qpc=qpc, begin=va + start * stride, end=va + (start + count) * stride,
                                       stride=stride, resource=resource, instance=data[1024:1024 + valid],
                                       geometry=geometry, complete=geometry is not None,
                                       tables=data[368:624], sequence=at // 1096 + 1))
    matches = {}
    for row in capture['records']:
        actual = [d for d in draws[row['side'], row['thread']] if row['beginQpc'] <= d['qpc'] <= row['endQpc']]
        for index in range(row['offset'], row['offset'] + row['count']):
            data = inputs[index * 256:(index + 1) * 256]
            valid, count, _, origin = struct.unpack_from('<4I', data)
            offset, size, stride = struct.unpack_from('<3I', data, 216)
            gpu, resource = struct.unpack_from('<2Q', data, 240)
            if origin == 2 and gpu:
                hits = [d for d in actual if d['stride'] == stride and d['resource'] == resource
                        and d['begin'] <= gpu and gpu + count * stride <= d['end']]
            elif origin == 1 and count == 1 and valid & 16:
                if offset + size > len(payload):
                    raise ValueError('Invalid instance payload range')
                expected = payload[offset:offset + size]
                hits = [d for d in actual if d['instance'] == expected]
            else:
                hits = []
            matches[index] = hits
    counts, mismatches, mapped_pairs = Counter(), [], []
    for left, right in plan['commonPacketPairs']:
        a, b = matches[left], matches[right]
        if not a or not b:
            counts['unmapped'] += 1
            continue
        a, b = [d for d in a if d['complete']], [d for d in b if d['complete']]
        if not a or not b:
            counts['incompleteGraphicsState'] += 1
            continue
        common = {d['geometry'] for d in a} & {d['geometry'] for d in b}
        if not common:
            counts['geometryMismatch'] += 1
            mismatches.append(dict(packets=[left, right], leftDraws=[d['sequence'] for d in a], rightDraws=[d['sequence'] for d in b]))
        else:
            counts['geometryCompatible'] += 1
            mapped_pairs.append(dict(packets=[left,right],ambiguous=len(a)>1 or len(b)>1,
                                     draws=[[x['sequence'],y['sequence']] for x in a for y in b if x['geometry']==y['geometry']]))
            if len(a) > 1 or len(b) > 1:
                counts['ambiguousMapping'] += 1
            if any(x['tables'] == y['tables'] for x in a for y in b if x['geometry'] == y['geometry']):
                counts['sameRawDescriptorTables'] += 1
    result = dict(pid=capture['pid'], dll=capture['dll'], captureSha256=hashlib.sha256(args.capture.read_bytes()).hexdigest(),
                  candidatePairs=len(plan['commonPacketPairs']), totals=dict(counts),
                  mismatches=mismatches, mappedPairs=mapped_pairs, runtimeEligible=False,
                  limitation='Geometry-only correlation. Descriptor table handles are not resource/content equivalence; '
                             'CPU batch mapping can be ambiguous. No native draw is skipped.')
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k:(v[:10] if k=='mismatches' else v) for k,v in result.items() if k!='mappedPairs'}, indent=2))


if __name__ == '__main__':
    main()
