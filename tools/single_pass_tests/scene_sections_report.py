"""Validate observed SceneDriver sections without granting clone/execution rights."""

import struct

from graph_parent_report import name_hash
from native_stage_report import _blob, _words


UINT64_MAX = (1 << 64) - 1
ROOT_BYTES, CHILD_BYTES = 112, 80
CHILD_COUNT, PREFIX_COUNT, TAIL_COUNT = 11, 6, 5
MAX_NODES, MAX_ARRAY_CAPACITY = 220, 8192
VTABLE_RVA, WORK_RVA = 0x2B3A008, 0x1EC1D0
SETUP = (0x312E1B0, 0x1EDD64, 0x983FD8, 32, 7)
BIND = (0x312DDF0, 0x20C3AC, 0x983FD8, 32, 0x200010)
RENDER = (0x2B39F70, 0x23A938, 0x986600, 32, None)
SPECIAL = (0x312DB50, 0x77D214, 0x986798, 24, None)
UNBIND = (0x312DE80, 0x20C02C, 0x983FD8, 32, 0x200010)
END = (0x312E210, 0xA2BABC, 0x983FD8, 32, 7)
PATTERN = (SETUP, BIND, RENDER, SPECIAL, UNBIND, END, SETUP, BIND, RENDER, UNBIND, END)


def _address(value, size, label):
    if not value or value & 7 or value > UINT64_MAX - size:
        raise ValueError(f'Invalid {label} address/range')


def _hex_blob(value, size, label):
    if not isinstance(value, str) or len(value) != size * 2:
        raise ValueError(f'{label} raw hex ABI mismatch')
    try:
        raw = bytes.fromhex(value)
    except ValueError as error:
        raise ValueError(f'{label} raw hex ABI mismatch') from error
    if len(raw) != size:
        raise ValueError(f'{label} raw hex ABI mismatch')
    return raw


def _pointer_hex(value, label):
    if not isinstance(value, str):
        raise ValueError(f'Invalid {label} identity')
    try:
        result = int(value, 16)
    except ValueError as error:
        raise ValueError(f'Invalid {label} identity') from error
    if not 0 <= result <= UINT64_MAX:
        raise ValueError(f'Invalid {label} identity')
    return result


def _graph(raw, owned):
    if (not isinstance(raw, (bytes, bytearray)) or len(raw) % 104
            or not 0 < len(raw) // 104 <= MAX_NODES or len(raw) // 104 != owned[8]):
        raise ValueError('Scene section source graph ABI/count mismatch')
    nodes, seen = {}, set()
    for handle, order, payload, vtable, work, definition in struct.iter_unpack('<II48sQQ32s', raw):
        node_id = handle & 0xDFFFFFFF
        if (node_id >= 65536 or handle not in (node_id, node_id | 0x20000000)
                or node_id in seen or order != len(nodes) or struct.unpack_from('<I', payload, 0)[0] != order
                or struct.unpack_from('<Q', definition, 0)[0] != vtable):
            raise ValueError('Invalid scene section source graph identity')
        seen.add(node_id)
        nodes[handle] = dict(object=struct.unpack_from('<Q', payload, 16)[0],
                             view=struct.unpack_from('<I', payload, 24)[0], vtable=vtable, work=work,
                             definition=definition)
    return nodes


def _render_rows(report, identity):
    if (not isinstance(report, dict) or report.get('schema') != 1 or report.get('status') != 'captured'
            or any(type(report.get(key)) is not int for key in ('schema', 'consumerId', 'epoch', 'pairGeneration', 'definitionCount', 'stageCount'))
            or tuple(report.get(key) for key in ('consumerId', 'epoch', 'pairGeneration')) != identity
            or report.get('definitionCount') != 4 or report.get('stageCount') != 12
            or not isinstance(report.get('definitions'), list) or len(report['definitions']) != 4):
        raise ValueError('Scene sections have a different native stages observation')
    rows, covered = {}, set()
    for index, row in enumerate(report['definitions']):
        if not isinstance(row, dict):
            raise ValueError('Invalid native stages definition')
        view, handle = row.get('viewIndex'), row.get('nodeHandle')
        if type(view) is not int or view not in (0, 1) or type(handle) is not int or not 0 <= handle < 2**32:
            raise ValueError('Invalid native stages root identity')
        first, count = row.get('firstStage'), row.get('count')
        if (type(row.get('index')) is not int or row['index'] != index or type(first) is not int
                or type(count) is not int or count not in (2, 4) or not 0 <= first <= 12 - count):
            raise ValueError('Invalid native stages entry range')
        indices = set(range(first, first + count))
        if covered.intersection(indices):
            raise ValueError('Native stages entry ranges overlap')
        covered.update(indices)
        key = (view, handle, _pointer_hex(row.get('rootObject'), 'native stages root'),
               _pointer_hex(row.get('object'), 'native stages child'))
        if key in rows:
            raise ValueError('Duplicate native stages child observation')
        rows[key] = row
    if len(covered) != 12:
        raise ValueError('Native stages entry coverage is incomplete')
    return rows


def _render(row, child_prefix, base, late):
    count = 4 if late else 2
    if (row.get('count') != count or row.get('vtableRva') != hex(RENDER[0])
            or row.get('workRva') != hex(RENDER[1])
            or not isinstance(row.get('entries'), list) or len(row['entries']) != count):
        raise ValueError('RenderElements section identity/count differs')
    header = _hex_blob(row.get('rawHeader'), 64, 'RenderElements header')
    gate, before, after, data, capacity, captured_count = struct.unpack_from('<4Q2I', header, 24)
    if (header[:32] != child_prefix or struct.unpack_from('<Q', header, 0)[0] != base + RENDER[0]
            or gate != base + (0x1134C40 if late else 0x1D41BE0) or before or after
            or captured_count != count or not count <= capacity <= MAX_ARRAY_CAPACITY):
        raise ValueError('RenderElements section header/gate differs')
    _address(data, count * 48, 'RenderElements stages')
    stages = []
    for index, entry in enumerate(row['entries']):
        if not isinstance(entry, dict):
            raise ValueError('Invalid RenderElements stage entry')
        if (type(entry.get('index')) is not int or entry['index'] != row['firstStage'] + index
                or type(entry.get('localIndex')) is not int or entry['localIndex'] != index):
            raise ValueError('RenderElements stage entry index differs')
        raw = _hex_blob(entry.get('raw'), 48, 'RenderElements stage')
        expected_stage = (9 if index < 2 else 8) if late else 7
        if (struct.unpack_from('<Q', raw, 0)[0] or struct.unpack_from('<I', raw, 8)[0] != (17 if index % 2 else 33)
                or raw[12] != index % 2 or struct.unpack_from('<I', raw, 16)[0] != expected_stage
                or raw[20] != (0 if index % 2 else 2) or struct.unpack_from('<I', raw, 24)[0]
                or struct.unpack_from('<I', raw, 28)[0] or raw[32]
                or struct.unpack_from('<I', raw, 36)[0] != 0xFFFFFFFF
                or struct.unpack_from('<I', raw, 40)[0] or struct.unpack_from('<I', raw, 44)[0]):
            raise ValueError('RenderElements section stage pattern differs')
        stages.append(expected_stage)
    if row.get('stages') != stages:
        raise ValueError('RenderElements displayed stages differ from raw entries')
    return dict(object=row['object'], count=count, stages=stages, gateRva=hex(gate - base),
                rawHeader=header.hex(), rawEntries=[entry['raw'] for entry in row['entries']])


def decode(stats, roots_raw, children_raw, source_nodes, owned_stats, image_base, native_stages_report):
    """Require the exact two-view, eleven-child source observation and stage join."""
    stats = _words(stats, 8, 'SceneSectionsStats')
    owned = _words(owned_stats, 24, 'OwnedExecutionStats')
    if stats[:2] != (1, 1) or stats[7] or stats[5:7] != (2, 22):
        raise ValueError('Scene sections observation was not captured completely')
    if not all(stats[2:5]) or stats[2:5] != owned[1:4]:
        raise ValueError('Scene sections consumer/epoch/pair identity differs')
    if type(image_base) is not int or not 0 < image_base <= UINT64_MAX - 0x3438A28:
        raise ValueError('Invalid scene sections image base')
    roots_raw = _blob(roots_raw, 2, ROOT_BYTES, 'Scene section roots')
    children_raw = _blob(children_raw, 22, CHILD_BYTES, 'Scene section children')
    graph = _graph(source_nodes, owned)
    render_rows = _render_rows(native_stages_report, stats[2:5])
    children = list(struct.iter_unpack('<5Q2I32s', children_raw))
    roots, covered, views, joined = [], set(), set(), set()
    observed_children, controls, observed_roots = {}, {}, {}
    for index, row in enumerate(struct.iter_unpack('<7Q56s', roots_raw)):
        obj, handle, view, first, count, split, reserved, header = row
        _address(obj, 56, 'SceneDriver root')
        if view not in (0, 1) or view in views or reserved or count != CHILD_COUNT or split != PREFIX_COUNT:
            raise ValueError('Invalid SceneDriver root/section identity')
        views.add(view)
        node = graph.get(handle)
        if (node is None or node['object'] != obj or node['view'] != view
                or node['vtable'] != image_base + VTABLE_RVA or node['work'] != image_base + WORK_RVA
                or header[:32] != node['definition']
                or struct.unpack_from('<Q', header, 0)[0] != node['vtable']
                or struct.unpack_from('<Q', header, 24)[0] != name_hash('GBuffer_Discard')
                or struct.unpack_from('<I', header, 32)[0] != 1):
            raise ValueError('SceneDriver root differs from exact source graph/header')
        data, capacity, native_count = struct.unpack_from('<Q2I', header, 40)
        if not native_count == count <= capacity <= MAX_ARRAY_CAPACITY:
            raise ValueError('SceneDriver child array count/capacity differs')
        _address(data, count * 16, 'SceneDriver child array')
        if obj in observed_roots and observed_roots[obj] != header:
            raise ValueError('Shared SceneDriver root observation changed')
        observed_roots[obj] = header
        if first > len(children) or count > len(children) - first:
            raise ValueError('SceneDriver child range exceeds captured rows')
        indices = set(range(first, first + count))
        if covered.intersection(indices):
            raise ValueError('SceneDriver child ranges overlap')
        covered.update(indices)
        decoded, render = [], []
        for slot, child in enumerate(children[first:first + count]):
            child_obj, control, vtable, work, destructor, recorded_slot, captured_bytes, prefix = child
            expected_vtable, expected_work, expected_destructor, expected_bytes, parameter = PATTERN[slot]
            _address(child_obj, expected_bytes, 'SceneDriver child')
            _address(control, 8, 'SceneDriver child control block')
            if (recorded_slot != slot or captured_bytes != expected_bytes
                    or (vtable, work, destructor) != tuple(image_base + value for value in (expected_vtable, expected_work, expected_destructor))
                    or struct.unpack_from('<Q', prefix, 0)[0] != vtable
                    or (parameter is not None and struct.unpack_from('<I', prefix, 24)[0] != parameter)
                    or any(prefix[captured_bytes:])):
                raise ValueError('SceneDriver child profile/parameter/padding differs')
            observation = (control, vtable, work, destructor, captured_bytes, prefix)
            if ((child_obj in observed_children and observed_children[child_obj] != observation)
                    or (control in controls and controls[control] != child_obj)):
                raise ValueError('Shared SceneDriver child/control observation changed')
            observed_children[child_obj] = observation
            controls[control] = child_obj
            decoded.append(dict(index=first + slot, slot=slot, object=hex(child_obj), controlBlock=hex(control),
                                vtableRva=hex(vtable - image_base), workRva=hex(work - image_base),
                                destructorRva=hex(destructor - image_base), capturedBytes=captured_bytes,
                                rawPrefix=prefix.hex(), raw=children_raw[(first + slot) * CHILD_BYTES:(first + slot + 1) * CHILD_BYTES].hex()))
            if slot in (2, 8):
                key = (view, handle, obj, child_obj)
                if key not in render_rows or key in joined:
                    raise ValueError('SceneDriver RenderElements has no unique native stages join')
                joined.add(key)
                render.append(dict(slot=slot, **_render(render_rows[key], prefix, image_base, slot == 8)))
        roots.append(dict(index=index, object=hex(obj), rootHandle=handle, viewIndex=view, firstChild=first,
                          childCount=count, splitIndex=split, prefixCount=PREFIX_COUNT, tailCount=TAIL_COUNT,
                          rawHeader=header.hex(), raw=roots_raw[index * ROOT_BYTES:(index + 1) * ROOT_BYTES].hex(),
                          data=hex(data), capacity=capacity, children=decoded, renderElements=render))
    if views != {0, 1} or len(covered) != len(children) or joined != set(render_rows):
        raise ValueError('Scene sections source views/children/stages coverage is incomplete')
    return dict(schema=1, status='captured', consumerId=stats[2], epoch=stats[3], pairGeneration=stats[4],
                rootCount=2, childCount=22, prefixCount=PREFIX_COUNT, tailCount=TAIL_COUNT, roots=roots,
                observationVerified=True, nativeOwnershipProved=False, clonedDefinitionsProved=False,
                executionPermitted=False,
                limitation='Exact source-bound section observation only; no clone ownership, scheduling change, '
                           'resource lifetime, or draw suppression permission.')
