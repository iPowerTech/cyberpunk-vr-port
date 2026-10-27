"""Package exact creation snapshots without inventing native PSO fixed state."""
import argparse
import copy
import json
from pathlib import Path
import re

import batch_shaders as batch
from link_stage_signatures import link_pixel_inputs
from package_native_camera import save_binary, validate_stage
from patch_resource_pairs import patch_pairs

TYPES = dict(srv=0, uav=1, cbuffer=2, sampler=3)


def require(good, reason):
    if not good:
        raise ValueError(reason)


def number(value, maximum=0xffffffff, minimum=0):
    return type(value) is int and minimum <= value <= maximum


def digest(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def blob(directory, entry, suffix='.dxil'):
    require(isinstance(entry, dict) and digest(entry.get('sha256')) and
            entry.get('file') == entry['sha256'] + suffix and
            number(entry.get('bytes'), 1024 * 1024, 32), 'Invalid shader/root file identity')
    raw = (directory / entry['file']).read_bytes()
    require(len(raw) == entry['bytes'] and batch.digest(raw) == entry['sha256'], 'Shader/root hash or size mismatch')
    return raw


def key(family):
    return family['nativeVs'], family['nativePs']


def canonical(rows):
    return sorted(json.dumps(row, sort_keys=True) for row in rows)


def uses(info, stage):
    visibility = 1 if stage == 'vs' else 5
    return [dict(type=TYPES[r['kind']], visibility=visibility, register=r['register'],
                 space=r['space'], count=r['count'], bytes=r.get('bytes', 0)) for r in info['resources']]


def expected_pairs(resource_uses):
    return [dict(type=u['type'], visibility=u['visibility'], register=u['register'],
                 space=100 if u['visibility'] == 1 else 101)
            for u in resource_uses if u['type'] in (0, 2) and u['space'] == 0 and u['count'] == 1]


def camera_contract(family):
    value = family.get('nativeCamera')
    if value is None:
        return 0
    require(not family.get('nativeCameraVs') and type(value.get('schema')) is int and value['schema'] == 1 and
            value.get('mainFirst') is True and type(value.get('eyeStride')) is int and value['eyeStride'] == 1024 and
            type(value.get('bindingSize')) is int and value['bindingSize'] == 2048, 'Conflicting native camera schema')
    mask = 0
    for use in family['uses']:
        if use['type'] == 2 and use['space'] == 0 and use['register'] == 1:
            bit = 1 if use['visibility'] == 1 else 2
            require(use['count'] == 1 and use['bytes'] == 848 and not mask & bit, 'Conflicting native camera resource')
            mask |= bit
    require(mask in (1, 2, 3) and number(value.get('stages'), 3, 1) and value['stages'] == mask, 'Conflicting native camera stage mask')
    for stage, bit in (('vs', 1), ('ps', 2)):
        if stage == 'ps' and not family['nativePs']:
            require(stage not in value and not mask & bit, 'Unexpected native camera PS')
            continue
        require(isinstance(value.get(stage), dict) and type(value[stage].get('cameraPatched')) is bool and
                value[stage]['cameraPatched'] == bool(mask & bit), 'Conflicting native camera stage flag')
    return mask


def family_blobs(family):
    yield family['vs']
    if family['nativePs']:
        yield family['ps']
    if 'nativeCameraVs' in family:
        yield family['nativeCameraVs']
    if 'nativeCamera' in family:
        yield family['nativeCamera']['vs']
        if family['nativePs']:
            yield family['nativeCamera']['ps']


def validate_family(family, directory):
    require(digest(family.get('nativeVs')) and number(family.get('nativeVsBytes'), 1024 * 1024, 32), 'Invalid native VS identity')
    require((family.get('nativePs') == '' and family.get('nativePsBytes') == 0) or
            (digest(family.get('nativePs')) and number(family.get('nativePsBytes'), 1024 * 1024, 32)), 'Invalid native PS identity')
    require(family.get('runtimeEligible') is False, 'Runtime admission is not an offline package claim')
    require(isinstance(family.get('uses'), list) and len(family['uses']) <= 1024 and
            isinstance(family.get('pairs'), list) and len(family['pairs']) <= 512, 'Invalid resource contract')
    for use in family['uses']:
        require(set(use) == {'type', 'visibility', 'register', 'space', 'count', 'bytes'} and
                number(use['type'], 3) and number(use['visibility'], 5) and use['visibility'] in (1, 5) and
                number(use['register']) and number(use['space']) and number(use['count'], minimum=1) and
                number(use['bytes'], 65536), 'Invalid resource use')
        require(use['visibility'] == 1 or bool(family['nativePs']), 'Pixel resource without pixel shader')
    for pair in family['pairs']:
        require(set(pair) == {'type', 'visibility', 'register', 'space'} and number(pair['type'], 3) and pair['type'] in (0, 2) and
                number(pair['visibility'], 5) and pair['visibility'] in (1, 5) and number(pair['register'], 0x7ffffffe) and
                number(pair['space'], 65535, 1), 'Invalid resource pair')
        require(sum(u['type'] == pair['type'] and u['visibility'] == pair['visibility'] and
                    u['space'] == 0 and u['register'] == pair['register'] and u['count'] == 1
                    for u in family['uses']) == 1, 'Pair has no unique source resource')
    camera_contract(family)
    return sum(len(blob(directory, entry)) for entry in family_blobs(family))


def load_sources(snapshot_path, conversion):
    report = batch.read_json(snapshot_path)
    source = report['nativeShaderSources']
    require(Path(source['directory']).name == source['directory'], 'Invalid source directory')
    source_dir = snapshot_path.parent / source['directory']
    rows = source['records']
    require(isinstance(rows, list) and 0 < len(rows) <= 512, 'Invalid source snapshot count')
    require(all(number(r.get('id'), 2**64 - 1, 1) for r in rows) and len({r['id'] for r in rows}) == len(rows), 'Duplicate/invalid source id')
    coverage = batch.read_json(conversion / 'coverage.json')['records']
    require(all(number(r.get('sourceId'), 2**64 - 1, 1) for r in coverage) and
            len({r['sourceId'] for r in coverage}) == len(coverage), 'Duplicate/invalid conversion id')
    by_id = {r['sourceId']: r for r in coverage}
    require(set(by_id) == {r['id'] for r in rows}, 'Snapshot/conversion source ids differ')
    inventory = batch.read_json(conversion / 'batch/manifest.json')['shaders']
    result = []
    for row in rows:
        converted = by_id[row['id']]
        require(converted.get('status') == 'validated' and converted.get('failures') == [] and
                converted.get('runtimeEligible') is False, 'Unvalidated source conversion')
        require(converted.get('native') == {stage: row[stage] for stage in ('vs', 'ps', 'root')} and
                converted.get('input') == row['input'], 'Conversion source identity/input differs')
        for stage in ('vs', 'ps', 'root'):
            blob(source_dir, row[stage], '.bin')
        stage_info, stage_ir, all_uses = {}, {}, []
        for stage in ('vs', 'ps'):
            original = inventory[row[stage]['sha256']]
            require(original.get('status') == 'validated' and original.get('stage') == stage and
                    digest(original.get('key')), 'Invalid original shader inventory')
            path = conversion / 'batch/cache' / original['key'] / 'native.ll'
            raw = path.read_bytes()
            require(batch.digest(raw) == original['artifacts']['native.ll'], 'Original IR hash changed')
            info = batch.classify(raw.decode('utf-8'))
            require(info['stage'] == stage and info['resources'] == original['resources'] and
                    info == converted['stages'][stage]['original'], 'Original resource/camera classification differs')
            stage_info[stage], stage_ir[stage] = info, raw.decode('utf-8')
            all_uses.extend(uses(info, stage))
        mask = 0
        for stage, bit in (('vs', 1), ('ps', 2)):
            camera = validate_stage(stage_info[stage], stage, all_uses)
            require(type(converted[stage].get('cameraPatched')) is bool and
                    converted[stage]['cameraPatched'] == camera, 'Converted camera stage flag differs')
            if camera:
                mask |= bit
                layout = converted['stages'][stage]['layout']
                require(layout and layout.get('main_first') is True and
                        (layout.get('original_size'), layout.get('eye_stride'), layout.get('binding_size'),
                         layout.get('camera_register'), layout.get('space')) == (848, 1024, 2048, 1, 0), 'Unsupported converted camera layout')
            blob(conversion / 'package', converted[stage])
        require(mask in (1, 2, 3) and mask == converted.get('cameraStageMask'), 'Converted camera mask differs')
        require(converted['interfaceLink']['entry']['sha256'] == converted['ps']['sha256'], 'Linked native camera PS differs')
        result.append(dict(snapshot=row, converted=converted, uses=all_uses, ir=stage_ir, mask=mask))
    return result


def paired_stage(sha, stage, source, out, identity, dxc, assembler):
    space = 100 if stage == 'vs' else 101
    cache_key = batch.cache_key(sha, dict(identity, mode='paired-resources', stage=stage, space=space))
    directory = out / 'cache' / cache_key
    previous = batch.cached_record(directory, cache_key, sha)
    if previous and previous['status'] == 'validated' and 'validated.ll' in previous['artifacts']:
        return previous, directory, True
    directory.mkdir(parents=True, exist_ok=True)
    text, report = patch_pairs(source, space)
    ir, binary, validated = [directory / name for name in ('stereo.ll', 'stereo.dxil', 'validated.ll')]
    ir.write_text(text, encoding='utf-8')
    validation = batch.run([assembler, dxc, ir, binary, *batch.PASSES])
    batch.run([dxc / 'dxc.exe', '-dumpbin', '-Fc', validated, binary])
    record = dict(key=cache_key, sha256=sha, status='validated', stage=stage, report=report, validation=validation,
                  artifacts={p.name: batch.digest(p.read_bytes()) for p in (ir, binary, validated)})
    batch.save_json(directory / 'result.json', record)
    return record, directory, False


def package(snapshot_path, conversion, out, dxc, assembler, base=None):
    sources = load_sources(snapshot_path, conversion)
    families, locations = [], {}
    if base:
        manifest = batch.read_json(base)
        require(type(manifest.get('schema')) is int and manifest['schema'] == 1 and manifest.get('purpose') == 'development-linkage-only' and
                isinstance(manifest.get('families'), list) and 0 < len(manifest['families']) <= 512, 'Invalid base manifest')
        families = copy.deepcopy(manifest['families'])
        for family in families:
            require(key(family) not in locations, 'Duplicate base family')
            validate_family(family, base.parent)
            locations[key(family)] = base.parent
    old_count = len(families)
    by_key = {key(f): f for f in families}
    out.mkdir(parents=True, exist_ok=True)
    identity = batch.toolchain_identity(dxc, assembler)
    for name in ('patch_resource_pairs.py', 'link_stage_signatures.py', 'package_native_camera.py', Path(__file__).name):
        helper = Path(__file__).with_name(name)
        identity['files'][str(helper.resolve())] = batch.digest(helper.read_bytes())
    batch.save_json(out / 'toolchain.json', identity)
    stages, receipts = {}, []
    hits = 0
    for source in sources:
        snapshot, converted = source['snapshot'], source['converted']
        pair = snapshot['vs']['sha256'], snapshot['ps']['sha256']
        existing = by_key.get(pair)
        if existing:
            require((existing['nativeVsBytes'], existing['nativePsBytes']) ==
                    (snapshot['vs']['bytes'], snapshot['ps']['bytes']) and canonical(existing['uses']) == canonical(source['uses']),
                    'Existing family conflicts with native source bytes/resources')
            require(canonical(existing['pairs']) == canonical(expected_pairs(source['uses'])), 'Existing family pair contract differs')
            require(camera_contract(existing) == source['mask'], 'Existing family camera contract differs')
            disposition = 'preserved-base' if locations[pair] == (base.parent if base else None) else 'deduplicated-snapshot'
        else:
            family = dict(nativeVs=pair[0], nativePs=pair[1], nativeVsBytes=snapshot['vs']['bytes'],
                          nativePsBytes=snapshot['ps']['bytes'], pairs=[], uses=source['uses'], runtimeEligible=False)
            pair_stages = {}
            for stage in ('vs', 'ps'):
                sha = snapshot[stage]['sha256']
                if sha not in stages:
                    row, directory, hit = paired_stage(sha, stage, source['ir'][stage], out, identity, dxc, assembler)
                    stages[sha] = row, directory
                    hits += int(hit)
                row, directory = stages[sha]
                require(row['stage'] == stage, 'Conflicting original shader stage')
                pair_stages[stage] = directory
                for binding in row['report']['pairs']:
                    require(binding['sourceSpace'] == 0 and binding['count'] == 2 and
                            binding['register'] == binding['sourceRegister'] * 2 and binding['space'] == (100 if stage == 'vs' else 101),
                            'Pair ABI differs')
                    family['pairs'].append(dict(type=TYPES[binding['kind']], visibility=1 if stage == 'vs' else 5,
                                                register=binding['sourceRegister'], space=binding['space']))
            require(canonical(family['pairs']) == canonical(expected_pairs(source['uses'])), 'Generated pair contract differs')
            linked, link_report = link_pixel_inputs((pair_stages['vs'] / 'validated.ll').read_text(),
                                                    (pair_stages['ps'] / 'validated.ll').read_text())
            link_dir = out / 'linked'
            link_dir.mkdir(exist_ok=True)
            link_name = batch.digest(linked.encode())
            link_ir, link_binary = link_dir / (link_name + '.ll'), link_dir / (link_name + '.dxil')
            link_ir.write_text(linked, encoding='utf-8')
            validation = batch.run([assembler, dxc, link_ir, link_binary, *batch.PASSES])
            family['vs'] = save_binary(out, (pair_stages['vs'] / 'stereo.dxil').read_bytes())
            family['ps'] = save_binary(out, link_binary.read_bytes())
            native_camera = dict(schema=1, mainFirst=True, eyeStride=1024, bindingSize=2048, stages=source['mask'])
            for stage in ('vs', 'ps'):
                native_camera[stage] = save_binary(out, blob(conversion / 'package', converted[stage]))
                native_camera[stage]['cameraPatched'] = converted[stage]['cameraPatched']
            family['nativeCamera'] = native_camera
            validate_family(family, out)
            require(len(families) < 512, 'Family capacity exceeded')
            families.append(family)
            by_key[pair], locations[pair] = family, out
            existing = family
            disposition = 'new-snapshot'
            batch.save_json(link_dir / (link_name + '.json'), dict(report=link_report, validation=validation))
        receipts.append(dict(sourceId=snapshot['id'], native=snapshot, disposition=disposition,
                             familyIndex=families.index(existing) + 1, cameraStageMask=source['mask'],
                             outputs={name: existing[name] for name in ('vs', 'ps', 'nativeCamera')}, runtimeAdmitted=False))
    total_bytes = 0
    for family in families:
        directory = locations[key(family)]
        for entry in family_blobs(family):
            data = blob(directory, entry)
            total_bytes += len(data)
            require(total_bytes <= 128 * 1024 * 1024, 'Shader package exceeds runtime memory bound')
            save_binary(out, data)
    result = dict(schema=1, purpose='development-linkage-only', families=families)
    batch.save_json(out / 'families.json', result)
    summary = dict(sources=len(sources), baseFamilies=old_count, newFamilies=len(families) - old_count,
                   families=len(families), preservedSourcePairs=sum(r['disposition'] == 'preserved-base' for r in receipts),
                   generatedStages=len(stages), stageCacheHits=hits, nativeCameraFamilies=sum('nativeCamera' in f or 'nativeCameraVs' in f for f in families),
                   runtimeAdmitted=False, manifestSHA256=batch.digest((out / 'families.json').read_bytes()))
    batch.save_json(out / 'provenance.json', dict(summary=summary, records=receipts,
                    snapshot=dict(file=str(snapshot_path), sha256=batch.digest(snapshot_path.read_bytes())),
                    conversion=dict(file=str(conversion / 'coverage.json'), sha256=batch.digest((conversion / 'coverage.json').read_bytes())),
                    base=dict(file=str(base), sha256=batch.digest(base.read_bytes())) if base else None,
                    limitation='Exact shader sources only. Fixed PSO state/input/root validation uses actual runtime creation callbacks; no PSO or GPU test was run.'))
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--conversion', type=Path, required=True)
    parser.add_argument('--base', type=Path)
    parser.add_argument('--dxc-dir', type=Path, required=True)
    parser.add_argument('--assembler', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.snapshot.resolve(), args.conversion.resolve(), args.out.resolve(),
                             args.dxc_dir.resolve(), args.assembler.resolve(), args.base.resolve() if args.base else None), indent=2))


if __name__ == '__main__':
    main()
