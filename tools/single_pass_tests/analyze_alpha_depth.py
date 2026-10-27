"""Audit the two exact alpha-depth families using saved CPU capture data only.

Descriptors and CBV addresses never certify resource contents. Missing dynamic
material constants leave the bindless texture index unresolved in this report.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from analyze_descriptor_audit import load
from analyze_skinning_reads import bone_range, deformation, resource


PIXEL = 'dc69ea3ee2d8d906ebcbba4bd8f8d750add2b80946b5de9c9d94360cb18339e5'
VERTICES = {
    39: '02f81c75e38964877d832f7378a39251132ab93580dfe89017e65d01fc22bc4c',
    43: '7fa0e076ff234929afda3679349d06353d4936245785b547b339432e3b1fd9ae',
}
READS = {
    (1, 5): (64, 68, 72, 80, 84, 88),
    (1, 7): (16, 24, 32),
    (5, 7): (0, 8, 12),
    (5, 0): (452,),
    (5, 4): (0, 272, 288),
}


def alpha_inputs(b7, b0, b4=None):
    if len(b7) < 16 or len(b0) < 456 or b4 is not None and len(b4) < 292:
        raise ValueError('Truncated alpha constant reads')
    gate, _, scale, bias = struct.unpack_from('<4f', b7)
    frame, = struct.unpack_from('<I', b0, 452)
    result = dict(alphaEnabled=gate != 0, ditherScale=scale, ditherBias=bias,
                  frameSelector=frame, ditherSlice=frame & 63,
                  alphaTextureIndex=None, alphaCutoff=None, uvScale=None)
    if b4 is not None:
        index, = struct.unpack_from('<I', b4)
        if result['alphaEnabled'] and index >= 32768:
            raise ValueError('Alpha texture index outside native bindless range')
        result.update(alphaTextureIndex=index,
                      alphaCutoff=struct.unpack_from('<f', b4, 272)[0],
                      uvScale=struct.unpack_from('<f', b4, 288)[0])
    return result


def descriptor(bindings, values, kind, visibility, register, space=0):
    found = [b for b in bindings.values() if b['kind'] == kind and b['space'] == space
             and b['key'][6] in (0, visibility)
             and b['register'] <= register < b['register'] + b['count']]
    if len(found) != 1:
        raise ValueError('Missing or ambiguous shader descriptor range')
    binding = found[0]
    offset = register - binding['register']
    identity = binding['ids'][offset] if offset < len(binding['ids']) else 0
    return dict(parameter=binding['key'][0], status=binding['status'],
                heap=binding['heap'], gpu=binding['gpu'], version=binding['version'],
                register=register, space=space, rangeCount=binding['count'],
                capturedSlots=len(binding['ids']), valueId=identity,
                value=list(values[identity]) if identity else None)


def dither_footprint(value, selector):
    info = resource(value)
    if (tuple(value[4:7]) != (11, 5, 5768) or info['dimension'] != 3
            or info['format'] != 11 or info['samples'] != 1 or info['quality']
            or value[7] >= info['mips'] or not value[8] or value[11] or value[12]
            or not value[10] or value[9] + value[10] > info['array']):
        raise ValueError('Unsupported native R16G16B16A16_UNORM dither array')
    width, height = max(1, info['width'] >> value[7]), max(1, info['height'] >> value[7])
    if width < 64 or height < 64:
        raise ValueError('Dither tile is outside the captured resource shape')
    slices = list(range(64)) if selector is None else [selector & 63]
    if max(slices) >= value[10]:
        raise ValueError('Dither slice is outside the SRV')
    return dict(resource=info, mip=value[7], viewSlices=slices,
                resourceSlices=[value[9] + s for s in slices],
                tileWidth=64, tileHeight=64, texelBytes=8,
                logicalBytes=64 * 64 * 8 * len(slices), contentsVerified=False)


def used_words(payload, offsets):
    if any(o < 0 or o + 4 > len(payload) for o in offsets):
        raise ValueError('Shader constant read exceeds owned payload')
    return {str(o): struct.unpack_from('<I', payload, o)[0] for o in offsets}


def constants(directory, capture, registry):
    path = directory / 'constants.json'
    if not path.exists():
        return {}, {}, []
    from read_constants import decode
    meta = json.loads(path.read_text())
    if (meta['pid'], meta['dll']) != (capture['pid'], capture['dll']) or meta['failure']:
        raise ValueError('Constants belong to a different or failed capture')
    blocks = {name: (directory / file).read_bytes() for name, file in meta['files'].items()}
    draws, bindings, payloads = decode(blocks)
    by_id = {r['id']: r for r in registry['records'] if r.get('asset') in VERTICES}
    result, owned = {}, {}
    for draw in draws:
        if draw['family'] not in by_id:
            continue
        rows = {}
        for binding in bindings[draw['first']:draw['first'] + draw['count']]:
            key = (binding['visibility'], binding['shaderRegister'])
            label = f'{key[0]}:b{key[1]}'
            row = dict(status=binding['status'], address=binding['address'],
                       bytes=binding['expectedBytes'], payload=binding['payload'],
                       receiptFlags=binding['reserved'])
            if binding['status'] == 1:
                p = payloads[binding['payload']]
                data = blocks['Blob'][p['offset']:p['offset'] + binding['expectedBytes']]
                owned[(draw['sequence'], key)] = data
                row['sha256'] = hashlib.sha256(data).hexdigest()
                if key in READS:
                    row['usedWords'] = used_words(data, READS[key])
            rows[label] = row
        result[draw['sequence']] = dict(asset=by_id[draw['family']]['asset'],
                                       side=draw['side'], frame=draw['frame'],
                                       flags=draw['flags'], bindings=rows)
    geometry_path = directory / 'geometry-pairs-bound.json'
    pairs, seen = [], set()
    if geometry_path.exists():
        geometry = json.loads(geometry_path.read_text())
        if ((geometry['pid'], geometry['dll']) != (capture['pid'], capture['dll'])
                or geometry['captureSha256'] != hashlib.sha256((directory / 'resolved_packets.json').read_bytes()).hexdigest()):
            raise ValueError('Geometry mapping does not belong to this capture')
        for item in geometry['mappedPairs']:
            if item['ambiguous'] or len(item['draws']) != 1:
                continue
            a, b = item['draws'][0]
            if a not in result or b not in result or (a, b) in seen:
                continue
            seen.add((a, b))
            x, y = result[a], result[b]
            if (x['asset'], x['frame']) != (y['asset'], y['frame']) or (x['side'], y['side']) != (1, 0):
                raise ValueError('Mapped alpha draws have different family/frame/eyes')
            comparison = {}
            for key in x['bindings']:
                left, right = x['bindings'][key], y['bindings'][key]
                comparison[key] = dict(
                    descriptorAddressEqual=left['address'] == right['address'],
                    payloadEqual=left.get('sha256') == right.get('sha256') if left['status'] == right['status'] == 1 else None,
                    usedWordsEqual=left.get('usedWords') == right.get('usedWords') if 'usedWords' in left and 'usedWords' in right else None,
                    differentUsedOffsets=[int(o) for o in left.get('usedWords', {})
                                          if left['usedWords'][o] != right.get('usedWords', {}).get(o)]
                    if 'usedWords' in left and 'usedWords' in right else None)
            pairs.append(dict(draws=[a, b], asset=x['asset'], constants=comparison))
    return result, owned, pairs


def analyze(directory, manifest_path):
    capture = json.loads((directory / 'resolved_packets.json').read_text())
    registry = json.loads((directory / 'families.json').read_text())
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    if hashlib.sha256(manifest_bytes).hexdigest() != registry['manifestSha256']:
        raise ValueError('Manifest differs from the captured family registry')
    for asset, vertex in VERTICES.items():
        row = manifest['families'][asset - 1]
        if (row['nativeVs'], row['nativePs']) != (vertex, PIXEL):
            raise ValueError('Alpha family shader identity mismatch')
    meta, descriptors, values = load(directory / 'resolved_packets-descriptors.json')
    if any((x['pid'], x['dll']) != (capture['pid'], capture['dll']) for x in (registry, meta)):
        raise ValueError('Different process/DLL captures')
    const_rows, owned, pairs = constants(directory, capture, registry)
    families = {int(r['original'], 16): r for r in registry['records']
                if r.get('asset') in VERTICES and r['result'] == 0}
    raw = (directory / capture['drawDataFile']).read_bytes()
    if len(raw) % 1096:
        raise ValueError('Truncated native draw ABI')
    result = []
    for at in range(0, len(raw), 1096):
        data = raw[at:at + 1096]
        sequence, _, _, pipeline, _ = struct.unpack_from('<5Q', data)
        frame, node, side, indices, instances = struct.unpack_from('<5I', data, 40)
        colors, = struct.unpack_from('<I', data, 88)
        if pipeline not in families or node != 0x23a938 or colors:
            continue
        family = families[pipeline]
        if side not in (0, 1) or not descriptors[sequence]['complete']:
            raise ValueError('Incomplete eye/descriptor evidence')
        if sequence in const_rows and (const_rows[sequence]['asset'], const_rows[sequence]['side'], const_rows[sequence]['frame']) != (family['asset'], side, frame):
            raise ValueError('Constants do not match native draw identity')
        bindings = descriptors[sequence]['bindings']
        dither = descriptor(bindings, values, 0, 5, 67)
        sampler = descriptor(bindings, values, 3, 5, 0)
        bindless = descriptor(bindings, values, 0, 5, 0, 1)
        row = dict(sequence=sequence, asset=family['asset'], side=side,
                   frame=frame, indices=indices, instances=instances,
                   ditherDescriptor=dither, samplerDescriptor=sampler,
                   bindlessRange=bindless, constants=const_rows.get(sequence),
                   alphaInputs=None)
        b7, b0, b4 = [owned.get((sequence, (5, r))) for r in (7, 0, 4)]
        if b7 is not None and b0 is not None:
            row['alphaInputs'] = alpha_inputs(b7, b0, b4)
            index = row['alphaInputs']['alphaTextureIndex']
            if index is not None and row['alphaInputs']['alphaEnabled']:
                row['alphaDescriptor'] = descriptor(bindings, values, 0, 5, index, 1)
        if dither['value'] and dither['value'][1] & 4:
            row['ditherFootprint'] = dither_footprint(dither['value'],
                row['alphaInputs']['frameSelector'] if row['alphaInputs'] else None)
        if family['asset'] == 39:
            row['vertexSrvs'] = {str(reg): descriptor(bindings, values, 0, 1, reg) for reg in (10, 77)}
            copied, = struct.unpack_from('<I', data, 1016)
            _, available, stride = struct.unpack_from('<QII', data, 112 + 7 * 16)
            first, = struct.unpack_from('<I', data, 68)
            slots, = struct.unpack_from('<I', data, 1092)
            instance = data[1024:1088]
            if instances == 1 and copied == stride == 64 and slots & 128 and (first + 1) * stride <= available:
                row['instanceSha256'] = hashlib.sha256(instance).hexdigest()
                for reg, key, function in ((10, 'boneRange', lambda v: bone_range(v, instance)),
                                           (77, 'deformation', deformation)):
                    value = row['vertexSrvs'][str(reg)]['value']
                    if value and value[1] & 4:
                        row[key] = function(value)
        result.append(row)
    return dict(pid=capture['pid'], dll=capture['dll'],
                manifestSha256=registry['manifestSha256'], draws=result,
                counts=dict(Counter(f"{r['asset']}:side{r['side']}" for r in result)),
                constantPairs=pairs, runtimeEligible=False,
                limitation='Offline observations only. Uncaptured constants and SRV contents remain unknown; equal descriptors or addresses do not prove contents or GPU lifetime.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    report = analyze(args.capture, args.manifest)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('draws', 'constantPairs')}, indent=2))


if __name__ == '__main__':
    main()
