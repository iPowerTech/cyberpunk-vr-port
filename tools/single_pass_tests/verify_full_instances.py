"""Require selected packets to cover entire native draw instance batches."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct


def full_batch(draw, packet, blob):
    if len(draw) != 1096 or len(packet) != 256:
        raise ValueError('Invalid draw/input ABI')
    valid, count, _, origin = struct.unpack_from('<4I', packet)
    offset, size, stride = struct.unpack_from('<3I', packet, 216)
    actual_count, = struct.unpack_from('<I', draw, 56)
    first, = struct.unpack_from('<I', draw, 68)
    slots, = struct.unpack_from('<I', draw, 1092)
    va, available, actual_stride = struct.unpack_from('<QII', draw, 112+7*16)
    if not slots & 128 or not valid & 16 or not count or count != actual_count or stride not in (48,64) or stride != actual_stride:
        raise ValueError('Packet does not cover the native instance batch')
    if size != count*stride or offset+size > len(blob) or (first+count)*stride > available:
        raise ValueError('Incomplete instance byte range')
    data = blob[offset:offset+size]
    if origin == 2:
        gpu, resource = struct.unpack_from('<2Q', packet, 240)
        actual_resource, = struct.unpack_from('<Q', draw, 992)
        if not resource or resource != actual_resource or gpu != va+first*stride:
            raise ValueError('Preuploaded instance range differs from native binding')
    elif origin == 1:
        copied, = struct.unpack_from('<I', draw, 1016)
        if count != 1 or copied != size or draw[1024:1024+copied] != data:
            raise ValueError('CPU instance was not observed at the native draw')
    else:
        raise ValueError('Unsupported instance source')
    return data, dict(instances=count, stride=stride, origin=origin)


def verify(pairs, native, inputs, blob):
    if len(native) % 1096 or len(inputs) % 256:
        raise ValueError('Truncated capture')
    result, counts, seen = [], Counter(), set()
    for pair in pairs:
        key = tuple(pair['draws'])
        if len(key) != 2 or key in seen:
            raise ValueError('Invalid or duplicate native draw pair')
        seen.add(key)
        batches = []
        identities = []
        for sequence, packet in zip(key, pair['packets']):
            if sequence <= 0 or sequence*1096 > len(native) or packet < 0 or (packet+1)*256 > len(inputs):
                raise ValueError('Instance pair index is out of bounds')
            draw = native[(sequence-1)*1096:sequence*1096]
            identities.append((struct.unpack_from('<I', draw, 40)[0], struct.unpack_from('<I', draw, 48)[0]))
            batches.append(full_batch(draw, inputs[packet*256:(packet+1)*256], blob))
        if len(identities)!=2 or identities[0][0]!=identities[1][0] or not identities[0][0] or (identities[0][1],identities[1][1])!=(1,0):
            raise ValueError('Instance pair frame/eye identity differs')
        left, right = batches
        if left[0] != right[0] or left[1]['instances'] != right[1]['instances'] or left[1]['stride'] != right[1]['stride']:
            raise ValueError('Complete per-eye instance batches differ')
        counts[str(left[1]['instances'])] += 1
        result.append(dict(draws=list(key), instances=left[1]['instances'], stride=left[1]['stride'],
                           bytes=len(left[0]), sha256=hashlib.sha256(left[0]).hexdigest(),
                           origins=[left[1]['origin'], right[1]['origin']]))
    return dict(pairs=result, count=len(result), instanceCounts=dict(counts), runtimeEligible=False,
                limitation='Complete owned CPU input snapshots match native draw ranges; GPU contents at execution and remaining dynamic state are not certified.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--constants', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    capture, constants = [json.loads(path.read_text()) for path in (a.capture, a.constants)]
    if capture['pid'] != constants['pid'] or capture['dll'] != constants['dll'] or not constants['passed']:
        raise ValueError('Constant pass does not match capture')
    result = verify(constants['selectedPass']['pairs'], (a.capture.parent/capture['drawDataFile']).read_bytes(),
                    (a.capture.parent/capture['inputDataFile']).read_bytes(), (a.capture.parent/capture['inputBlobFile']).read_bytes())
    result.update(pid=capture['pid'], dll=capture['dll'])
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k:v for k,v in result.items() if k!='pairs'}, indent=2))
