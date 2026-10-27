"""Build a validated, content-addressed stereo DXIL inventory without GPU replay.

Accepts the native-shaders/index.json capture export and optional frame detail.
Outputs are offline candidates, NEVER permission to replace a live pipeline.
Camera conversion does not prove other bindings, visibility or targets compatible.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

from camera_usage import camera_usage
from patch_camera import patch
from upgrade_stage import upgrade

SCHEMA = 1
PASSES = ['-hlsl-dxilload', '-viewid-state', '-hlsl-dxilemit']
GRAPHICS = {'vs', 'ps', 'gs', 'hs', 'ds'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def metadata(source):
    return dict(re.findall(r'(?m)^(![\w.]+) = !\{(.*)\}$', source))


def container_parts(data):
    if len(data) < 32 or data[:4] != b'DXBC':
        raise ValueError('Invalid shader container header')
    size, count = struct.unpack_from('<2I', data, 24)
    if size != len(data) or count > (size - 32) // 4:
        raise ValueError('Invalid shader container size/table')
    parts = {}
    spans = []
    for index in range(count):
        offset, = struct.unpack_from('<I', data, 32 + index * 4)
        if offset < 32 + count * 4 or offset > size - 8:
            raise ValueError('Invalid shader part offset')
        length, = struct.unpack_from('<I', data, offset + 4)
        end = offset + 8 + length
        if end > size or any(offset < b and a < end for a, b in spans):
            raise ValueError('Truncated or overlapping shader parts')
        tag = data[offset:offset + 4].decode('ascii')
        if tag in parts:
            raise ValueError('Duplicate shader part')
        parts[tag] = data[offset + 8:end]
        spans.append((offset, end))
    return parts


def classify(source):
    md = metadata(source)
    model = re.fullmatch(r'!"(\w+)", i32 (\d+), i32 (\d+)',
                        md.get(md.get('!dx.shaderModel'), ''))
    if not model:
        raise ValueError('Missing or unsupported shader model metadata')
    stage, major, minor = model.groups()
    resources = []
    root = md.get(md.get('!dx.resources'), '')
    if not root and '@dx.op.createHandle' in source:
        raise ValueError('Resource metadata missing for a resource-using shader')
    if root:
        lists = root.split(', ')
        if len(lists) != 4:
            raise ValueError('Unsupported resource metadata')
        for kind, ref in zip(('srv', 'uav', 'cbuffer', 'sampler'), lists):
            if ref == 'null':
                continue
            if ref not in md:
                raise ValueError('Missing resource list: ' + ref)
            for child in md[ref].split(', '):
                if child not in md:
                    raise ValueError('Missing resource declaration: ' + child)
                row = md[child]
                binding = re.match(r'i32 (\d+), (.+)\* undef, !"([^"]*)", '
                                   r'i32 (\d+), i32 (\d+), i32 (\d+), (.*)', row)
                if not binding:
                    raise ValueError('Unsupported resource binding: ' + row)
                resource_id, type_name, name, space, register, count, tail = binding.groups()
                item = dict(kind=kind, id=int(resource_id), type=type_name, name=name,
                            space=int(space), register=int(register), count=int(count))
                if kind == 'cbuffer':
                    size = re.match(r'i32 (\d+),', tail)
                    if not size:
                        raise ValueError('Missing constant buffer size')
                    item['bytes'] = int(size[1])
                resources.append(item)
    cameras = [r for r in resources if r['kind'] == 'cbuffer'
               and re.fullmatch(r'%[\w.]*CameraShaderConsts', r['type'])]
    result = dict(stage=stage, model=f'{major}.{minor}', resources=resources,
                  cameraBindings=len(cameras), runtimeEligible=False)
    if stage not in GRAPHICS:
        result.update(status='excluded', reason='Not a graphics shader')
        return result
    if (major, minor) != ('6', '0'):
        raise ValueError('Only original SM6.0 graphics shaders supported')
    if '@dx.op.viewID.' in source:
        raise ValueError('Already view-instanced shader')
    if cameras:
        result['camera'] = camera_usage(source)
        result['transform'] = 'camera_view_id'
    else:
        # No camera block is not proof of eye independence (screen textures,
        # other constant buffers, SV_Position and resource writes can vary).
        result['transform'] = 'metadata_only'
    result['requirements'] = ['pipeline_link_validation', 'per_eye_visibility',
                              'target_array_routing', 'binding_equivalence']
    if any(r['kind'] == 'uav' for r in resources):
        result['requirements'].append('uav_side_effect_audit')
    if any(r['kind'] == 'srv' for r in resources):
        result['requirements'].append('srv_eye_dependency_audit')
    if any(r['kind'] == 'cbuffer' and r not in cameras for r in resources):
        result['requirements'].append('non_camera_constants_audit')
    if cameras:
        result['requirements'].append('two_current_camera_blocks')
        if 147 in result['camera']['words']:
            result['requirements'].append('early_camera_word_147_unavailable')
    return result


def collect(index):
    shaders, pipelines = {}, []
    for row in index:
        pso = row.get('resources', {}).get('pPipelineState')
        if not isinstance(pso, int) or pso <= 0:
            raise ValueError('Missing PSO resource identity')
        hashes = []
        for shader in row['shaders']:
            sha = shader['sha256']
            if not re.fullmatch('[0-9a-f]{64}', sha):
                raise ValueError('Invalid shader SHA256')
            item = shaders.setdefault(sha, dict(paths=set(), psos=set()))
            item['paths'].add(shader['file'])
            item['psos'].add(pso)
            hashes.append(sha)
        # Preserve stages including duplicates: duplicate stages fail PSO checks.
        pipelines.append(dict(pso=pso, chunk=row.get('chunk'), shaders=hashes))
    if len({p['pso'] for p in pipelines}) != len(pipelines):
        raise ValueError('Duplicate PSO resource identity')
    return shaders, pipelines


def toolchain_identity(dxc_dir, assembler):
    files = [dxc_dir / name for name in ('dxc.exe', 'dxcompiler.dll', 'dxil.dll')]
    files += [assembler, Path(__file__), Path(__file__).with_name('patch_camera.py'),
              Path(__file__).with_name('camera_usage.py'), Path(__file__).with_name('upgrade_stage.py')]
    return dict(schema=SCHEMA, passes=PASSES,
                files={str(p.resolve()): digest(p.read_bytes()) for p in files})


def cache_key(sha, identity):
    return digest(json.dumps(dict(input=sha, toolchain=identity), sort_keys=True).encode())


def cached_record(directory, key, sha):
    try:
        row = read_json(directory / 'result.json')
        if not isinstance(row, dict) or not isinstance(row.get('artifacts'), dict):
            return None
        if row.get('status') not in ('validated', 'excluded', 'rejected'):
            return None
        if row['key'] != key or row['sha256'] != sha:
            return None
        for name, checksum in row['artifacts'].items():
            # Only names in this entry, never paths supplied by cache contents.
            if Path(name).name != name or digest((directory / name).read_bytes()) != checksum:
                return None
        if row['status'] == 'validated' and not {'stereo.dxil', 'stereo.ll'} <= row['artifacts'].keys():
            return None
        return row
    except (OSError, ValueError, KeyError, TypeError):
        return None


def run(command):
    result = subprocess.run([str(v) for v in command], capture_output=True,
                            encoding='utf-8', errors='replace', timeout=90)
    if result.returncode:
        raise ValueError((result.stderr or result.stdout or f'exit {result.returncode}').strip()[-6000:])
    return (result.stdout + result.stderr).strip()


def process_shader(sha, paths, output, identity, dxc_dir, assembler):
    # Verify the original bytes even on cache hits. Never substitute by filename.
    binary = None
    for name in sorted(paths):
        path = Path(name)
        if path.is_file():
            if digest(path.read_bytes()) != sha:
                raise ValueError('Original shader hash mismatch: ' + name)
            binary = path
    if binary is None:
        raise ValueError('Original shader file not found')
    key = cache_key(sha, identity)
    directory = output / 'cache' / key
    previous = cached_record(directory, key, sha)
    if previous:
        return previous, True
    directory.mkdir(parents=True, exist_ok=True)
    row = dict(sha256=sha, key=key, status='rejected', runtimeEligible=False, artifacts={})
    try:
        parts = container_parts(binary.read_bytes())
        if 'DXIL' not in parts:
            legacy = parts.get('SHEX', parts.get('SHDR', b''))
            token = struct.unpack_from('<I', legacy)[0] if len(legacy) >= 4 else 0xFFFFFFFF
            stage = {0: 'ps', 1: 'vs', 2: 'gs', 3: 'hs', 4: 'ds', 5: 'cs'}.get(token >> 16, 'unknown')
            row.update(status='excluded', stage=stage, reason='Legacy DXBC: no DXIL conversion implemented')
            save_json(directory / 'result.json', row)
            return row, False
        native = directory / 'native.ll'
        run([dxc_dir / 'dxc.exe', '-dumpbin', '-Fc', native, binary])
        source = native.read_text(encoding='utf-8')
        row.update(classify(source))
        row['artifacts']['native.ll'] = digest(native.read_bytes())
        if row['status'] != 'excluded':
            if row['transform'] == 'camera_view_id':
                patched, conversion = patch(source)
                row['conversion'] = conversion
            else:
                patched = upgrade(source)
            ir = directory / 'stereo.ll'
            ir.write_text(patched, encoding='utf-8')
            blob = directory / 'stereo.dxil'
            temporary = directory / 'validated.tmp.dxil'
            row['validation'] = run([assembler, dxc_dir, ir, temporary, *PASSES])
            temporary.replace(blob)
            row['status'] = 'validated'
            row['artifacts'].update({p.name: digest(p.read_bytes()) for p in (ir, blob)})
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        row.update(status='rejected', reason=str(error))
    save_json(directory / 'result.json', row)
    return row, False


def event_weights(events):
    weights = defaultdict(lambda: dict(draws=0, dispatches=0, replayEventSumMs=0.0,
                                      views=Counter(), nodes=Counter()))
    for event in events:
        if not event.get('pso') or event.get('kind') not in ('draw', 'dispatch'):
            continue
        row = weights[event['pso']]
        row['draws' if event['kind'] == 'draw' else 'dispatches'] += 1
        row['replayEventSumMs'] += event.get('ms', 0) or 0
        row['views'][event.get('view', 'unknown')] += 1
        row['nodes'][event.get('node', 'unknown')] += 1
    return weights


def pipeline_report(pipeline, results, weights):
    shaders = [results[sha] for sha in pipeline['shaders']]
    stages = [row.get('stage', 'unknown') for row in shaders]
    blockers = []
    if not stages or any(s not in GRAPHICS for s in stages) or stages.count('vs') != 1:
        blockers.append('not_supported_graphics_pipeline')
    if len(stages) != len(set(stages)):
        blockers.append('ambiguous_duplicate_stages')
    if ('hs' in stages) != ('ds' in stages):
        blockers.append('incomplete_tessellation_stages')
    if any(row['status'] != 'validated' for row in shaders):
        blockers.append('unvalidated_shader')
    bindings = defaultdict(set)
    for row in shaders:
        if 'conversion' in row:
            camera = row['conversion']
            bindings[(camera['space'], camera['camera_register'])].add(
                (camera['original_size'], camera['eye_stride'], camera['binding_size']))
    if any(len(layouts) != 1 for layouts in bindings.values()):
        blockers.append('camera_layout_conflict')
    return dict(**pipeline, stages=stages, bytecodeReady=not blockers,
                runtimeEligible=False, blockers=blockers,
                requirements=sorted({r for row in shaders for r in row.get('requirements', [])}),
                cameraBindings=[dict(space=space, register=register, layouts=sorted(layouts))
                                for (space, register), layouts in sorted(bindings.items())],
                workload=weights.get(pipeline['pso'], {}))


def family_reports(results, pipelines, weights):
    """Group audit work by resource/camera layout, not by bytecode substitutability."""
    psos_by_shader = defaultdict(set)
    for pipeline in pipelines:
        for sha in pipeline['shaders']:
            psos_by_shader[sha].add(pipeline['pso'])
    families = {}
    for sha, row in results.items():
        if row['status'] != 'validated':
            continue
        shape = dict(stage=row['stage'], transform=row['transform'], camera=row.get('camera'),
                     resources=[{k: v for k, v in resource.items() if k not in ('id', 'name')}
                                for resource in row['resources']])
        key = digest(json.dumps(shape, sort_keys=True).encode())
        family = families.setdefault(key, dict(key=key, layout=shape, shaders=[], psos=set()))
        family['shaders'].append(sha)
        family['psos'].update(psos_by_shader[sha])
    for family in families.values():
        family['psos'] = sorted(family['psos'])
        family['draws'] = sum(weights.get(pso, {}).get('draws', 0) for pso in family['psos'])
        family['note'] = 'Audit family only; each shader keeps its own hash and validated binary.'
    return sorted(families.values(), key=lambda f: (-f['draws'], f['key']))


def pass_coverage(pipelines, weights):
    nodes = defaultdict(lambda: dict(draws=0, bytecodeReadyDraws=0, runtimeEligibleDraws=0, psos=[]))
    for pipeline in pipelines:
        workload = weights.get(pipeline['pso'], {})
        if not workload.get('draws'):
            continue
        for node, count in workload['nodes'].items():
            row = nodes[node]
            row['draws'] += count
            row['bytecodeReadyDraws'] += count if pipeline['bytecodeReady'] else 0
            row['psos'].append(pipeline['pso'])
    return dict(sorted(nodes.items(), key=lambda item: -item[1]['draws']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--frame', type=Path)
    parser.add_argument('--dxc-dir', type=Path, required=True)
    parser.add_argument('--assembler', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    start = time.monotonic()
    identity = toolchain_identity(args.dxc_dir, args.assembler)
    inventory, pipelines = collect(read_json(args.index))
    weights = event_weights(read_json(args.frame)['events']) if args.frame else {}
    order = sorted(inventory, key=lambda sha: -sum(weights.get(p, {}).get('draws', 0)
                                                  for p in inventory[sha]['psos']))
    results = {}
    cache_hits = 0
    for number, sha in enumerate(order, 1):
        try:
            row, hit = process_shader(sha, inventory[sha]['paths'], args.out, identity,
                                      args.dxc_dir, args.assembler)
        except (ValueError, OSError) as error:
            row, hit = dict(sha256=sha, status='rejected', reason=str(error), runtimeEligible=False), False
        results[sha] = row
        cache_hits += hit
        if number % 25 == 0 or number == len(order):
            print(json.dumps(dict(processed=number, total=len(order), cacheHits=cache_hits,
                                  statuses=dict(Counter(r['status'] for r in results.values())))), flush=True)
    psos = [pipeline_report(p, results, weights) for p in pipelines]
    psos.sort(key=lambda p: (-p['workload'].get('draws', 0), p['pso']))
    families = family_reports(results, pipelines, weights)
    summary = dict(shaders=len(results), pipelines=len(psos), cacheHits=cache_hits,
                   statuses=dict(Counter(r['status'] for r in results.values())),
                   stages=dict(Counter(r.get('stage', 'unknown') for r in results.values())),
                   transforms=dict(Counter(r.get('transform', 'none') for r in results.values())),
                   bytecodeReadyPipelines=sum(p['bytecodeReady'] for p in psos),
                   auditFamilies=len(families),
                   runtimeEligiblePipelines=0, elapsedSeconds=time.monotonic() - start)
    args.out.mkdir(parents=True, exist_ok=True)
    save_json(args.out / 'manifest.json', dict(schema=SCHEMA, toolchain=identity, summary=summary,
              timingNote='Replay event sums are ranking hints, not additive live GPU frame time.',
              shaders=results, pipelines=psos, families=families, passes=pass_coverage(psos, weights)))
    print(json.dumps(summary, indent=2))
    return int(any(r['status'] == 'rejected' for r in results.values()))


if __name__ == '__main__':
    raise SystemExit(main())
