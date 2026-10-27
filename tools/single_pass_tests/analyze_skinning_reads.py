"""Bound exact skinning shader reads from live input contracts and SRV metadata.

This does not prove equal resource contents or authorize skipping native draws.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from analyze_descriptor_audit import load

SHADER = '906dcb2a6f034360c3b1d00067dbf12b9f6ee79a9789e73fae258688dee00d0f'


def resource(v):
    if len(v) != 20 or v[0] != 2 or not v[2] or v[1] & 7 != 5:
        raise ValueError('Explicit SRV resource metadata is missing')
    return dict(cookie=v[2], dimension=v[13] & 0xffffffff, format=v[13] >> 32,
                width=v[14], height=v[15] & 0xffffffff, array=(v[15] >> 32) & 65535,
                mips=v[15] >> 48, flags=v[16] & 0xffffffff, samples=v[17] & 0xffffffff,
                quality=v[17] >> 32, heapType=v[18] & 65535 if v[1] & 8 else None,
                gpuAddress=v[19])


def bone_range(v, instance):
    info = resource(v)
    if (len(instance) != 64 or tuple(v[4:7]) != (39, 1, 5768) or v[9] or v[10] != 1 or
            not v[8] or v[7] % 4 or info['dimension'] != 1):
        raise ValueError('Not the contract-1 raw bone buffer')
    begin, size = v[7] * 4, v[8] * 4
    base, stride = struct.unpack_from('<2I', instance, 48)
    length = stride * 255 + 48
    if (begin + size > info['width'] or base % 4 or stride % 4 or
            base + length > min(1 << 32, size)):
        raise ValueError('Bone address overflow or incomplete palette range')
    return dict(offset=begin + base, bytes=length, base=base, stride=stride)


def deformation(v):
    info = resource(v)
    if (tuple(v[4:7]) != (61, 4, 5768) or v[9] or v[10] or info['dimension'] != 3 or info['format'] != 61 or
            info['array'] != 1 or info['samples'] != 1 or info['quality'] or
            not info['width'] or not info['height'] or not v[8] or v[7] >= info['mips']):
        raise ValueError('Unsupported R8 deformation texture/view')
    width, height = max(1, info['width'] >> v[7]), max(1, info['height'] >> v[7])
    return dict(mip=v[7], width=width, height=height, texelBytes=width * height)


def srv(bindings, values, register):
    found = []
    for binding in bindings.values():
        delta = register - binding['register']
        if binding['kind'] != 0 or binding['space'] or binding['key'][6] not in (0, 1) or not 0 <= delta < binding['count']:
            continue
        if delta >= len(binding['ids']) or not binding['ids'][delta]:
            raise ValueError('Shader SRV slot was not captured')
        found.append(list(values[binding['ids'][delta]]))
    if len(found) != 1:
        raise ValueError('Missing or ambiguous vertex SRV')
    return found[0]


def merge(ranges):
    result = []
    for start, end in sorted((r['offset'], r['offset'] + r['bytes']) for r in ranges):
        if result and start <= result[-1][1]:
            result[-1][1] = max(result[-1][1], end)
        else:
            result.append([start, end])
    return [dict(offset=start, bytes=end - start) for start, end in result]


def analyze(raw, descriptors, values, registry, manifest):
    if len(raw) % 1096:
        raise ValueError('Invalid native draw ABI')
    families = {int(r['original'], 16): r for r in registry['records'] if r['result'] == 0}
    result = []
    for at in range(0, len(raw), 1096):
        data = raw[at:at + 1096]
        sequence, _, _, pipeline, _ = struct.unpack_from('<5Q', data)
        _, node, side, indices, instances = struct.unpack_from('<5I', data, 40)
        colors, = struct.unpack_from('<I', data, 88)
        family = families.get(pipeline)
        if not family or not family.get('skinningReadContract') or node != 0x23a938 or colors:
            continue
        if not 1 <= family['asset'] <= len(manifest['families']):
            raise ValueError('Invalid family asset index')
        asset = manifest['families'][family['asset'] - 1]
        if family['skinningReadContract'] != 1 or asset['nativeVs'] != SHADER:
            raise ValueError('Shader/input contract mismatch')
        copied, = struct.unpack_from('<I', data, 1016)
        _, available, stride = struct.unpack_from('<QII', data, 112 + 7 * 16)
        first, = struct.unpack_from('<I', data, 68)
        slots, = struct.unpack_from('<I', data, 1092)
        if (side not in (0, 1) or instances != 1 or copied != 64 or stride != 64 or
                not slots & 128 or (first + instances) * stride > available or not descriptors[sequence]['complete']):
            raise ValueError('Incomplete native skinning draw/instance evidence')
        bindings = descriptors[sequence]['bindings']
        bones, texture = [srv(bindings, values, register) for register in (10, 77)]
        instance = data[1024:1088]
        result.append(dict(sequence=sequence, side=side, indices=indices,
                           instanceSha256=hashlib.sha256(instance).hexdigest(),
                           bones=resource(bones), boneRange=bone_range(bones, instance),
                           texture=resource(texture), deformation=deformation(texture)))
    if {r['side'] for r in result} != {0, 1}:
        raise ValueError('No contract-1 depth draws from both eyes')
    totals = []
    for side in (0, 1):
        own = [r for r in result if r['side'] == side]
        buffers, textures = {}, {}
        for row in own:
            buffers.setdefault(row['bones']['cookie'], []).append(row['boneRange'])
            key = (row['texture']['cookie'], row['deformation']['mip'])
            if key in textures and textures[key] != row['deformation']:
                raise ValueError('Conflicting immutable texture metadata')
            textures[key] = row['deformation']
        spans = [dict(cookie=cookie, ranges=merge(ranges)) for cookie, ranges in buffers.items()]
        totals.append(dict(side=side, draws=len(own), boneRanges=spans,
                           boneBytes=sum(r['bytes'] for b in spans for r in b['ranges']),
                           deformationBytes=sum(t['texelBytes'] for t in textures.values())))
    return dict(draws=result, totals=totals, runtimeEligible=False,
                limitation='Verified VS/input contract and bounded read footprints only. Bone/texture contents, mutable constants and per-eye equivalence are not proved.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    capture = json.loads((a.capture / 'resolved_packets.json').read_text())
    registry = json.loads((a.capture / 'families.json').read_text())
    meta, draws, values = load(a.capture / 'resolved_packets-descriptors.json')
    manifest = a.manifest.read_bytes()
    if (capture['pid'], capture['dll']) != (registry['pid'], registry['dll']) or meta['pid'] != capture['pid'] or meta['dll'] != capture['dll']:
        raise ValueError('Different process or DLL captures')
    if hashlib.sha256(manifest).hexdigest() != registry['manifestSha256']:
        raise ValueError('Different family manifest')
    result = analyze((a.capture / capture['drawDataFile']).read_bytes(), draws, values, registry, json.loads(manifest))
    result.update(pid=capture['pid'], dll=capture['dll'])
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'draws'}, indent=2))


if __name__ == '__main__':
    main()
