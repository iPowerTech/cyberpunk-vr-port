"""Offline prototype: select two CameraShaderConsts blocks with SV_ViewID.

Input must be DXC's lossless -dumpbin output, not RenderDoc's formatted IR.
Unsupported layouts fail closed. The game does not load these outputs.
After this rewrite run DXC's viewid-state/metadata passes, assembler and validator.
"""
import argparse
import json
import re
from pathlib import Path
from camera_usage import camera_usage


def patch(source, main_first=False):
    usage=camera_usage(source)
    if '@dx.op.viewID.' in source:
        raise ValueError('Shader is already view instanced')
    # Named SSA values let us insert instructions without corrupting numeric IDs.
    source = re.sub(r'%(\d+)\b', lambda m: '%cvr' + m[1], source)
    source = re.sub(r'(?m)^(\d+):', lambda m: 'cvr' + m[1] + ':', source)
    source = re.sub(r'(?m)^; <label>:(\d+)\s*(;[^\n]*)?$',
                    lambda m: 'cvr' + m[1] + ': ' + (m[2] or ''), source)
    metadata = re.findall(
        r'(?m)^(!\d+) = !\{i32 (\d+), (%[\w.]*CameraShaderConsts)\* undef, !"[^"]*", '
        r'i32 (\d+), i32 (\d+), i32 (\d+), i32 (\d+), null\}$', source)
    if len(metadata) != 1:
        raise ValueError('Expected one explicitly typed CameraShaderConsts binding')
    md, cb_id, cb_type, space, register, count, size = metadata[0]
    size = int(size)
    if int(count) != 1 or not 16 <= size <= 32768:
        raise ValueError('Unsupported camera CB array or size')
    stride = (size + 255) & ~255
    handle_pattern = (r'(%[\w.]+) = call %dx.types.Handle @dx.op.createHandle'
        r'\(i32 57, i8 2, i32 ' + cb_id + r', i32 ' + register + r', i1 false\)')
    handles = re.findall(handle_pattern, source)
    if len(handles) != 1:
        raise ValueError('Expected one static legacy camera handle')
    handle = handles[0]
    load_pattern = (r'(?m)^(  %[\w.]+ = call %dx.types.CBufRet.\w+ '
        r'@dx.op.cbufferLoadLegacy.\w+\(i32 59, %dx.types.Handle ' + re.escape(handle)
        + r', i32 )(%[\w.]+|\d+)(\).*)$')
    loads = list(re.finditer(load_pattern, source))
    if not loads or len(re.findall(re.escape(handle) + r'\b', source)) != len(loads) + 1:
        raise ValueError('Camera handle has unsupported uses')
    serial = 0

    def replace_load(match):
        nonlocal serial
        serial += 1
        index = '%cvr_eye_index_' + str(serial)
        return ('  ' + index + ' = add i32 ' + match[2] + ', %cvr_camera_base\n'
                + match[1] + index + match[3])

    source = re.sub(load_pattern, replace_load, source)
    entries = re.findall(r'(?m)^define void @([\w.]+)\(\) \{$', source)
    if len(entries) != 1:
        raise ValueError('Expected one graphics entry point')
    entry = 'define void @' + entries[0] + '() {'
    selection='\n  %cvr_camera_eye = xor i32 %cvr_view, 1' if main_first else ''
    source = source.replace(entry, entry + '\ncvr0:\n  %cvr_view = call i32 @dx.op.viewID.i32(i32 138)'+selection
                            + '\n  %cvr_camera_base = mul i32 '+('%cvr_camera_eye' if main_first else '%cvr_view')+', ' + str(stride // 16), 1)
    source += '\ndeclare i32 @dx.op.viewID.i32(i32) nounwind readnone\n'
    old_metadata = re.search(r'(?m)^' + re.escape(md) + r' = .*$', source)[0]
    source = source.replace(old_metadata, old_metadata.replace('i32 ' + str(size) + ', null}',
                                                               'i32 ' + str(2 * stride) + ', null}'))
    for key, expected, replacement in [
        ('dx.shaderModel', r'!"(vs|ps|gs|hs|ds)", i32 6, i32 0', r'!"\1", i32 6, i32 1'),
        ('dx.version', r'i32 1, i32 0', 'i32 1, i32 1')]:
        ref = re.search(r'(?m)^!' + re.escape(key) + r' = !\{(!\d+)\}$', source)
        if not ref:
            raise ValueError('Missing ' + key)
        node = re.search(r'(?m)^' + re.escape(ref[1]) + r' = !\{(.*)\}$', source)
        changed, n = re.subn(expected, replacement, node[1])
        if n != 1:
            raise ValueError('Only SM6.0/DXIL1.0 graphics shaders supported by this prototype')
        source = source.replace(node[0], ref[1] + ' = !{' + changed + '}')
    source = re.sub(r'(?m)^!dx.viewIdState = .*\n', '', source)
    # ShaderFlags::ViewID (DXIL flag bit 21) must agree with the new intrinsic.
    # Leave every other entry-point property intact; DXC regenerates dependencies.
    entry_properties = re.search(r'(?m)^!\d+ = !\{void \(\)\* @' + re.escape(entries[0])
                                 + r', !"[^"]*", !\d+, !\d+, (!\d+|null)\}$', source)
    if not entry_properties:
        raise ValueError('Unsupported entry-point properties')
    if entry_properties[1] == 'null':
        new_id = '!' + str(max(map(int, re.findall(r'(?m)^!(\d+) =', source))) + 1)
        source = source.replace(entry_properties[0], entry_properties[0][:-5] + new_id + '}')
        source += '\n' + new_id + ' = !{i32 0, i64 ' + str(1 << 21) + '}\n'
    else:
        flags = re.search(r'(?m)^' + re.escape(entry_properties[1]) + r' = !\{(.*)\}$', source)
        values, n = re.subn(r'i32 0, i64 (\d+)', lambda m: 'i32 0, i64 ' + str(int(m[1]) | (1 << 21)), flags[1])
        if n != 1:
            raise ValueError('Expected one existing shader flags property')
        source = source.replace(flags[0], entry_properties[1] + ' = !{' + values + '}')
    return source, {'camera_register': int(register), 'space': int(space), 'original_size': size,
                    'main_first':bool(main_first),
                    'eye_stride': stride, 'binding_size': stride * 2, 'patched_loads': serial,
                    'camera_words':usage['words'],'dynamic_camera_indices':usage['dynamicIndices']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--main-first',action='store_true')
    args = parser.parse_args()
    source, report = patch(args.input.read_text(),main_first=args.main_first)
    args.output.write_text(source)
    print(json.dumps(report))
