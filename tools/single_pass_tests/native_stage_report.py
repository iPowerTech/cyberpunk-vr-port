"""Decode source-bound RenderElements observations, never grant execution rights."""

import struct
from collections import Counter, defaultdict
from collections.abc import Sequence

from graph_parent_report import name_hash


UINT64_MAX = (1 << 64) - 1
MAX_DEFINITIONS = 220
MAX_STAGES = 8192
MAX_GRAPH_NODES = 220
DEFINITION_BYTES = 128
STAGE_BYTES = 48
RENDER_WORK_RVA = 0x23A938
RENDER_VTABLE_RVA = 0x2B39F70
SCENE_WORK_RVA = 0x1EC1D0
SCENE_VTABLE_RVA = 0x2B3A008


def _words(values, count, name):
    if (not isinstance(values, Sequence) or isinstance(values, (str, bytes, bytearray))
            or len(values) != count
            or any(type(value) is not int or not 0 <= value <= UINT64_MAX for value in values)):
        raise ValueError(f'{name} uint64 ABI/count mismatch')
    return tuple(values)


def _blob(value, count, stride, name):
    if not isinstance(value, (bytes, bytearray)) or len(value) != count * stride:
        raise ValueError(f'{name} binary ABI/count mismatch')
    return bytes(value)


def _callback(value, image_base):
    return dict(address=hex(value), rva=hex(value - image_base) if value >= image_base else None)


def decode(stats, definitions_raw, stages_raw, source_nodes, owned_stats, image_base):
    """Require a complete observation bound to the same owned consumer and graph."""
    stats = _words(stats, 8, 'NativeStagesStats')
    owned = _words(owned_stats, 24, 'OwnedExecutionStats')
    if stats[0] != 1 or stats[1] != 1 or stats[7]:
        raise ValueError('Native stages observation was not captured successfully')
    if not all(stats[2:5]) or stats[2:5] != owned[1:4]:
        raise ValueError('Native stages consumer/epoch/pair identity differs')
    if not 0 < stats[5] <= MAX_DEFINITIONS or stats[6] > MAX_STAGES:
        raise ValueError('Native stages observation exceeds its bounded ABI')
    if type(image_base) is not int or not 0 < image_base <= UINT64_MAX - max(RENDER_VTABLE_RVA, SCENE_VTABLE_RVA):
        raise ValueError('Invalid native image base')
    definitions_raw = _blob(definitions_raw, stats[5], DEFINITION_BYTES, 'Native stage definitions')
    stages_raw = _blob(stages_raw, stats[6], STAGE_BYTES, 'Native stage entries')
    if (not isinstance(source_nodes, (bytes, bytearray)) or len(source_nodes) % 104
            or not 0 < len(source_nodes) // 104 <= MAX_GRAPH_NODES
            or len(source_nodes) // 104 != owned[8]):
        raise ValueError('Source graph node ABI/count mismatch')

    graph, normalized_handles = {}, set()
    for handle, order, raw, vtable, work, definition in struct.iter_unpack('<II48sQQ32s', source_nodes):
        normalized = handle & 0xDFFFFFFF
        if (normalized >= 65536 or handle not in (normalized, normalized | 0x20000000)
                or handle in graph or normalized in normalized_handles or order != len(graph)
                or struct.unpack_from('<I', raw, 0)[0] != order
                or struct.unpack_from('<Q', definition, 0)[0] != vtable):
            raise ValueError('Invalid, duplicate or out-of-order source graph node')
        normalized_handles.add(normalized)
        graph[handle] = dict(object=struct.unpack_from('<Q', raw, 16)[0],
                             view=struct.unpack_from('<I', raw, 24)[0], vtable=vtable, work=work,
                             headerVtable=struct.unpack_from('<Q', definition, 0)[0],
                             name=struct.unpack_from('<Q', definition, 24)[0])

    stage_rows = [stages_raw[offset:offset + STAGE_BYTES] for offset in range(0, len(stages_raw), STAGE_BYTES)]
    covered, identities, roots, shared = set(), set(), {}, defaultdict(list)
    definitions, stage_histogram, callback_histogram = [], Counter(), Counter()
    for index, row in enumerate(struct.iter_unpack('<8Q64s', definitions_raw)):
        obj, handle, view, first, count, vtable, work, root_obj, header = row
        if (not obj or obj & 7 or not root_obj or root_obj & 7 or view not in (0, 1)
                or vtable != image_base + RENDER_VTABLE_RVA or work != image_base + RENDER_WORK_RVA):
            raise ValueError('Invalid RenderElements definition identity')
        node = graph.get(handle)
        if (node is None or node['object'] != root_obj or node['view'] != view
                or node['headerVtable'] != node['vtable']
                or (node['vtable'], node['work']) not in (
                    (image_base + SCENE_VTABLE_RVA, image_base + SCENE_WORK_RVA),
                    (image_base + RENDER_VTABLE_RVA, image_base + RENDER_WORK_RVA))):
            raise ValueError('RenderElements root differs from the exact source graph node')
        if node['work'] == image_base + SCENE_WORK_RVA and node['name'] != name_hash('GBuffer_Discard'):
            raise ValueError('RenderElements root is not the GBuffer_Discard frontier')
        if node['work'] == work and obj != root_obj:
            raise ValueError('Direct RenderElements root differs from its observed definition')
        root_identity = (handle, root_obj)
        if view in roots and roots[view] != root_identity:
            raise ValueError('More than one native stage root for the same view')
        roots[view] = root_identity
        identity = (view, handle, obj)
        if identity in identities:
            raise ValueError('Duplicate RenderElements child within one source root')
        identities.add(identity)

        header_vtable = struct.unpack_from('<Q', header, 0)[0]
        gate, before, after, data, capacity, header_count = struct.unpack_from('<4Q2I', header, 24)
        if header_vtable != vtable or header_count != count or count > capacity or capacity > MAX_STAGES:
            raise ValueError('RenderElements raw header differs from its stage definition')
        if not data or data & 7 or data > UINT64_MAX - count * STAGE_BYTES:
            raise ValueError('RenderElements stage storage is missing or unaligned')
        if first > len(stage_rows) or count > len(stage_rows) - first:
            raise ValueError('RenderElements stage range exceeds the captured entries')
        slots = set(range(first, first + count))
        if covered.intersection(slots):
            raise ValueError('RenderElements stage ranges overlap')
        covered.update(slots)
        entries = []
        for local_index, raw in enumerate(stage_rows[first:first + count]):
            callback = struct.unpack_from('<Q', raw, 0)[0]
            view_flags = struct.unpack_from('<I', raw, 8)[0]
            stage = struct.unpack_from('<I', raw, 16)[0]
            filter0, filter1 = struct.unpack_from('<2I', raw, 24)
            prebuilt, partition0, partition1 = struct.unpack_from('<3I', raw, 36)
            entries.append(dict(index=first + local_index, localIndex=local_index, raw=raw.hex(),
                                callback=_callback(callback, image_base), viewFlags=view_flags,
                                mode=raw[12], stage=stage, technique=raw[20], filter=[filter0, filter1],
                                state=raw[32], prebuiltList=prebuilt, partition=[partition0, partition1]))
            stage_histogram[stage] += 1
            callback_histogram[callback] += 1
        stages = [entry['stage'] for entry in entries]
        tail_count = 0
        for stage in reversed(stages):
            if stage != 9:
                break
            tail_count += 1
        definitions.append(dict(index=index, object=hex(obj), nodeHandle=handle, viewIndex=view,
                                rootObject=hex(root_obj), vtableRva=hex(vtable - image_base),
                                workRva=hex(work - image_base), firstStage=first, count=count,
                                rawHeader=header.hex(), gate=_callback(gate, image_base),
                                before=_callback(before, image_base), after=_callback(after, image_base),
                                data=hex(data), capacity=capacity, stages=stages, entries=entries,
                                stage9Count=stages.count(9), tailStage9Count=tail_count,
                                tailFirstStage=first + count - tail_count if tail_count else None))
        shared[obj].append(index)
    if set(roots) != {0, 1}:
        raise ValueError('Native stages observation does not cover both source views')
    if len(covered) != len(stage_rows):
        raise ValueError('Native stages observation has unclaimed stage entries')
    paired = [dict(object=hex(obj), definitions=indices,
                   views=sorted({definitions[index]['viewIndex'] for index in indices}))
              for obj, indices in sorted(shared.items())
              if len({definitions[index]['viewIndex'] for index in indices}) > 1]
    return dict(schema=1, status='captured', consumerId=stats[2], epoch=stats[3], pairGeneration=stats[4],
                definitionCount=stats[5], stageCount=stats[6], definitions=definitions,
                roots=[dict(viewIndex=view, nodeHandle=roots[view][0], object=hex(roots[view][1]))
                       for view in sorted(roots)],
                stages=dict(sorted(stage_histogram.items())),
                callbacks=[dict(**_callback(callback, image_base), count=count)
                           for callback, count in sorted(callback_histogram.items())],
                stage9Count=sum(definition['stage9Count'] for definition in definitions),
                tailStage9Count=sum(definition['tailStage9Count'] for definition in definitions),
                pairedSharedObjects=paired, nativeOwnershipProved=False, childPathProved=False,
                partitionSemanticsProved=False, executionPermitted=False,
                limitation='Source-bound observation only; child paths are captured by the native observer. '
                           'Partition fields do not grant scheduling, resource, or draw suppression permission.')
