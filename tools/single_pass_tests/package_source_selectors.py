"""Package audited original-shader selectors; grants no resource/copy permission."""

import argparse
import json
from pathlib import Path
import re

from source_selector_report import EvidenceCatalog, Visibility, catalog_report, load_evidence

MAX_FAMILIES = 64
MAX_SITES = 256
MAX_TOTAL_SITES = 4096
MAX_BYTES = 1024 * 1024
PURPOSE = 'original-source-selector-evaluation-only'
HASH = re.compile(r'[0-9a-f]{64}')
SSA = re.compile(r'%[0-9]{1,31}')


def require(value, reason):
    if not value:
        raise ValueError(reason)


def integer(value, maximum=2**32 - 1, minimum=0):
    require(type(value) is int and minimum <= value <= maximum, 'Invalid selector integer')
    return value


def digest(value):
    require(type(value) is str and HASH.fullmatch(value) is not None, 'Invalid original selector SHA256')
    return value


def cbv_key(key, family):
    require(type(key.visibility) is Visibility and key.root_sha256 == family.root_sha256,
            'CBV root/stage differs from original family')
    values = [integer(value) for value in (key.shader_register, key.space, key.parameter, key.table_offset)]
    require(values == [4, 0, 1 if key.visibility == Visibility.VS else 19, 0],
            'Unsupported original b4 root binding')
    return dict(root=digest(key.root_sha256), visibility=int(key.visibility), register=4,
                space=0, parameter=key.parameter, tableOffset=0)


def srv_key(key, cbv):
    require(type(key.visibility) is Visibility and key.visibility == cbv.visibility and key.root_sha256 == cbv.root_sha256,
            'SRV root/stage differs from selector b4')
    for value in (key.parameter, key.space, key.shader_register_base, key.original_table_offset, key.declared_extent):
        integer(value)
    require((key.parameter, key.space) in (((13, 1),) if key.visibility == Visibility.VS else ((31, 1), (33, 3))) and
            key.shader_register_base == 0 and key.original_table_offset == 0 and key.declared_extent == 32768,
            'Unsupported original SRV root range')
    return dict(root=digest(key.root_sha256), visibility=int(key.visibility), parameter=key.parameter,
                space=key.space, shaderRegisterBase=0, originalTableOffset=0, declaredExtent=32768)


def build_package(catalog, reflection):
    """Pure export of an already validated catalog and its original reflection."""
    require(isinstance(catalog, EvidenceCatalog) and 0 < len(catalog.families) <= MAX_FAMILIES,
            'Invalid selector catalog extent')
    digest(catalog.evidence_sha256)
    require(isinstance(reflection, dict) and isinstance(reflection.get('shaders'), dict), 'Missing original shader reflection')
    reflected = {}
    for shader in reflection['shaders'].values():
        require(isinstance(shader, dict) and isinstance(shader.get('resources'), list), 'Invalid shader reflection')
        sha = digest(shader.get('sha256'))
        require(sha not in reflected and shader.get('stage') in ('vs', 'ps'), 'Ambiguous shader reflection')
        reflected[sha] = shader
    inventory = catalog_report(catalog)
    require(inventory['evidenceSha256'] == catalog.evidence_sha256, 'Static catalog evidence changed')
    families = []
    seen_keys, seen_numbers = set(), set()
    total_sites = 0
    for plan in catalog.families:
        for sha in (plan.key.root_sha256, plan.key.vs_sha256, plan.key.ps_sha256):
            digest(sha)
        integer(plan.number, MAX_FAMILIES, 1)
        require(plan.key not in seen_keys and plan.number not in seen_numbers, 'Ambiguous original selector family')
        seen_keys.add(plan.key)
        seen_numbers.add(plan.number)
        require(type(plan.unsupported_reason) is str and len(plan.unsupported_reason) <= 1024 and
                len(plan.unresolved_inputs) <= 64 and all(type(value) is str and 0 < len(value) <= 1024
                                                       for value in plan.unresolved_inputs),
                'Invalid unsupported selector reason/inputs')
        for stage, sha in (('vs', plan.key.vs_sha256), ('ps', plan.key.ps_sha256)):
            require(sha in reflected and reflected[sha]['stage'] == stage, 'Original shader stage/hash differs')
        family = dict(number=plan.number, root=plan.key.root_sha256, vs=plan.key.vs_sha256, ps=plan.key.ps_sha256,
                      supported=plan.supported, unsupportedReason=plan.unsupported_reason,
                      unresolvedInputs=list(plan.unresolved_inputs), cbvs=[], sites=[])
        if not plan.supported:
            require(not plan.sites and bool(plan.unresolved_inputs), 'Unsupported family fabricated a partial b4 plan')
            families.append(family)
            continue
        require(not plan.unresolved_inputs and 0 < len(plan.sites) <= MAX_SITES and 0 < len(plan.required_cbvs) <= 2,
                'Invalid supported selector plan extent')
        total_sites += len(plan.sites)
        require(total_sites <= MAX_TOTAL_SITES, 'Selector package exceeds total site capacity')
        requirements = {}
        for key in plan.required_cbvs:
            view = cbv_key(key, plan.key)
            shader = reflected[plan.key.vs_sha256 if key.visibility == Visibility.VS else plan.key.ps_sha256]
            resources = [resource for resource in shader['resources'] if
                         resource.get('type') == 2 and resource.get('space') == 0 and resource.get('register') == 4]
            require(len(resources) == 1 and type(resources[0].get('count')) is int and resources[0]['count'] == 1,
                    'Original b4 declaration is missing/ambiguous')
            for field in ('type', 'space', 'register', 'count'):
                integer(resources[0].get(field))
            size = integer(resources[0].get('bytes'), 65536, 1)
            offsets = sorted({site.byte_offset for site in plan.sites if site.cbv == key})
            for offset in offsets:
                integer(offset, 65532)
                require(offset % 4 == 0 and offset + 4 <= size, 'Selector DWORD is outside original reflected b4')
            extent = plan.required_byte_extent(key)
            require(offsets and extent == offsets[-1] + 4 and extent <= size, 'B4 required byte extent differs')
            view.update(reflectedBytes=size, requiredByteExtent=extent, dwordOffsets=offsets)
            requirements[key] = view
            family['cbvs'].append(view)
        seen_sites = set()
        for site in plan.sites:
            cbv = cbv_key(site.cbv, plan.key)
            require(site.cbv in requirements and type(site.byte_offset) is int and site.byte_offset in requirements[site.cbv]['dwordOffsets'],
                    'Selector source DWORD identity differs')
            expected_shader = plan.key.vs_sha256 if site.cbv.visibility == Visibility.VS else plan.key.ps_sha256
            require(site.shader_sha256 == expected_shader, 'Selector shader/stage differs from exact original family')
            require(type(site.create_handle_ssa) is str and SSA.fullmatch(site.create_handle_ssa) and
                    type(site.index_ssa) is str and SSA.fullmatch(site.index_ssa) and site.create_handle_ssa != site.index_ssa,
                    'Invalid original selector SSA identity')
            integer(site.create_handle_line, 1000000, 1)
            identity = (site.shader_sha256, site.create_handle_ssa)
            require(identity not in seen_sites, 'Duplicate original selector site')
            seen_sites.add(identity)
            require(len(site.operations) == 1 and site.operations[0].opcode == 'add' and
                    type(site.operations[0].operand) is int and site.operations[0].operand == 0,
                    'Only audited original add-zero selectors are supported')
            family['sites'].append(dict(shader=site.shader_sha256, createHandleSSA=site.create_handle_ssa,
                                        createHandleLine=site.create_handle_line, indexSSA=site.index_ssa,
                                        cbv=cbv, byteOffset=site.byte_offset, operations=[dict(opcode='add', operand=0)],
                                        table=srv_key(site.table, site.cbv)))
        families.append(family)
    result = dict(schema=1, purpose=PURPOSE, evidenceSha256=catalog.evidence_sha256, families=families)
    require(len(serialize(result)) <= MAX_BYTES, 'Selector package exceeds JSON byte capacity')
    return result


def serialize(package):
    return (json.dumps(package, indent=2, ensure_ascii=True, allow_nan=False) + '\n').encode('utf-8')


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate evidence JSON key')
        result[key] = value
    return result


def read_json(path):
    return json.loads(Path(path).read_bytes(), object_pairs_hook=_unique_object,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError('Non-finite evidence JSON')))


def package(bounds_path, reflection_path):
    # Reject ambiguous source JSON before the authoritative IR/catalog audit.
    read_json(bounds_path)
    reflection = read_json(reflection_path)
    return build_package(load_evidence(bounds_path, reflection_path), reflection)


def main():
    root = Path(__file__).resolve().parents[2] / 'build' / 'single-pass-20261005'
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bounds', type=Path, default=root / 'source-selector-bounds-audit' / 'bounds.json')
    parser.add_argument('--reflection', type=Path, default=root / 'source-table-reflection-audit' / 'matrix.json')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    require(args.out.suffix == '.json' and args.out.resolve() not in (args.bounds.resolve(), args.reflection.resolve()),
            'Selector output must be a separate JSON artifact')
    require(not args.out.exists(), 'Use a fresh selector output file')
    result = package(args.bounds, args.reflection)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(serialize(result))
    print(json.dumps(dict(file=str(args.out), families=len(result['families']),
                          supportedFamilies=sum(family['supported'] for family in result['families']),
                          sites=sum(len(family['sites']) for family in result['families']),
                          evidenceSha256=result['evidenceSha256'], descriptorOwnershipProved=False,
                          inputContentsFrozen=False, nativeExecutionPermission=False, nativeCommandsSuppressed=0)))


if __name__ == '__main__':
    main()
