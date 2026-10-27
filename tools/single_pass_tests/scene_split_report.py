"""Verify native SceneDriver expansion without claiming single-pass gain."""

import struct

from graph_parent_report import fingerprint, name_hash
from owned_execution_report import _words, decode as decode_owned, decode_schedule
from resource_order_report import graph as parse_graph
from whole_gbuffer_report import _prefix


CONTROL = 'CyberpunkVR_OwnedSceneSplitEnable'
NODE_LAYOUT = '<II48sQQ32s'


def _sections(stats, raw):
    if stats[0] == 1:
        if stats[5] != 1 or raw not in (None, b''):
            raise ValueError('One-eye scene split has an unexpected section ABI')
        return [(1, *stats[10:15])]
    if (stats[0] != 2 or stats[5] != 2 or any(stats[10:15])
            or not isinstance(raw, bytes) or len(raw) != 12 * 8):
        raise ValueError('Two-eye scene split section ABI differs')
    rows = list(struct.iter_unpack('<6Q', raw))
    if [row[0] for row in rows] != [1, 0]:
        raise ValueError('Two-eye scene split sections are not peer then main')
    return rows


def _reported_sections(split, stats):
    if stats[0] == 1:
        return _sections(stats, None)
    rows = split.get('sections')
    if not isinstance(rows, list) or len(rows) != 2:
        raise ValueError('Two-eye scene split has no exact section records')
    values = []
    for row in rows:
        try:
            values.extend((row['viewIndex'], row['prefixHandle'], row['tailHandle'],
                           int(row['originalObject'], 16), int(row['prefixObject'], 16), int(row['tailObject'], 16)))
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError('Two-eye scene split section record differs') from error
    values = _words(values, 12, 'OwnedSceneSplitSections')
    return _sections(stats, struct.pack('<12Q', *values))


def validate_options(enabled, *, prepared_current=False, prepared_infix=False, native_export=False,
                     prepared_pair=False, prepared_sequence=False, source_stream=False):
    if enabled and any((prepared_current, prepared_infix, native_export, prepared_pair, prepared_sequence, source_stream)):
        raise ValueError('--scene-split cannot share prepared consumers, native export, or the two-list retained source stream')


def validate_capture(capture):
    execution = capture.get('ownedExecution', {})
    split = execution.get('sceneSplit', {})
    if (capture.get('passed') is not True or capture.get('sceneSplitRequested') is not True
            or type(capture.get('sceneSplitControlCleanup')) is not int or capture['sceneSplitControlCleanup'] != 0
            or split.get('verified') is not True
            or split.get('mode') != 'scene-split' or execution.get('proof', {}).get('mode') != 'scene-split'
            or split.get('publication', {}).get('nativePublicationProved') is not True
            or capture.get('sceneSplitFrontierCleanup') != 0
            or split.get('nativeDrawSuppression') != 0 or split.get('wholeSceneSinglePassProved') is not False
            or split.get('performanceGainProved') is not False):
        raise ValueError('Native scene split capture or control restoration is incomplete')
    stats = _words(split.get('stats'), 24, 'OwnedSceneSplitStats')
    _reported_sections(split, stats)
    if 'sceneSplitEyes' in capture and capture['sceneSplitEyes'] != ('both' if stats[0] == 2 else 'one'):
        raise ValueError('Native scene split capture differs from the requested eyes')
    owned = _words(execution.get('stats'), 24, 'OwnedExecutionStats')
    if stats[2:5] != owned[1:4] or stats[20:24] != (1, 1, 0, 0):
        raise ValueError('Native scene split capture has a different consumer or incomplete execution')
    return split


def validate_publication(capture, *, frontier=None, require_frontier=False):
    """Join draw scopes to original native lists and optional reset tokens.

    Preparation ResetList labels and submit batches are independent observation
    counters, not native recording generations or frontier batch identifiers.
    """
    execution = capture.get('ownedExecution', {})
    split = execution.get('sceneSplit', {})
    stats = _words(split.get('stats'), 24, 'OwnedSceneSplitStats')
    sections = _reported_sections(split, stats)
    owned = _words(execution.get('stats'), 24, 'OwnedExecutionStats')
    resources = _words(execution.get('resources'), 12, 'OwnedExecutionResources')
    scene = execution.get('sceneSections', {})
    if (split.get('verified') is not True or stats[2:5] != owned[1:4] or stats[20:24] != (1, 1, 0, 0)
            or scene.get('verified') is not True or not isinstance(scene.get('roots'), list)
            or len(scene['roots']) != 2 or tuple(scene.get(key) for key in ('consumerId', 'epoch', 'pairGeneration')) != owned[1:4]):
        raise ValueError('Scene split publication lacks its execution/section identity')
    roots = {root['viewIndex']: root for root in scene['roots']}
    if (set(roots) != {0, 1}
            or any(int(roots[view]['object'], 16) != original or roots[view]['rootHandle'] != prefix
                   for view, prefix, _, original, _, _ in sections)):
        raise ValueError('Scene split publication has different original roots')
    headers = [bytes.fromhex(roots[view]['rawHeader']) for view in (0, 1)]
    if any(len(header) != 56 for header in headers):
        raise ValueError('Scene split publication has invalid original root headers')
    bases = [struct.unpack_from('<Q', header, 0)[0] - 0x2B3A008 for header in headers]
    if bases[0] != bases[1] or bases[0] <= 0:
        raise ValueError('Scene split publication has different image identities')
    definitions = []
    for view, prefix, tail, _, prefix_object, tail_object in sections:
        suffix = ('Peer' if view else 'Main') if stats[0] == 2 else ''
        definitions.extend(((f'prefix{suffix}', view, prefix, prefix_object, {7}, {7}),
                            (f'tail{suffix}', view, tail, tail_object, {8, 9}, {9})))
    if stats[0] == 1:
        definitions.append(('otherEye', 0, roots[0]['rootHandle'], int(roots[0]['object'], 16), {7, 8, 9}, {7, 9}))
    list_count = len(definitions)
    parent_report = capture.get('graphParents', {})
    parents, groups, records = parent_report.get('rows'), capture.get('passTargets', {}).get('groups'), capture.get('records')
    if (not isinstance(parents, list) or not isinstance(groups, list) or not isinstance(records, list)
            or parent_report.get('sourceFingerprint') != hex(resources[6])):
        raise ValueError('Scene split publication lacks complete source-bound draw evidence')
    selected_parents = [parent for parent in parents if parent.get('frame') == 1
                        and parent.get('name') == name_hash('GBuffer_Discard')]
    claimed = set()
    result = []
    for name, view, handle, obj, allowed, required in definitions:
        matched = [parent for parent in selected_parents if parent.get('handle', -1) & 0xDFFFFFFF == handle & 0xDFFFFFFF]
        if not matched or not required <= {parent.get('stage') for parent in matched}:
            raise ValueError('Scene split list is missing required native draw scopes')
        list_identity = None
        scopes = []
        for parent in matched:
            scope = parent.get('scope')
            if (type(scope) is not int or scope <= 0 or scope in claimed or parent.get('status') != 0
                    or parent.get('object') != obj or parent.get('viewIndex') != view or parent.get('side') != view
                    or parent.get('stage') not in allowed or parent.get('work') != bases[0] + 0x1EC1D0
                    or parent.get('fingerprint') != resources[6] or not parent.get('context')):
                raise ValueError('Scene split parent identity/stage differs')
            claimed.add(scope)
            targets = [group for group in groups if group.get('scope') == scope and group.get('frame') == 1]
            if len(targets) != 1:
                raise ValueError('Scene split scope has no unique captured target group')
            group = targets[0]
            count = group.get('draws')
            submit = group.get('submission')
            if (group.get('complete') is not True or type(count) is not int or count <= 0
                    or group.get('nativeDraws') != count or group.get('knownTargets') != count
                    or group.get('targetVariants') != 1 or group.get('side') != view or group.get('stage') != parent['stage']
                    or not isinstance(submit, dict) or submit.get('serialized') is not True
                    or any(type(submit.get(key)) is not int or submit[key] <= 0 for key in ('queue', 'batch', 'generation'))
                    or type(submit.get('position')) is not int or submit['position'] < 0 or not group.get('list')):
                raise ValueError('Scene split target group or submission is incomplete')
            boundaries = {kind: [event for event in records if event.get('kind') == kind and event.get('scope') == scope] for kind in (13, 14)}
            if any(len(value) != 1 for value in boundaries.values()):
                raise ValueError('Scene split scope has no exact begin/end records')
            begin, end = boundaries[13][0], boundaries[14][0]
            expected = (scope, parent['context'], group['list'], 1, view, parent['stage'], 0)
            if (any(tuple(event.get(key) for key in ('scope', 'context', 'output', 'frame', 'side', 'stage', 'status')) != expected for event in (begin, end))
                    or end.get('flags0') != count or end.get('sequence', 0) <= begin.get('sequence', 0)
                    or end.get('qpc') != group.get('lastQpc')):
                raise ValueError('Scene split native scope draw count or context differs')
            reset = [event for event in records if event.get('kind') == 16 and event.get('sequence') == submit['generation']]
            submitted = [event for event in records if event.get('kind') == 15 and event.get('context') == group['list']
                         and event.get('input') == submit['queue'] and event.get('scope') == submit['batch']
                         and event.get('flags0') == submit['position']]
            if (len(reset) != 1 or reset[0].get('context') != group['list'] or reset[0].get('qpc', 0) > group['firstQpc']
                    or len(submitted) != 1 or submitted[0].get('reserved') != 1 or submitted[0].get('qpc', 0) < end['qpc']):
                raise ValueError('Scene split reset label or actual submit record differs')
            identity = (group['list'], parent['context'], parent['slot'], parent['ordinal'],
                        submit['queue'], submit['batch'], submit['position'], submit['generation'])
            if list_identity is not None and identity != list_identity:
                raise ValueError('One scene section unexpectedly spans different lists/recordings')
            list_identity = identity
            scopes.append(dict(scope=scope, stage=parent['stage'], draws=count))
        command, context, slot, ordinal, queue, batch, position, reset_label = list_identity
        result.append(dict(section=name, viewIndex=view, nodeHandle=handle, object=hex(obj), ordinal=ordinal,
                           producerSlot=slot, list=command, workContext=context, queue=queue, observedBatch=batch,
                           position=position, resetListLabel=reset_label, scopes=scopes, draws=sum(row['draws'] for row in scopes)))
    if (claimed != {parent.get('scope') for parent in selected_parents}
            or len({row['list'] for row in result}) != list_count or len({row['resetListLabel'] for row in result}) != list_count
            or len({(row['queue'], row['observedBatch']) for row in result}) != 1
            or [row['position'] for row in result] != list(range(1, list_count + 1))):
        raise ValueError('Scene split did not publish distinct ordered native lists')

    if require_frontier and frontier is None:
        raise ValueError('Scene split requires native submission-frontier token evidence')
    native = None
    if frontier is not None:
        # Re-run the existing bounded decoder; its legacy matching rules remain unchanged.
        from read_preparation import FRONTIER_ROWS, decode_submission_frontier
        blocks = {name: b''.join(struct.pack(f'<{words}Q', *_words(row, words, 'SubmissionFrontierRow')) for row in frontier.get(name, []))
                  for name, _, _, words, _ in FRONTIER_ROWS}
        observed = decode_submission_frontier(frontier.get('sequence'), frontier.get('installed'), frontier.get('state'),
                                              frontier.get('failure'), frontier.get('stats'), frontier.get('details'), blocks)
        if (observed['installed'] != 1 or observed['state'] != 2 or observed['failure']
                or observed['stats'][14:] != [0, 0] or frontier.get('boundaryFailure', 0)):
            raise ValueError('Scene split submission-frontier observer failed or overflowed')
        ranges, slots, submits = (observed[key] for key in ('ranges', 'slots', 'submits'))
        matches = []
        for row in result:
            candidates = []
            for index, slot in enumerate(slots):
                parent = ranges[slot[2]]
                if (parent[18:21] != list(owned[1:4]) or slot[15] & 0xDFFFFFFF != row['nodeHandle'] & 0xDFFFFFFF
                        or slot[16] != row['viewIndex']):
                    continue
                if (parent[15:18] != [1, 0, 0] or parent[21] != owned[6] or parent[23] != resources[6]
                        or slot[4] != row['producerSlot'] or slot[3] != slot[4] + 1 or not 0 < slot[5] <= 128
                        or slot[7] != row['list'] or not slot[6] or not slot[8] or slot[9] not in (1, 2) or slot[10]
                        or slot[13:15] != [1, 0] or not all(slot[key] for key in (11, 12, 19))
                        or slot[17:19] != [0x1EC1D0, name_hash('GBuffer_Discard')]):
                    raise ValueError('Scene split native slot/range identity or reset token differs')
                submitted = [item for item in submits if item[9] == index]
                if (len(submitted) != 1 or submitted[0][7] != 1 or submitted[0][11]
                        or submitted[0][3] != row['queue'] or submitted[0][5] != row['position']):
                    raise ValueError('Scene split native submit did not match its exact slot')
                item = submitted[0]
                # GraphParent.context is the callback work context. Slot.context
                # is a registry-owned recording context; only Submit.context
                # must equal it (checked by decode_submission_frontier above).
                candidates.append(dict(rangeRow=slot[2], slotRow=index, probeEpoch=slot[0], invocation=slot[1],
                                       batch=item[1], count=item[4], nativeContext=slot[6], nativeContextId=slot[5],
                                       nativeGeneration=slot[11], token=slot[12], residencySet=slot[19]))
            if len(candidates) != 1:
                raise ValueError('Scene split native list has no unique qualified publication slot')
            row.update(candidates[0])
            matches.append(candidates[0])
        if (len({(row['probeEpoch'], row['batch'], row['count']) for row in matches}) != 1
                or len({row['rangeRow'] for row in matches}) != 1):
            raise ValueError('Scene split native publication is not one common recording batch/range')
        native = dict(probeEpoch=matches[0]['probeEpoch'], batch=matches[0]['batch'], rangeRow=matches[0]['rangeRow'])
    return dict(verified=True, observedListPublication=True, nativePublicationProved=native is not None,
                lists=result, native=native, nativeDrawSuppression=0, gpuCompletionProved=False,
                limitation=f'{list_count} original native list publications only. ResetList labels are observation sequences, '
                           'not native generations; no GPU-completion or performance proof.')


def _edges(graph, phase):
    return {(row['source'], row['target']) for row in graph['edges'] if row['phase'] == phase}


def _context(graph, base):
    _, _, required, missing = _prefix(graph['nodes'], _edges(graph, 0),
                                      {node['id']: node['phaseOrder'][1] for node in graph['nodes']}, base)
    if missing:
        raise ValueError('Scene split loses native context-prefix CPU readiness')
    return required


def _ranks(graph):
    for phase in (0, 1):
        nodes = graph['nodes']
        levels = {node['id']: 0 for node in nodes}
        degree = dict.fromkeys(levels, 0)
        following = {key: [] for key in levels}
        for start, end in _edges(graph, phase):
            degree[end] += 1
            following[start].append(end)
        pending = sorted(key for key, value in degree.items() if not value)
        for node in pending:
            for end in sorted(following[node]):
                levels[end] = max(levels[end], levels[node] + 1)
                degree[end] -= 1
                if not degree[end]:
                    pending.append(end)
        if len(pending) != len(nodes):
            raise ValueError('Scene split phase contains a cycle')
        expected = sorted(nodes, key=lambda node: (levels[node['id']], node['order']))
        if any(node['phaseOrder'][phase] != rank for rank, node in enumerate(expected)):
            raise ValueError('Scene split native phase ranks differ')


def _pointer(value, size=56):
    if not value or value & 7 or value > (1 << 64) - 1 - size:
        raise ValueError('Scene split object pointer is missing, unaligned, or overflowing')


def decode(values, base_nodes, base_edges, mapping_raw, *, schedule, owned_stats, resources,
           source_nodes, source_edges, bindings, execution_nodes, execution_edges, image_base, sections_raw=None):
    stats = _words(values, 24, 'OwnedSceneSplitStats')
    owned = _words(owned_stats, 24, 'OwnedExecutionStats')
    resource = _words(resources, 12, 'OwnedExecutionResources')
    schedule = _words(schedule, 16, 'OwnedExecutionSchedule')
    decode_owned(owned, resource)
    sections = _sections(stats, sections_raw)
    section_count = len(sections)
    if (stats[1] != 1 or stats[2:5] != owned[1:4]
            or stats[15:18] != (1, 1, 1) or stats[20:24] != (1, 1, 0, 0)):
        raise ValueError('Scene split execution receipt is incomplete, failed, or suppresses native draws')
    if (not 0 < stats[6] <= 220 - section_count or stats[8] != stats[6] + section_count or stats[7] > 65536 - 2 * section_count
            or stats[9] != stats[7] + 2 * section_count or stats[6] != owned[8]
            or len(base_nodes) != stats[6] * 104 or len(base_edges) != stats[7] * 32
            or len(execution_nodes) != stats[8] * 104 or len(execution_edges) != stats[9] * 32
            or not isinstance(mapping_raw, bytes) or len(mapping_raw) != stats[8] * 12
            or len(bindings) != len(source_nodes) or len(source_nodes) != stats[6] * 104):
        raise ValueError('Scene split graph/mapping ABI/count mismatch')
    if type(image_base) is not int or not 0 < image_base <= (1 << 64) - 1 - 0x3438A28:
        raise ValueError('Invalid scene split image base')
    source, original, _ = parse_graph(source_nodes, source_edges, image_base)
    base, early, _ = parse_graph(base_nodes, base_edges, image_base)
    actual, executed, _ = parse_graph(execution_nodes, execution_edges, image_base)
    _, current, _ = parse_graph(bindings, source_edges, image_base)
    _ranks(base)
    _ranks(actual)
    source_hash = fingerprint(source_nodes, source_edges, image_base)
    actual_hash = fingerprint(execution_nodes, execution_edges, image_base)
    if (schedule[:4] != (2, owned[1], source_hash, actual_hash) or schedule[4:7] != (stats[6], len(source['edges']), stats[9])
            or schedule[14:] != (1, 1) or resource[5:7] != (source_hash, actual_hash)
            or owned[9] != len(source['edges'])):
        raise ValueError('Scene split schedule/source/resource fingerprints differ')
    existing = {node['id'] for node in base['nodes']}
    selections = []
    for view, prefix_handle, tail_handle, original_object, prefix_object, tail_object in sections:
        selected = [node for node in base['nodes'] if node['view'] == view and int(node['work'], 16) == image_base + 0x1EC1D0
                    and int(node['vtable'], 16) == image_base + 0x2B3A008
                    and node['definitionWords'][6] | node['definitionWords'][7] << 32 == name_hash('GBuffer_Discard')]
        if len(selected) != 1:
            raise ValueError('Scene split has no unique original discard root for its eye')
        selected = selected[0]
        tail_id = 0
        while tail_id in existing:
            tail_id += 1
        existing.add(tail_id)
        if (prefix_handle, tail_handle, original_object) != (selected['handle'], tail_id | (selected['handle'] & 0x20000000), int(selected['object'], 16)):
            raise ValueError('Scene split selected source/prefix/tail identity differs')
        selections.append(dict(node=selected, tailId=tail_id, tailHandle=tail_handle, prefixObject=prefix_object, tailObject=tail_object))
    originals = {row[3] for row in sections}
    clones = [pointer for row in sections for pointer in row[4:6]]
    for pointer in [*originals, *clones]:
        _pointer(pointer)
    if (any(clone in originals for clone in clones)
            or any(abs(left - right) < 56 for index, left in enumerate(clones) for right in clones[index + 1:])):
        raise ValueError('Scene split clone objects alias')
    for node in source['nodes']:
        obj = int(node['object'], 16)
        size = 56 if int(node['work'], 16) == image_base + 0x1EC1D0 else 32
        if obj and (obj > (1 << 64) - 1 - size or any(clone < obj + size and obj < clone + 56 for clone in clones)):
            raise ValueError('Scene split clone overlaps an original definition')

    expected_mapping, refreshed_base = [], []
    by_ordinal = {row['node']['order']: row for row in selections}
    for index, (old, before, now) in enumerate(zip(original, early, current)):
        if (old[:2] != before[:2] or old[2][:28] != before[2][:28] or old[2][30:32] != before[2][30:32]
                or old[3:] != before[3:] or old[:5] != now[:5]):
            raise ValueError('Base coupled graph or current source changed native node identity')
        aliases_selected = struct.unpack_from('<Q', old[2], 16)[0] in originals
        if aliases_selected and now[5] != before[5]:
            raise ValueError('Scene split original shared definition changed after cloning')
        selection = by_ordinal.get(index)
        expected_mapping.append((old[0], before[0], 1 if selection else 0))
        refreshed_base.append(struct.pack(NODE_LAYOUT, *before[:5], now[5]))
        expected_bytes = bytearray(before[2])
        if selection:
            struct.pack_into('<Q', expected_bytes, 16, selection['prefixObject'])
        row = executed[index]
        if (row[:2] != before[:2] or row[2][:28] != expected_bytes[:28] or row[2][30:32] != expected_bytes[30:32]
                or row[3:5] != before[3:5] or row[5] != (before[5] if selection else now[5])):
            raise ValueError('Scene split execution changed an unmapped node or definition')
    expected_mapping.extend((row['node']['handle'], row['tailHandle'], 2) for row in selections)
    if list(struct.iter_unpack('<3I', mapping_raw)) != expected_mapping:
        raise ValueError('Scene split source-to-execution mapping is not exact')
    for index, selection in enumerate(selections, stats[6]):
        tail = executed[index]
        original_row = early[selection['node']['order']]
        expected_bytes = bytearray(original_row[2])
        struct.pack_into('<I', expected_bytes, 0, index)
        struct.pack_into('<Q', expected_bytes, 16, selection['tailObject'])
        if (tail[:2] != (selection['tailHandle'], index) or tail[2][:28] != expected_bytes[:28]
                or tail[2][30:32] != expected_bytes[30:32] or tail[3:] != original_row[3:]):
            raise ValueError('Scene split appended tail changed source metadata')
    for phase in (0, 1):
        expected = _edges(base, phase)
        for selection in selections:
            prefix_id, tail_id = selection['node']['id'], selection['tailId']
            expected = {(tail_id if start == prefix_id else start, end) for start, end in expected}
            expected.add((prefix_id, tail_id))
        if _edges(actual, phase) != expected:
            raise ValueError('Scene split incoming/outgoing phase edges are not exact')
    changed = tuple(sum(old[2][28 + phase] != row[2][28 + phase] for old, row in zip(original, executed)) for phase in (0, 1))
    required = _context(actual, image_base)
    if schedule[7:12] != (*changed, required, required, 0) or stats[18:20] != (required, required):
        raise ValueError('Scene split rank/context-prefix proof differs')

    # Validate the unchanged coupled transformation separately from the split.
    # Only identity-node definition bytes are refreshed from the source receipt.
    refreshed_base = b''.join(refreshed_base)
    base_hash = fingerprint(refreshed_base, base_edges, image_base)
    base_schedule = list(schedule)
    base_schedule[3], base_schedule[6] = base_hash, stats[7]
    base_schedule[7:9] = [sum(old[2][28 + phase] != row[2][28 + phase] for old, row in zip(original, early)) for phase in (0, 1)]
    base_required = _context(base, image_base)
    base_schedule[9:12] = [base_required, base_required, 0]
    base_resources = list(resource)
    base_resources[6] = base_hash
    coupled = decode_schedule(base_schedule, list(owned), base_resources, source_nodes, source_edges,
                              refreshed_base, base_edges, image_base, bindings=bindings, coupled=True)
    result = dict(mode='scene-split', verified=True, stats=list(stats), consumerId=stats[2], epoch=stats[3], pairGeneration=stats[4],
                selectedView=stats[5], sourceNodes=stats[6], executionNodes=stats[8], baseCoupledEdges=stats[7], executionEdges=stats[9],
                mappings=[dict(sourceHandle=a, executionHandle=b, section=c) for a, b, c in expected_mapping],
                nativeContextPairs=required, baseCoupled=coupled, nativeDrawSuppression=0,
                sharedScenePreparationProved=False, wholeSceneSinglePassProved=False, performanceGainProved=False)
    if stats[0] == 1:
        result.update(prefixHandle=stats[10], tailHandle=stats[11], originalObject=hex(stats[12]),
                      prefixObject=hex(stats[13]), tailObject=hex(stats[14]))
    else:
        result['sections'] = [dict(viewIndex=view, prefixHandle=prefix, tailHandle=tail, originalObject=hex(original),
                                  prefixObject=hex(prefix_object), tailObject=hex(tail_object))
                              for view, prefix, tail, original, prefix_object, tail_object in sections]
    return result
