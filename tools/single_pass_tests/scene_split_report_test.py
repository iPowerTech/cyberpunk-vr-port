import copy
import contextlib
import io
import struct
import sys
import unittest
from unittest.mock import patch

from graph_parent_report import fingerprint, name_hash
from owned_schedule_report_test import BASE, fixture as coupled_fixture
from resource_order_report import graph
from scene_split_report import decode, validate_capture, validate_options, validate_publication
from whole_gbuffer_report import _prefix


LAYOUT = '<II48sQQ32s'


def encode(rows, phases):
    rows = [[row[0], row[1], bytearray(row[2]), row[3], row[4], row[5]] for row in rows]
    by_id = {row[0] & 0xDFFFFFFF: row for row in rows}
    for row in rows:
        struct.pack_into('<4I', row[2], 32, *([0x80000000] * 4))
    edges = []
    for phase, pairs in enumerate(phases):
        level = dict.fromkeys(by_id, 0)
        degree = dict.fromkeys(by_id, 0)
        children = {key: [] for key in by_id}
        for start, end in pairs:
            degree[end] += 1
            children[start].append(end)
        pending = sorted(key for key in degree if not degree[key])
        for start in pending:
            for end in sorted(children[start]):
                level[end] = max(level[end], level[start] + 1)
                degree[end] -= 1
                if not degree[end]:
                    pending.append(end)
        for rank, row in enumerate(sorted(rows, key=lambda row: (level[row[0] & 0xDFFFFFFF], row[1]))):
            row[2][28 + phase] = rank
        for start, end in sorted(pairs):
            index = len(edges)
            handle = index | 0x40000000
            outgoing = struct.unpack_from('<I', by_id[start][2], 40 + phase * 4)[0]
            incoming = struct.unpack_from('<I', by_id[end][2], 32 + phase * 4)[0]
            edges.append(struct.pack('<8I', handle, index, index, phase, start | 0x20000000, end | 0x20000000, outgoing, incoming))
            struct.pack_into('<I', by_id[start][2], 40 + phase * 4, handle)
            struct.pack_into('<I', by_id[end][2], 32 + phase * 4, handle)
    return b''.join(struct.pack(LAYOUT, *row) for row in rows), b''.join(edges)


def fixture(both=False):
    schedule, owned, resources, source_nodes, source_edges, base_nodes, base_edges, _ = coupled_fixture()
    parsed, rows, _ = graph(base_nodes, base_edges, BASE)
    execution = [list(row) for row in rows]
    phases = [{(edge['source'], edge['target']) for edge in parsed['edges'] if edge['phase'] == phase} for phase in (0, 1)]
    sections = []
    for order, selected in enumerate((7, 2) if both else (7,)):
        tail, prefix_object = 10 + order, 0x200000 + order * 0x1000
        original_object = struct.unpack_from('<Q', rows[selected][2], 16)[0]
        execution[selected][2] = bytearray(execution[selected][2])
        struct.pack_into('<Q', execution[selected][2], 16, prefix_object)
        appended = list(rows[selected])
        appended[:2] = [tail, tail]
        appended[2] = bytearray(appended[2])
        struct.pack_into('<I', appended[2], 0, tail)
        struct.pack_into('<Q', appended[2], 16, prefix_object + 0x100)
        execution.append(appended)
        phases = [{(tail if start == selected else start, end) for start, end in pairs} | {(selected, tail)} for pairs in phases]
        sections.append((1 - order, selected, tail, original_object, prefix_object, prefix_object + 0x100))
    execution_nodes, execution_edges = encode(execution, phases)
    actual, executed, _ = graph(execution_nodes, execution_edges, BASE)
    _, _, required, missing = _prefix(actual['nodes'], phases[0], {node['id']: node['phaseOrder'][1] for node in actual['nodes']}, BASE)
    assert not missing
    original = list(struct.iter_unpack(LAYOUT, source_nodes))
    schedule[3] = resources[6] = fingerprint(execution_nodes, execution_edges, BASE)
    schedule[6] = len(execution_edges) // 32
    schedule[7:9] = [sum(a[2][28 + phase] != b[2][28 + phase] for a, b in zip(original, executed)) for phase in (0, 1)]
    schedule[9:12] = [required, required, 0]
    count = len(sections)
    stats = [2 if both else 1, 1, *owned[1:4], 2 if both else 1, 10, 19, 10 + count, 19 + 2 * count,
             *([0] * 5 if both else sections[0][1:]),
             1, 1, 1, required, required, 1, 1, 0, 0]
    selected_handles = {row[1] for row in sections}
    mappings = [(row[0], row[0], 1 if row[0] in selected_handles else 0) for row in rows] + [(row[1], row[2], 2) for row in sections]
    kwargs = dict(
        schedule=schedule, owned_stats=owned, resources=resources, source_nodes=source_nodes, source_edges=source_edges,
        bindings=source_nodes, execution_nodes=execution_nodes, execution_edges=execution_edges, image_base=BASE)
    if both:
        kwargs['sections_raw'] = b''.join(struct.pack('<6Q', *row) for row in sections)
    return [stats, base_nodes, base_edges, b''.join(struct.pack('<3I', *row) for row in mappings)], kwargs


def replace_definition(raw, index, offset, value):
    result = bytearray(raw)
    struct.pack_into('<Q', result, index * 104 + 72 + offset, value)
    return bytes(result)


def refresh_hash(kwargs):
    value = fingerprint(kwargs['execution_nodes'], kwargs['execution_edges'], BASE)
    kwargs['schedule'][3] = kwargs['resources'][6] = value


def publication_fixture(both=False):
    args, kwargs = fixture(both)
    split = decode(*args, **kwargs)
    source = list(struct.iter_unpack(LAYOUT, kwargs['source_nodes']))
    scene_roots = []
    for view, index in ((0, 2), (1, 7)):
        header = bytearray(56)
        header[:32] = source[index][5]
        scene_roots.append(dict(viewIndex=view, rootHandle=index, object=hex(struct.unpack_from('<Q', source[index][2], 16)[0]), rawHeader=header.hex()))
    capture = dict(passed=True, sceneSplitRequested=True, sceneSplitControlCleanup=0, sceneSplitFrontierCleanup=0,
                   ownedExecution=dict(stats=kwargs['owned_stats'], resources=kwargs['resources'], sceneSplit=split,
                       proof=dict(mode='scene-split'), sceneSections=dict(verified=True, consumerId=args[0][2], epoch=args[0][3], pairGeneration=args[0][4], roots=scene_roots)),
                   graphParents=dict(sourceFingerprint=hex(kwargs['resources'][6]), rows=[]), passTargets=dict(groups=[]), records=[])
    specs = ((7, 1, 0x200000, (7,), (5,)), (10, 1, 0x200100, (9,), (9,)))
    if both:
        specs += ((2, 0, 0x201000, (7,), (4,)), (11, 0, 0x201100, (9,), (8,)))
    else:
        specs += ((2, 0, int(scene_roots[0]['object'], 16), (7, 9), (4, 8)),)
    list_count = len(specs)
    slots = []
    for index, (handle, view, obj, stages, counts) in enumerate(specs):
        command, context, reset, position = 0x500000 + index * 0x100, 0x600000 + index * 0x100, 100 + index, index + 1
        capture['records'].append(dict(kind=16, sequence=reset, context=command, qpc=100))
        capture['records'].append(dict(kind=15, context=command, input=0x700000, scope=89, flags0=position, reserved=1, qpc=3000))
        for local, (stage, count) in enumerate(zip(stages, counts)):
            scope, first = 1000 + index * 10 + local, 1000 + index * 100 + local * 20
            parent = dict(scope=scope, context=context, fingerprint=kwargs['resources'][6], object=obj, work=BASE + 0x1EC1D0,
                          name=name_hash('GBuffer_Discard'), handle=handle, ordinal=handle, viewIndex=view, slot=84 + index,
                          status=0, stage=stage, frame=1, side=view)
            capture['graphParents']['rows'].append(parent)
            capture['passTargets']['groups'].append(dict(scope=scope, list=command, frame=1, side=view, stage=stage,
                firstQpc=first, lastQpc=first + 10, draws=count, nativeDraws=count, knownTargets=count, targetVariants=1, complete=True,
                submission=dict(queue=0x700000, batch=89, position=position, serialized=True, generation=reset)))
            for kind in (13, 14):
                capture['records'].append(dict(kind=kind, sequence=scope + kind, scope=scope, context=context, output=command,
                    frame=1, side=view, stage=stage, status=0, flags0=count if kind == 14 else 0, qpc=first + (10 if kind == 14 else 1)))
        slots.append([1, 11, 0, 85 + index, 84 + index, index + 1, 0xB00000 + index * 0x100, command, 0x800000 + index * 0x100, 1, 0,
                      501 + index, 0x900000 + index * 0x100, 1, 0, handle, view, 0x1EC1D0, name_hash('GBuffer_Discard'), 0xA00000 + index * 0x100])
    range_row = [1, 11, 1, BASE + 1, 0x1000, 0x2000, 85, 84 + list_count, 2, 0x3000, 0x4000, 0x5000, 0, list_count, 0, 1, 0, 0,
                 *kwargs['owned_stats'][1:4], kwargs['owned_stats'][6], 9, kwargs['resources'][6]]
    submits = [[1, 3, 1, 0x700000, list_count + 1, 0, 0x123000, 0, 0, (1 << 64) - 1, 0, 0, 0, (1 << 64) - 1, (1 << 64) - 1, (1 << 64) - 1]]
    for index, slot in enumerate(slots):
        submits.append([1, 3, 1, 0x700000, list_count + 1, index + 1, slot[7], 1, 11, index, slot[11], 0, slot[6], slot[4], slot[15], slot[16]])
    frontier = dict(sequence=2, installed=1, state=2, failure=0, boundaryFailure=0,
                    stats=[1, 1, 0, list_count, list_count, list_count, 0, 1, 0, 1, list_count, 1, 0, 0, 0, 0], details=[0] * 16,
                    ranges=[range_row], slots=slots, submits=submits)
    return capture, frontier


class SceneSplitReportTest(unittest.TestCase):
    def test_exact_two_eye_composition_preserves_all_original_nodes(self):
        args, kwargs = fixture(True)
        result = decode(*args, **kwargs)
        self.assertEqual((result['selectedView'], result['sourceNodes'], result['executionNodes']), (2, 10, 12))
        self.assertEqual([row['viewIndex'] for row in result['sections']], [1, 0])
        self.assertEqual(result['mappings'][-2:], [dict(sourceHandle=7, executionHandle=10, section=2),
                                                 dict(sourceHandle=2, executionHandle=11, section=2)])
        self.assertEqual(result['nativeDrawSuppression'], 0)
        self.assertFalse(result['sharedScenePreparationProved'])
        self.assertFalse(result['performanceGainProved'])

    def test_two_eye_stats_and_each_section_word_are_bound(self):
        for index in range(24):
            args, kwargs = fixture(True)
            args[0][index] += 1
            with self.subTest(stats=index), self.assertRaises(ValueError):
                decode(*args, **kwargs)
        for index in range(12):
            args, kwargs = fixture(True)
            values = list(struct.unpack('<12Q', kwargs['sections_raw']))
            values[index] += 8
            kwargs['sections_raw'] = struct.pack('<12Q', *values)
            with self.subTest(section=index), self.assertRaises(ValueError):
                decode(*args, **kwargs)

    def test_two_eye_requires_exact_section_abi_and_order(self):
        args, kwargs = fixture(True)
        valid = kwargs['sections_raw']
        for raw in (None, b'', valid[:-1], valid + bytes(8), list(valid), valid[48:] + valid[:48]):
            kwargs['sections_raw'] = raw
            with self.subTest(raw_type=type(raw).__name__), self.assertRaises(ValueError):
                decode(*args, **kwargs)
        args, kwargs = fixture()
        kwargs['sections_raw'] = valid
        with self.assertRaises(ValueError):
            decode(*args, **kwargs)

    def test_two_eye_all_four_clones_must_remain_distinct(self):
        for first, second in ((4, 5), (4, 10), (4, 11), (5, 10), (5, 11), (10, 11)):
            args, kwargs = fixture(True)
            values = list(struct.unpack('<12Q', kwargs['sections_raw']))
            values[second] = values[first] + 48
            kwargs['sections_raw'] = struct.pack('<12Q', *values)
            with self.subTest(pair=(first, second)), self.assertRaisesRegex(ValueError, 'clone objects alias'):
                decode(*args, **kwargs)

    def test_two_eye_clones_cannot_overlap_either_original(self):
        for clone in (4, 5, 10, 11):
            for original in (3, 9):
                args, kwargs = fixture(True)
                values = list(struct.unpack('<12Q', kwargs['sections_raw']))
                values[clone] = values[original] + 8
                kwargs['sections_raw'] = struct.pack('<12Q', *values)
                with self.subTest(clone=clone, original=original), self.assertRaises(ValueError):
                    decode(*args, **kwargs)

    def test_two_eye_both_original_definitions_are_frozen(self):
        for index, tail in ((7, 10), (2, 11)):
            args, kwargs = fixture(True)
            kwargs['bindings'] = replace_definition(kwargs['bindings'], index, 8, 12345)
            for node in (index, tail):
                kwargs['execution_nodes'] = replace_definition(kwargs['execution_nodes'], node, 8, 12345)
            refresh_hash(kwargs)
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, 'shared definition changed'):
                decode(*args, **kwargs)

    def test_two_eye_each_mapping_is_exact(self):
        for offset in range(0, 12 * 12, 4):
            args, kwargs = fixture(True)
            raw = bytearray(args[3])
            raw[offset] ^= 1
            args[3] = bytes(raw)
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'mapping'):
                decode(*args, **kwargs)

    def test_two_eye_all_four_clone_payloads_are_bound(self):
        for index in (2, 7, 10, 11):
            for offset in (0, 8, 16, 24, 30, 64, 80):
                args, kwargs = fixture(True)
                raw = bytearray(kwargs['execution_nodes'])
                raw[index * 104 + offset] ^= 1
                kwargs['execution_nodes'] = bytes(raw)
                refresh_hash(kwargs)
                with self.subTest(index=index, offset=offset), self.assertRaises(ValueError):
                    decode(*args, **kwargs)

    def test_two_eye_tail_edges_cannot_be_bypassed(self):
        for selected, tail in ((7, 10), (2, 11)):
            args, kwargs = fixture(True)
            parsed, rows, _ = graph(kwargs['execution_nodes'], kwargs['execution_edges'], BASE)
            phases = [{(edge['source'], edge['target']) for edge in parsed['edges'] if edge['phase'] == phase} for phase in (0, 1)]
            outgoing = next(pair for pair in phases[1] if pair[0] == tail)
            phases[1].remove(outgoing)
            phases[1].add((selected, outgoing[1]))
            kwargs['execution_nodes'], kwargs['execution_edges'] = encode(rows, phases)
            refresh_hash(kwargs)
            with self.subTest(selected=selected), self.assertRaisesRegex(ValueError, 'incoming/outgoing phase edges'):
                decode(*args, **kwargs)

    def test_two_eye_unrelated_current_definition_can_refresh(self):
        args, kwargs = fixture(True)
        kwargs['bindings'] = replace_definition(kwargs['bindings'], 0, 24, 12345)
        kwargs['execution_nodes'] = replace_definition(kwargs['execution_nodes'], 0, 24, 12345)
        refresh_hash(kwargs)
        self.assertTrue(decode(*args, **kwargs)['verified'])

    def test_four_native_lists_have_exact_live_slot_tokens(self):
        capture, frontier = publication_fixture(True)
        result = validate_publication(capture, frontier=frontier, require_frontier=True)
        self.assertTrue(result['nativePublicationProved'])
        self.assertEqual([row['section'] for row in result['lists']], ['prefixPeer', 'tailPeer', 'prefixMain', 'tailMain'])
        self.assertEqual([row['viewIndex'] for row in result['lists']], [1, 1, 0, 0])
        self.assertEqual([row['position'] for row in result['lists']], [1, 2, 3, 4])
        self.assertEqual([row['draws'] for row in result['lists']], [5, 9, 4, 8])
        self.assertEqual([row['nativeGeneration'] for row in result['lists']], [501, 502, 503, 504])
        capture['ownedExecution']['sceneSplit']['publication'] = result
        self.assertTrue(validate_capture(capture)['verified'])
        capture['sceneSplitEyes'] = 'both'
        self.assertTrue(validate_capture(capture)['verified'])
        capture['sceneSplitEyes'] = 'one'
        with self.assertRaisesRegex(ValueError, 'requested eyes'):
            validate_capture(capture)

    def test_two_eye_publication_rejects_missing_or_reordered_sections(self):
        for change in ('missing', 'reordered', 'different_object', 'other_view'):
            capture, frontier = publication_fixture(True)
            sections = capture['ownedExecution']['sceneSplit']['sections']
            if change == 'missing':
                sections.pop()
            elif change == 'reordered':
                sections.reverse()
            elif change == 'different_object':
                sections[1]['originalObject'] = hex(0x998000)
            else:
                sections[1]['viewIndex'] = 1
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_two_eye_each_native_list_requires_tokens_and_residency(self):
        for slot in range(4):
            for word in (5, 6, 7, 8, 11, 12, 19):
                capture, frontier = publication_fixture(True)
                frontier['slots'][slot][word] = 0
                with self.subTest(slot=slot, word=word), self.assertRaises(ValueError):
                    validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_two_eye_requires_all_four_draw_scopes_and_exact_submit_positions(self):
        for index in range(4):
            capture, frontier = publication_fixture(True)
            capture['graphParents']['rows'].pop(index)
            with self.subTest(missing=index), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)
            capture, frontier = publication_fixture(True)
            capture['passTargets']['groups'][index]['submission']['position'] = 4 - index
            with self.subTest(position=index), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_exact_one_eye_split_with_all_original_nodes(self):
        args, kwargs = fixture()
        result = decode(*args, **kwargs)
        self.assertEqual(result['mode'], 'scene-split')
        self.assertEqual((result['sourceNodes'], result['executionNodes']), (10, 11))
        self.assertEqual(result['mappings'][-1], dict(sourceHandle=7, executionHandle=10, section=2))
        self.assertEqual(result['baseCoupled']['mode'], 'coupled')
        self.assertEqual(result['nativeDrawSuppression'], 0)
        self.assertFalse(result['wholeSceneSinglePassProved'])
        self.assertFalse(result['performanceGainProved'])

    def test_each_stats_field_is_bound(self):
        for index in range(24):
            args, kwargs = fixture()
            args[0][index] += 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                decode(*args, **kwargs)

    def test_each_schedule_field_is_bound(self):
        for index in range(16):
            args, kwargs = fixture()
            kwargs['schedule'][index] += 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                decode(*args, **kwargs)

    def test_current_unrelated_definition_refresh(self):
        args, kwargs = fixture()
        kwargs['bindings'] = replace_definition(kwargs['bindings'], 0, 24, 12345)
        kwargs['execution_nodes'] = replace_definition(kwargs['execution_nodes'], 0, 24, 12345)
        refresh_hash(kwargs)
        self.assertTrue(decode(*args, **kwargs)['verified'])

    def test_selected_definition_may_not_refresh(self):
        args, kwargs = fixture()
        kwargs['bindings'] = replace_definition(kwargs['bindings'], 7, 8, 12345)
        kwargs['execution_nodes'] = replace_definition(kwargs['execution_nodes'], 7, 8, 12345)
        kwargs['execution_nodes'] = replace_definition(kwargs['execution_nodes'], 10, 8, 12345)
        refresh_hash(kwargs)
        with self.assertRaisesRegex(ValueError, 'shared definition changed'):
            decode(*args, **kwargs)

    def test_unmatched_current_definition_rejected(self):
        args, kwargs = fixture()
        kwargs['bindings'] = replace_definition(kwargs['bindings'], 0, 24, 12345)
        with self.assertRaisesRegex(ValueError, 'unmapped node'):
            decode(*args, **kwargs)

    def test_base_definition_must_still_be_early_source(self):
        args, kwargs = fixture()
        args[1] = replace_definition(args[1], 0, 24, 12345)
        with self.assertRaises(ValueError):
            decode(*args, **kwargs)

    def test_mapping_exact_and_ordered(self):
        for offset in (0, 4, 8, 7 * 12, 7 * 12 + 4, 7 * 12 + 8, 10 * 12, 10 * 12 + 4, 10 * 12 + 8):
            args, kwargs = fixture()
            raw = bytearray(args[3])
            raw[offset] ^= 1
            args[3] = bytes(raw)
            with self.assertRaisesRegex(ValueError, 'mapping'):
                decode(*args, **kwargs)

    def test_expanded_payload_tampering_even_with_fresh_hash(self):
        for index in (0, 7, 10):
            for offset in (0, 8, 16, 24, 30, 64, 80):
                args, kwargs = fixture()
                raw = bytearray(kwargs['execution_nodes'])
                raw[index * 104 + offset] ^= 1
                kwargs['execution_nodes'] = bytes(raw)
                refresh_hash(kwargs)
                with self.subTest(index=index, offset=offset), self.assertRaises(ValueError):
                    decode(*args, **kwargs)

    def test_outgoing_edge_cannot_bypass_tail_even_with_valid_graph(self):
        args, kwargs = fixture()
        parsed, rows, _ = graph(kwargs['execution_nodes'], kwargs['execution_edges'], BASE)
        phases = [{(edge['source'], edge['target']) for edge in parsed['edges'] if edge['phase'] == phase} for phase in (0, 1)]
        self.assertIn((10, 2), phases[1])
        phases[1].remove((10, 2))
        phases[1].add((7, 2))
        kwargs['execution_nodes'], kwargs['execution_edges'] = encode(rows, phases)
        refresh_hash(kwargs)
        with self.assertRaisesRegex(ValueError, 'incoming/outgoing phase edges'):
            decode(*args, **kwargs)

    def test_original_shared_alias_must_also_stay_frozen(self):
        args, kwargs = fixture()
        original_obj = args[0][12]
        for owner, key in ((kwargs, 'source_nodes'), (kwargs, 'bindings'), (kwargs, 'execution_nodes')):
            raw = bytearray(owner[key])
            struct.pack_into('<Q', raw, 2 * 104 + 8 + 16, original_obj)
            owner[key] = bytes(raw)
        raw = bytearray(args[1])
        struct.pack_into('<Q', raw, 2 * 104 + 8 + 16, original_obj)
        args[1] = bytes(raw)
        source_hash = fingerprint(kwargs['source_nodes'], kwargs['source_edges'], BASE)
        kwargs['schedule'][2] = kwargs['owned_stats'][13] = kwargs['resources'][5] = source_hash
        kwargs['schedule'][12:14] = [1, 1]
        refresh_hash(kwargs)
        self.assertTrue(decode(*args, **kwargs)['verified'])
        kwargs['bindings'] = replace_definition(kwargs['bindings'], 2, 8, 12345)
        kwargs['execution_nodes'] = replace_definition(kwargs['execution_nodes'], 2, 8, 12345)
        refresh_hash(kwargs)
        with self.assertRaisesRegex(ValueError, 'shared definition changed'):
            decode(*args, **kwargs)

    def test_raw_abi_and_count_bounds(self):
        for index in (1, 2, 3):
            args, kwargs = fixture()
            args[index] = args[index][:-1]
            with self.assertRaises(ValueError):
                decode(*args, **kwargs)
        for key in ('source_nodes', 'source_edges', 'bindings', 'execution_nodes', 'execution_edges'):
            args, kwargs = fixture()
            kwargs[key] = kwargs[key][:-1]
            with self.assertRaises(ValueError):
                decode(*args, **kwargs)
        for values in ([1] * 23, [1] * 25, [True] + [1] * 23, [1] * 23 + [-1]):
            args, kwargs = fixture()
            args[0] = values
            with self.assertRaises(ValueError):
                decode(*args, **kwargs)

    def test_scene_split_conflicts_are_explicit(self):
        validate_options(True)
        validate_options(False, prepared_current=True)
        for key in ('prepared_current', 'prepared_infix', 'native_export', 'prepared_pair', 'prepared_sequence', 'source_stream'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_options(True, **{key: True})

    def test_launcher_requires_verified_execution_and_restored_control(self):
        capture, frontier = publication_fixture()
        split = capture['ownedExecution']['sceneSplit']
        split['publication'] = validate_publication(capture, frontier=frontier, require_frontier=True)
        self.assertIs(validate_capture(capture), split)
        for key, value in (('passed', False), ('sceneSplitRequested', False), ('sceneSplitControlCleanup', 1), ('sceneSplitFrontierCleanup', 1)):
            bad = copy.deepcopy(capture)
            bad[key] = value
            with self.assertRaises(ValueError):
                validate_capture(bad)

    def test_publication_without_tokens_is_observation_only(self):
        capture, _ = publication_fixture()
        result = validate_publication(capture)
        self.assertTrue(result['observedListPublication'])
        self.assertFalse(result['nativePublicationProved'])
        self.assertEqual([row['draws'] for row in result['lists']], [5, 9, 12])
        with self.assertRaisesRegex(ValueError, 'requires native submission'):
            validate_publication(capture, require_frontier=True)

    def test_three_native_lists_have_exact_live_slot_tokens(self):
        capture, frontier = publication_fixture()
        result = validate_publication(capture, frontier=frontier, require_frontier=True)
        self.assertTrue(result['nativePublicationProved'])
        self.assertEqual([row['position'] for row in result['lists']], [1, 2, 3])
        self.assertEqual([row['resetListLabel'] for row in result['lists']], [100, 101, 102])
        self.assertEqual([row['nativeGeneration'] for row in result['lists']], [501, 502, 503])
        self.assertTrue(all(row['workContext'] != row['nativeContext'] for row in result['lists']))
        self.assertEqual(result['native']['batch'], 3)
        self.assertEqual(result['lists'][0]['observedBatch'], 89)
        self.assertFalse(result['gpuCompletionProved'])

    def test_submit_context_is_native_recording_context_not_work_context(self):
        capture, frontier = publication_fixture()
        result = validate_publication(capture, frontier=frontier, require_frontier=True)
        self.assertNotEqual(result['lists'][0]['workContext'], result['lists'][0]['nativeContext'])
        frontier['submits'][1][12] = result['lists'][0]['workContext']
        with self.assertRaisesRegex(ValueError, 'another source slot'):
            validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_publication_parent_target_scope_join(self):
        for key, value in (('object', 0x99900), ('handle', 99), ('viewIndex', 0), ('status', 1), ('context', 0),
                           ('stage', 8), ('fingerprint', 1), ('work', BASE + 1)):
            capture, frontier = publication_fixture()
            capture['graphParents']['rows'][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)
        for key, value in (('draws', 1), ('nativeDraws', 1), ('knownTargets', 0), ('complete', False),
                           ('targetVariants', 2), ('side', 0), ('scope', 99), ('list', 0x99)):
            capture, frontier = publication_fixture()
            capture['passTargets']['groups'][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_publication_requires_one_ordered_same_batch_and_reset_labels(self):
        for key, value in (('queue', 0x88), ('batch', 90), ('position', 4), ('generation', 100), ('serialized', False)):
            capture, frontier = publication_fixture()
            capture['passTargets']['groups'][1]['submission'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)
        capture, frontier = publication_fixture()
        capture['passTargets']['groups'][-1]['list'] += 8
        with self.assertRaises(ValueError):
            validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_publication_draw_count_comes_from_end_record(self):
        capture, frontier = publication_fixture()
        end = next(row for row in capture['records'] if row.get('kind') == 14)
        end['flags0'] += 1
        with self.assertRaisesRegex(ValueError, 'draw count or context'):
            validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_native_range_must_return_with_exact_owned_graph_identity(self):
        for index, value in ((15, 0), (16, 1), (17, 1), (18, 999), (19, 999), (20, 999), (21, 999), (23, 999)):
            capture, frontier = publication_fixture()
            frontier['ranges'][0][index] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_native_slot_requires_exact_recording_and_residency_set(self):
        for index, value in ((3, 99), (4, 99), (5, 0), (5, 129), (6, 99), (7, 99), (8, 0), (9, 0), (10, 1),
                             (11, 0), (12, 0), (13, 0), (14, 1),
                             (15, 99), (16, 0), (17, 0), (18, 0), (19, 0)):
            capture, frontier = publication_fixture()
            frontier['slots'][0][index] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)

    def test_native_submission_must_match_exact_slot_without_failure(self):
        for index, value in ((3, 99), (5, 0), (6, 99), (7, 2), (9, 2), (10, 1), (11, 1), (12, 1), (13, 1), (14, 1), (15, 0)):
            capture, frontier = publication_fixture()
            frontier['submits'][1][index] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate_publication(capture, frontier=frontier, require_frontier=True)
        for key, value in (('verified', False), ('nativeDrawSuppression', 1), ('performanceGainProved', True)):
            bad = copy.deepcopy(capture)
            bad['ownedExecution']['sceneSplit'][key] = value
            with self.assertRaises(ValueError):
                validate_capture(bad)

    def test_both_command_lines_reject_conflicts_before_process_access(self):
        import read_preparation
        import run_launch_tests
        for mode in (['--scene-split'], ['--scene-split-eyes', 'both']):
            for flag in ('--prepared-current', '--prepared-infix', '--native-export', '--retained-source-stream'):
                with self.subTest(reader=flag, mode=mode), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                    read_preparation.main(['--pid', '1', '--out', 'unused-scene-split.json', *mode, flag])
                self.assertEqual(raised.exception.code, 2)
                with self.subTest(launcher=flag, mode=mode), patch.object(sys, 'argv', ['run_launch_tests.py', '--out', 'unused-scene-split', *mode, flag]), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                    run_launch_tests.main()
                self.assertEqual(raised.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
