"""Prepare matte window glass and the same tinted glass cut to round buttons.

Only edits vrport/ui resources. The icon is a 64-sided mesh with the window's
material and no colored ink fill. Serialize these JSON sources with WolvenKit.
"""
from pathlib import Path
import base64
import copy
import json
import math
import struct

ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / 'mods/archive/source/raw/vrport/ui'


def read(name):
    return json.loads((RAW / name).read_text(encoding='utf-8-sig'))


def write(name, data):
    path = RAW / name
    newline = '\r\n' if path.exists() and b'\r\n' in path.read_bytes() else '\n'
    path.write_bytes((json.dumps(data, ensure_ascii=False, indent=1) + '\n').replace('\n', newline).encode('utf-8'))


def matte(data, opaque=False):
    mat = data['Data']['RootChunk']['preloadLocalMaterialInstances'][0]['Data']
    changes = {
        'FrontFacesReflectionPower': ('Float', 0),
        'IOR': ('Float', 1),
        'RefractionDepth': ('Float', 0),
        'FresnelBias': ('Float', 0),
        'GlassSpecularColor': ('Color', {'$type': 'Color', 'Alpha': 255, 'Red': 0, 'Green': 0, 'Blue': 0}),
        'SurfaceMetalness': ('Float', 0),
        'GlassRoughnessBias': ('Float', 1),
        'MaskRoughnessBias': ('Float', 1),
        'BlurByRoughness': ('Float', 1),
        'BlurRadius': ('Float', 1),
        # MaskOpacity blends in a lit opaque surface. It picks up blue scene
        # lighting; darkening must come from glass transmission instead.
        'MaskOpacity': ('Float', 0),
        'Opacity': ('Float', 1 if opaque else .85),
        'TintFromVertexPaint': ('Float', 0),
        'NormalStrength': ('Float', 0),
        'TintColor': ('Color', {'$type': 'Color', 'Alpha': 255, 'Red': 0 if opaque else 28,
                              'Green': 0 if opaque else 28, 'Blue': 0 if opaque else 28}),
        'Roughness': ('rRef:ITexture', {'DepotPath': {'$type': 'ResourcePath', '$storage': 'string',
                       '$value': 'base\\materials\\placeholder\\white.xbm'}, 'Flags': 'Default'}),
    }
    values = mat['values']
    for name, (kind, value) in changes.items():
        replacement = {'$type': kind, name: value}
        index = next((i for i, old in enumerate(values) if name in old), None)
        if index is None:
            values.append(replacement)
        else:
            values[index] = replacement


def disc(data):
    mesh = data['Data']['RootChunk']
    blob = mesh['renderResourceBlob']['Data']
    head = blob['header']
    chunk = head['renderChunkInfos'][0]
    original = base64.b64decode(blob['renderBuffer']['Bytes'])
    old_offsets = chunk['chunkVertices']['byteOffsets']['Elements']
    normal = original[old_offsets[2]:old_offsets[2]+8]
    color_uv1 = original[old_offsets[3]:old_offsets[3]+8]
    count = 64
    points = [(0., 0.)] + [(.5*math.cos(-i*math.tau/count), .5*math.sin(-i*math.tau/count)) for i in range(count)]
    indices = [v for i in range(count) for v in (0, i+1, (i+1) % count+1)]
    streams = [b''.join(struct.pack('<4h', round(x*65534), round(y*65534), -32767, 32767) for x, y in points),
               b''.join(struct.pack('<2e', .5-x, .5-y) for x, y in points), normal*len(points), color_uv1*len(points)]
    buffer = bytearray()
    offsets = []
    for stream in streams:
        buffer.extend(b'\0' * (-len(buffer) % 16))
        offsets.append(len(buffer))
        buffer.extend(stream)
    head['vertexBufferSize'] = len(buffer)
    buffer.extend(b'\0' * (-len(buffer) % 1024))
    head['indexBufferOffset'] = len(buffer)
    packed_indices = struct.pack('<' + 'H'*len(indices), *indices)
    head['indexBufferSize'] = len(packed_indices)
    buffer.extend(packed_indices)
    chunk['chunkVertices']['byteOffsets']['Elements'][:4] = offsets
    chunk['numVertices'] = len(points)
    chunk['numIndices'] = len(indices)
    blob['renderBuffer']['Bytes'] = base64.b64encode(buffer).decode('ascii')
    mesh['surfaceAreaPerAxis']['Z'] = count*.125*math.sin(math.tau/count)
    for entry in mesh['parameters']:
        collision = entry.get('Data', {})
        if collision.get('$type') != 'meshMeshParamUICollisionData':
            continue
        collision['vertices'] = [{'$type': 'Vector3', 'X': x, 'Y': y, 'Z': 0} for x, y in points]
        collision['uvs'] = [{'$type': 'Vector2', 'X': .5-x, 'Y': .5-y} for x, y in points]
        collision['trianglesIndices'] = [v for i in range(count) for v in (0, (i+1) % count+1, i+1)]
    assert max(indices) < len(points) and len(buffer) == head['indexBufferOffset'] + head['indexBufferSize']


def main():
    glass = read('vr_menu_glass_plane.mesh.json');matte(glass)
    write('vr_menu_glass_plane.mesh.json', glass)
    opaque = read('vr_menu_opaque_plane.mesh.json');matte(opaque, True)
    write('vr_menu_opaque_plane.mesh.json', opaque)
    # The button backing is the window material, cut to a circle. No separate
    # tint/opacity recipe: it must look like the adjacent panel in the same light.
    icon = copy.deepcopy(glass);disc(icon)
    write('vr_menu_icon_plane.mesh.json', icon)
    entity = read('vr_menu_opaque.ent.json')
    def replace(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if child == 'vrport\\ui\\vr_menu_opaque_plane.mesh':
                    value[key] = 'vrport\\ui\\vr_menu_icon_plane.mesh'
                elif child == 'vrp_menu_opaque':
                    value[key] = 'vrp_menu_icon'
                else:
                    replace(child)
        elif isinstance(value, list):
            for child in value:
                replace(child)
    replace(entity)
    write('vr_menu_icon.ent.json', entity)
    print('Matte panes and circular icon backing using the exact window material prepared.')


if __name__ == '__main__':
    main()
