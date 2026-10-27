"""Pure evaluation of audited original-shader descriptor selectors.

Only caller-supplied immutable b4 view bytes are consumed. Artifact/IR matching
does not certify the byte capture epoch, GPU contents, allocation or execution.
"""

from dataclasses import dataclass
from enum import IntEnum
import hashlib
import json
from pathlib import Path
import re
import struct


DECLARED_EXTENT = 32768
_HASH = re.compile(r'[0-9a-f]{64}')
_HANDLE = re.compile(r'call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 (\d+), i32 (\d+), i32 (%\d+|-?\d+), i1 (true|false)\)')
_ADD = re.compile(r'add i32 (%\d+), (-?\d+)')
_EXTRACT = re.compile(r'extractvalue %dx\.types\.CBufRet\.i32 (%\d+), (\d+)')
_LOAD = re.compile(r'call %dx\.types\.CBufRet\.i32 @dx\.op\.cbufferLoadLegacy\.i32\(i32 59, %dx\.types\.Handle (%\d+), i32 (\d+)\)')


class SelectorError(ValueError):
    pass


class EvidenceMismatch(SelectorError):
    pass


class UnsupportedFamily(SelectorError):
    pass


class BindingMismatch(SelectorError):
    pass


class SelectorOutOfRange(SelectorError):
    pass


class Visibility(IntEnum):
    VS = 1
    PS = 5

    @property
    def stage(self):
        return 'vs' if self == Visibility.VS else 'ps'


@dataclass(frozen=True, order=True)
class FamilyKey:
    root_sha256: str
    vs_sha256: str
    ps_sha256: str


@dataclass(frozen=True, order=True)
class CbvKey:
    root_sha256: str
    visibility: Visibility
    shader_register: int
    space: int
    parameter: int
    table_offset: int


@dataclass(frozen=True)
class OwnedCbvBytes:
    key: CbvKey
    # Starts at the bound CBV's GPU address, not its COM resource base.
    data: bytes


@dataclass(frozen=True)
class U32Operation:
    opcode: str
    operand: int

    def apply(self, value):
        if self.opcode != 'add' or type(self.operand) is not int or self.operand != 0:
            raise EvidenceMismatch('Only the exact audited add i32 value,0 operation is supported')
        return value


@dataclass(frozen=True, order=True)
class SrvTableKey:
    root_sha256: str
    visibility: Visibility
    parameter: int
    space: int
    shader_register_base: int
    original_table_offset: int
    declared_extent: int


@dataclass(frozen=True)
class SelectorSite:
    shader_sha256: str
    create_handle_ssa: str
    create_handle_line: int
    index_ssa: str
    cbv: CbvKey
    byte_offset: int
    operations: tuple[U32Operation, ...]
    table: SrvTableKey


@dataclass(frozen=True)
class FamilyPlan:
    key: FamilyKey
    number: int
    sites: tuple[SelectorSite, ...]
    unsupported_reason: str = ''
    unresolved_inputs: tuple[str, ...] = ()

    @property
    def supported(self):
        return not self.unsupported_reason

    @property
    def required_cbvs(self):
        return tuple(sorted({site.cbv for site in self.sites}))

    def required_byte_extent(self, key):
        offsets = [site.byte_offset for site in self.sites if site.cbv == key]
        if not offsets:
            raise BindingMismatch('CBV key is not required by this original shader family')
        return max(offsets) + 4


@dataclass(frozen=True)
class EvidenceCatalog:
    families: tuple[FamilyPlan, ...]
    evidence_sha256: str

    def select(self, key):
        matches = [family for family in self.families if family.key == key]
        if len(matches) != 1:
            raise BindingMismatch('Original root/VS/PS identity is not one exact audited family')
        return matches[0]


@dataclass(frozen=True)
class SiteSelection:
    site: SelectorSite
    source_dword: int
    shader_index: int
    descriptor_offset: int


@dataclass(frozen=True)
class SlotInterval:
    offset: int
    count: int
    # D3D12_DESCRIPTOR_RANGE_TYPE_SRV; never CBV/UAV/sampler by inference.
    type: int = 0


@dataclass(frozen=True)
class TableSelection:
    key: SrvTableKey
    offsets: tuple[int, ...]
    count: int
    uses: tuple[SlotInterval, ...]


@dataclass(frozen=True)
class SelectorResult:
    family: FamilyKey
    selections: tuple[SiteSelection, ...]
    tables: tuple[TableSelection, ...]

    def report(self):
        return dict(
            family=dict(root=self.family.root_sha256, vs=self.family.vs_sha256, ps=self.family.ps_sha256),
            sites=[dict(shader=entry.site.shader_sha256, visibility=int(entry.site.cbv.visibility),
                        createHandleSSA=entry.site.create_handle_ssa, createHandleLine=entry.site.create_handle_line,
                        indexSSA=entry.site.index_ssa, sourceCbvRegister=4, sourceCbvSpace=0,
                        sourceCbvParameter=entry.site.cbv.parameter, sourceCbvTableOffset=entry.site.cbv.table_offset,
                        sourceByteOffset=entry.site.byte_offset, sourceDword=entry.source_dword,
                        shaderIndex=entry.shader_index, descriptorOffset=entry.descriptor_offset,
                        operations=[dict(opcode=op.opcode, operand=op.operand) for op in entry.site.operations])
                   for entry in self.selections],
            tables=[dict(parameter=table.key.parameter, visibility=int(table.key.visibility), space=table.key.space,
                         shaderRegisterBase=table.key.shader_register_base, type=0,
                         originalTableOffset=table.key.original_table_offset,
                         declaredExtent=table.key.declared_extent, offsets=list(table.offsets), count=table.count,
                         uses=[dict(offset=use.offset, count=use.count, type=use.type) for use in table.uses])
                    for table in self.tables],
            selectorEvaluationCompleteForKnownSites=True, selectorByteCaptureEpochIndependentlyVerified=False,
            completeShaderInputsProved=False, inputContentsFrozen=False, copyPermission=False,
            captureExtentAdmitted=False, nativeExecutionPermission=False, nativeExecutionPermitted=False,
            nativeSuppression=0, nativeCommandsSuppressed=0, performanceGainProved=False,
        )


def _require(condition, message):
    if not condition:
        raise EvidenceMismatch(message)


def _root_range(root, kind, visibility, register, space, parameter=None):
    matches = []
    for table in root['parameters']:
        if table['visibility'] not in (0, int(visibility)) or (parameter is not None and table['parameter'] != parameter):
            continue
        for descriptor_range in table['ranges']:
            if descriptor_range['type'] != kind or descriptor_range['space'] != space:
                continue
            first, count = descriptor_range['register'], descriptor_range['rootCount']
            if first <= register and (count == 0xFFFFFFFF or register - first < count):
                matches.append((table['parameter'], descriptor_range))
    _require(len(matches) == 1, 'Original root/stage/register/space range is not unique')
    return matches[0]


def _compile_site(key, shader, reflected, root, site, ir):
    visibility = Visibility.VS if shader['stage'] == 'vs' else Visibility.PS
    handle = _HANDLE.fullmatch(ir[site['createHandleSSA']]['rhs'])
    _require(handle is not None and int(handle[1]) == 0 and int(handle[2]) == site['resourceId'] and
             handle[3] == site['indexSSA'] and (handle[4] == 'true') == site['nonUniform'], 'Original SRV createHandle changed')
    srv = [resource for resource in reflected['resources'] if resource['type'] == 0 and resource['id'] == site['resourceId']]
    _require(len(srv) == 1, 'Original SRV declaration is ambiguous')
    srv = srv[0]
    _require(srv['register'] == 0 and srv['space'] == site['space'] and srv['count'] == DECLARED_EXTENT and
             site['declaredCountNotProof'] == DECLARED_EXTENT, 'Declared SRV extent/register/space changed')
    _require(site['expression'] == 'identity' and site['sourceInput']['type'] == 'cbv', 'Non-CBV selector was blanket admitted')
    for node in site['chain']:
        _require(ir.get(node['ssa']) == {key: node[key] for key in ('rhs', 'line', 'block')}, 'Selector SSA/line does not match original IR')
    index_node = ir[site['indexSSA']]
    add = _ADD.fullmatch(index_node['rhs'])
    _require(add is not None and int(add[2]) == 0, 'Audited selector operation is not exact add0')
    value_ssa = add[1]
    extract = _EXTRACT.fullmatch(ir[value_ssa]['rhs'])
    _require(extract is not None, 'Selector value is not an original integer CBV component')
    load_ssa, component = extract[1], int(extract[2])
    load = _LOAD.fullmatch(ir[load_ssa]['rhs'])
    _require(load is not None and component < 4, 'Selector is not an exact legacy CBV load')
    cbv_handle, row = load[1], int(load[2])
    handle = _HANDLE.fullmatch(ir[cbv_handle]['rhs'])
    _require(handle is not None and int(handle[1]) == 2 and handle[3] == '4', 'CBV createHandle association changed')
    cbv_resource = [resource for resource in reflected['resources'] if resource['type'] == 2 and resource['id'] == int(handle[2])]
    _require(len(cbv_resource) == 1 and cbv_resource[0]['register'] == 4 and cbv_resource[0]['space'] == 0 and
             cbv_resource[0]['count'] == 1, 'Selector source is not the exact b4/space0 CBV')
    cbv_resource = cbv_resource[0]
    offset = row * 16 + component * 4
    source = site['sourceInput']
    _require(source['valueSSA'] == value_ssa and source['loadSSA'] == load_ssa and source['scalarType'] == 'i32' and
             source['byteOffset'] == offset and source['byteCount'] == 4 and source['component'] == component and
             source['legacyRow'] == str(row), 'Audited source DWORD offset does not match original SSA')
    _require(all(source['resource'][field] == cbv_resource[field] for field in ('id', 'type', 'space', 'register', 'count')) and
             source['resource']['handleSSA'] == cbv_handle and source['resource']['handleIndex'] == '4' and
             source['resource']['declaredCbufferBytes'] == cbv_resource['bytes'], 'Source CBV declaration identity changed')
    _require(offset + 4 <= cbv_resource['bytes'], 'Selector DWORD exceeds its reflected CBV declaration')
    cbv_parameter, cbv_range = _root_range(root, 2, visibility, 4, 0)
    parameter, srv_range = _root_range(root, 0, visibility, 0, srv['space'], site['parameter'])
    _require(srv_range['rootCount'] == 0xFFFFFFFF and srv_range['register'] == 0, 'Original bindless root range changed')
    _require(len(site['chain']) == 3 and [node['ssa'] for node in site['chain']] == [site['indexSSA'], value_ssa, load_ssa],
             'Unexpected selector SSA chain requires a new exact audit')
    cbv_key = CbvKey(key.root_sha256, visibility, 4, 0, cbv_parameter, cbv_range['offset'] + 4 - cbv_range['register'])
    table = SrvTableKey(key.root_sha256, visibility, parameter, srv['space'], 0, srv_range['offset'], DECLARED_EXTENT)
    return SelectorSite(shader['sha256'], site['createHandleSSA'], site['createHandleLine'], site['indexSSA'],
                        cbv_key, offset, (U32Operation('add', 0),), table)


def load_evidence(bounds_path=None, reflection_path=None):
    """Load artifact files/IR once; never opens a process, device or memory map."""
    root_path = Path(__file__).resolve().parents[2] / 'build' / 'single-pass-20261005'
    bounds_path = Path(bounds_path) if bounds_path is not None else root_path / 'source-selector-bounds-audit' / 'bounds.json'
    reflection_path = Path(reflection_path) if reflection_path is not None else root_path / 'source-table-reflection-audit' / 'matrix.json'
    bounds_bytes, reflection_bytes = bounds_path.read_bytes(), reflection_path.read_bytes()
    bounds, matrix = json.loads(bounds_bytes), json.loads(reflection_bytes)
    shaders = {shader['sha256']: shader for shader in bounds['shaders']}
    reflected = {shader['sha256']: shader for shader in matrix['shaders'].values()}
    _require(len(shaders) == len(bounds['shaders']) == len(reflected) == len(matrix['shaders']), 'Original shader identities are duplicated/missing')
    original = {}
    for sha, shader in shaders.items():
        _require(_HASH.fullmatch(sha) is not None and shader['irFile'] == sha + '.ll' and
                 shader['irFile'] == reflected[sha]['irFile'] and shader['stage'] == reflected[sha]['stage'], 'Original shader/file/stage identity changed')
        raw = (reflection_path.parent / shader['irFile']).read_bytes()
        _require(hashlib.sha256(raw).hexdigest() == shader['irTextSha256'], 'Original IR does not match the audited hash')
        ir, block = {}, '0'
        for number, line in enumerate(raw.decode('utf-8').splitlines(), 1):
            label = re.match(r'; <label>:(\d+)', line)
            if label:
                block = label[1]
            assignment = re.fullmatch(r'\s*(%\d+) = (.*)', line.split(';', 1)[0].strip())
            if assignment:
                ir[assignment[1]] = dict(rhs=assignment[2], line=number, block=block)
        calls = sorted((resource['id'], call['index'], call['nonuniform'])
                       for resource in reflected[sha]['resources'] if resource['type'] == 0 and resource['space'] in (1, 3)
                       for call in resource['handleCalls'])
        _require(calls == sorted((site['resourceId'], site['indexSSA'], site['nonUniform']) for site in shader['selectors']),
                 'Original reflected bindless call coverage changed')
        for site in shader['selectors']:
            _require(site['createHandleSSA'] in ir and ir[site['createHandleSSA']]['line'] == site['createHandleLine'], 'Original createHandle line changed')
        original[sha] = ir
    reflected_families = {FamilyKey(family['root'], family['vs'], family['ps']): family for family in matrix['families']}
    plans = []
    for family in bounds['families']:
        key = FamilyKey(family['root'], family['vs'], family['ps'])
        _require(key in reflected_families and family['family'] == reflected_families[key]['family'] and
                 _HASH.fullmatch(key.root_sha256) is not None and key.root_sha256 in matrix['roots'], 'Original root/family association changed')
        sites, unresolved = [], []
        for stage, sha in (('vs', key.vs_sha256), ('ps', key.ps_sha256)):
            shader = shaders[sha]
            _require(shader['stage'] == stage, 'VS/PS original-family association changed')
            for site in shader['selectors']:
                if site['sourceInput']['type'] != 'cbv':
                    source = site['sourceInput']
                    unresolved.append(f"{stage}:{sha}:{site['createHandleSSA']}:t{source['resource']['register']} "
                                      f"space{source['resource']['space']} field{source['fieldByteOffset']} record={source['recordIndexSSA']}")
                    continue
                sites.append(_compile_site(key, shader, reflected[sha], matrix['roots'][key.root_sha256], site, original[sha]))
        plans.append(FamilyPlan(key, family['family'], tuple(sites),
                                'Actual t5 selector/record inputs are required; b4-only evaluation is unsupported' if unresolved else '', tuple(unresolved)))
    _require(len(plans) == len(reflected_families) == len(bounds['families']) and len({plan.key for plan in plans}) == len(plans),
             'Original-family catalog is incomplete or duplicated')
    return EvidenceCatalog(tuple(plans), hashlib.sha256(bounds_bytes + reflection_bytes).hexdigest())


def _intervals(offsets):
    result = []
    for offset in offsets:
        if result and result[-1].offset + result[-1].count == offset:
            prior = result.pop()
            result.append(SlotInterval(prior.offset, prior.count + 1))
        else:
            result.append(SlotInterval(offset, 1))
    return tuple(result)


def evaluate(plan, snapshots):
    """Evaluate every audited site; any failure publishes no partial result.

    The caller must prove these immutable bytes came from this native source
    occurrence and are the same view inputs used by any later consumer.
    """
    if not isinstance(plan, FamilyPlan):
        raise BindingMismatch('An exact original-family plan is required')
    if not plan.supported:
        raise UnsupportedFamily(plan.unsupported_reason + ': ' + '; '.join(plan.unresolved_inputs))
    if not plan.sites:
        raise EvidenceMismatch('A supported original family cannot omit all audited selector sites')
    supplied = {}
    for snapshot in snapshots:
        if not isinstance(snapshot, OwnedCbvBytes) or not isinstance(snapshot.key, CbvKey) or type(snapshot.data) is not bytes:
            raise BindingMismatch('Only typed immutable owned CBV view bytes are accepted')
        if type(snapshot.key.visibility) is not Visibility or any(type(value) is not int for value in
                (snapshot.key.shader_register, snapshot.key.space, snapshot.key.parameter, snapshot.key.table_offset)):
            raise BindingMismatch('CBV identity fields must have exact typed visibility/integer values')
        if snapshot.key in supplied:
            raise BindingMismatch('Duplicate owned CBV snapshot key')
        supplied[snapshot.key] = snapshot.data
    if set(supplied) != set(plan.required_cbvs):
        raise BindingMismatch('Owned b4 snapshots do not exactly match original root/stage/register/space/slot identities')
    for key, data in supplied.items():
        if len(data) < plan.required_byte_extent(key):
            raise BindingMismatch(f'Short owned {key.visibility.stage} b4 bytes: need {plan.required_byte_extent(key)}, got {len(data)}')
    selections, tables = [], {}
    for site in plan.sites:
        raw, = struct.unpack_from('<I', supplied[site.cbv], site.byte_offset)
        index = raw
        for operation in site.operations:
            index = operation.apply(index)
        if not 0 <= index < site.table.declared_extent:
            raise SelectorOutOfRange(f'Family {plan.number} {site.cbv.visibility.stage} {site.create_handle_ssa} '
                                     f'b4+{site.byte_offset}: index {index} outside declared extent {site.table.declared_extent}')
        offset = site.table.original_table_offset + index
        selections.append(SiteSelection(site, raw, index, offset))
        tables.setdefault(site.table, set()).add(offset)
    table_results = []
    for key, values in sorted(tables.items()):
        offsets = tuple(sorted(values))
        table_results.append(TableSelection(key, offsets, max(offsets) + 1, _intervals(offsets)))
    return SelectorResult(plan.key, tuple(selections), tuple(table_results))


def catalog_report(catalog):
    """Static integration inventory, not a captured native-draw selector result."""
    return dict(
        evidenceSha256=catalog.evidence_sha256,
        families=[dict(
            number=plan.number, root=plan.key.root_sha256, vs=plan.key.vs_sha256, ps=plan.key.ps_sha256,
            supported=plan.supported, unsupportedReason=plan.unsupported_reason,
            unresolvedInputs=list(plan.unresolved_inputs), siteCount=len(plan.sites),
            requiredCbvs=[dict(root=key.root_sha256, visibility=int(key.visibility), register=key.shader_register,
                               space=key.space, parameter=key.parameter, tableOffset=key.table_offset,
                               requiredByteExtent=plan.required_byte_extent(key),
                               dwordOffsets=sorted({site.byte_offset for site in plan.sites if site.cbv == key}))
                          for key in plan.required_cbvs],
        ) for plan in catalog.families],
        suppliedNativeBytes=False, inputContentsFrozen=False, nativeExecutionPermission=False,
        copyPermission=False, captureExtentAdmitted=False, nativeSuppression=0,
    )


if __name__ == '__main__':
    print(json.dumps(catalog_report(load_evidence()), indent=2))
