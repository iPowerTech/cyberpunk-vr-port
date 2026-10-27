import copy
import struct
import unittest

from scene_access_report import BLOCKS, PREFIX, U32, U64, decode, read_report, validate
from scene_split_report import validate_publication
from scene_split_report_test import publication_fixture


def event(sequence, recording, child, kind, resource=U32, *, offset=0, size=U64, flags=0, tag=0, internal=0):
    return [sequence, offset, size, 0, recording, child, kind, resource, U32, U32, 0, 0, flags, tag, internal, 0]


def fixture():
    capture, frontier = publication_fixture(True)
    publication = validate_publication(capture, frontier=frontier, require_frontier=True)
    owned = capture['ownedExecution']['stats']
    identities = [owned[1], owned[2], owned[3], owned[6], owned[4], owned[5]]
    children, recordings = [], []
    for index, position in enumerate((1, 2)):
        row = publication['lists'][position]
        children.append([*identities, int(row['object'], 16), 0xC00000+index*0x100, 0xD00000, 0xE00000,
                         row['workContext'], 101+index, 0, row['nodeHandle'], row['viewIndex'], 2-index,
                         6 if not index else 0, 77, 0, 0, 0])
        recordings.append([row['list'], index+1, 1, 1, 0, row['nativeGeneration'], row['token'], row['nativeContext'],
                           0xF00000, row['nodeHandle'], row['viewIndex'], *owned[1:4], index*3, 3])
    resources = [[0x110000, 21, 1, 0, 0, 0, 1, 4096, 1, 1, 1, 0, 4, 1, 1, 0],
                 [0x120000, 0, 0, 0, 0, 0, 3, 128, 128, 1, 1, 28, 1, 0, 0, 0]]
    events = [event(1, 0, 0, 1, 0, offset=128, size=256, tag=4), event(2, 0, 0, 3, 0), event(3, 0, 0, 6, flags=1),
              event(4, 1, 1, 2, 0, offset=256, size=128, tag=4), event(5, 1, 1, 6), event(6, 1, 1, 7, tag=8)]
    stats = [1, 0, 1, 7, 2, 2, 6, 2, 2, 2, 0, 1, 2, 0, 0, 0]
    return decode(stats, recordings, resources, events, children), publication, owned


class SceneAccessReportTest(unittest.TestCase):
    def test_exact_two_native_recordings_are_observation_only(self):
        report, publication, owned = fixture()
        result = validate(report, publication, owned_stats=owned)
        self.assertTrue(result['verified'])
        for name in ('accessCoverageComplete', 'nativeExecutionPermission', 'reorderPermission', 'performanceGainProved'):
            self.assertFalse(result[name])
        self.assertEqual([row['section'] for row in result['childSummary']], ['tailPeer', 'prefixMain'])
        self.assertEqual([row['observedShaders'] for row in result['childSummary']], [1, 1])
        self.assertEqual([row['observedCopyReadOperands'] for row in result['childSummary']], [1, 0])
        self.assertEqual([row['observedCopyWriteOperands'] for row in result['childSummary']], [0, 1])
        self.assertEqual(result['sameResourcePotentialOverlaps'][0]['hazards'],
                         [dict(peer='read', main='write', extent='observed-byte-overlap')])

    def test_unknown_ranges_are_not_declared_whole_resource(self):
        report, publication, owned = fixture()
        report['events'][0][2] = U64
        result = validate(report, publication, owned_stats=owned)
        self.assertEqual(result['sameResourcePotentialOverlaps'][0]['hazards'][0]['extent'], 'unknown-range')
        self.assertFalse(result['accessCoverageComplete'])

    def test_disjoint_ranges_and_read_read_do_not_report_overlap(self):
        for changes in ('disjoint', 'read-read', 'zero'):
            report, publication, owned = fixture()
            if changes == 'disjoint':report['events'][3][1] = 512
            elif changes == 'read-read':report['events'][3][6] = 1
            else:report['events'][3][2] = 0
            self.assertEqual(validate(report, publication, owned_stats=owned)['sameResourcePotentialOverlaps'], [])

    def test_cumulative_counts_may_include_retired_unselected_recordings(self):
        report, publication, owned = fixture()
        report['stats'][9] += 10
        report['stats'][10] += 10
        report['stats'][12] += 10
        self.assertTrue(validate(report, publication, owned_stats=owned)['verified'])
        for field in (9, 11, 12):
            bad = copy.deepcopy(report)
            bad['stats'][field] = 0
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate(bad, publication, owned_stats=owned)

    def test_unattributed_and_internal_events_remain_incomplete(self):
        for internal in (0, 1):
            report, publication, owned = fixture()
            report['events'][1][5] = U32
            report['events'][1][14] = internal
            report['stats'][10] = 1-internal
            result = validate(report, publication, owned_stats=owned)
            self.assertFalse(result['accessCoverageComplete'])
            if not internal:
                report['stats'][10] = 0
                with self.assertRaises(ValueError):validate(report, publication, owned_stats=owned)

    def test_each_native_identity_field_is_joined(self):
        for index in (0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15):
            report, publication, owned = fixture()
            report['recordings'][0][index] += 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate(report, publication, owned_stats=owned)
        report, publication, owned = fixture()
        report['recordings'][0][8] = 0
        with self.assertRaises(ValueError):validate(report, publication, owned_stats=owned)

    def test_selected_registry_equality_is_consistency_not_independent_proof(self):
        for index in (0, 1):
            report, publication, owned = fixture()
            report['recordings'][index][8] += 0x1000
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, 'different native registries'):
                validate(report, publication, owned_stats=owned)
        report, publication, owned = fixture()
        for row in report['recordings']:row[8] += 0x1000
        result = validate(report, publication, owned_stats=owned)
        self.assertTrue(result['verified'])
        for name in ('accessCoverageComplete', 'nativeExecutionPermission', 'reorderPermission', 'performanceGainProved'):
            self.assertFalse(result[name])

    def test_selected_records_must_be_exactly_peer_tail_and_main_prefix(self):
        for position in (0, 3):
            report, publication, owned = fixture()
            report['recordings'][0][0] = publication['lists'][position]['list']
            with self.assertRaises(ValueError):validate(report, publication, owned_stats=owned)

    def test_each_child_graph_root_and_section_is_joined(self):
        for index in (*range(7), 10, 13, 14, 15, 19, 20):
            report, publication, owned = fixture()
            report['children'][0][index] += 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate(report, publication, owned_stats=owned)
        for child, slot in ((0, 5), (0, 11), (1, 6)):
            report, publication, owned = fixture()
            report['children'][child][16] = slot
            with self.subTest(child=child, slot=slot), self.assertRaises(ValueError):
                validate(report, publication, owned_stats=owned)
        for index in (7, 8, 9, 11, 17):
            report, publication, owned = fixture()
            report['children'][0][index] = 0
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate(report, publication, owned_stats=owned)

    def test_event_indices_and_sequence_are_strict(self):
        for index, value in ((0, 0), (3, 1), (4, 1), (5, 2), (6, 0), (7, 2), (8, 0), (14, 2), (15, 1), (1, 4000), (2, 4096)):
            report, publication, owned = fixture()
            report['events'][0][index] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate(report, publication, owned_stats=owned)
        report, publication, owned = fixture()
        report['events'][3][0] = report['events'][0][0]
        with self.assertRaises(ValueError):validate(report, publication, owned_stats=owned)

    def test_close_overflow_failure_and_permission_cannot_be_ignored(self):
        for index in (0, 1, 2, 8, 13, 14, 15):
            report, publication, owned = fixture()
            report['stats'][index] += 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate(report, publication, owned_stats=owned)
        for index, value in ((3, 0), (4, 0x80004005)):
            report, publication, owned = fixture()
            report['recordings'][0][index] = value
            with self.assertRaises(ValueError):validate(report, publication, owned_stats=owned)

    def test_metadata_flags_and_placement_are_strict(self):
        for index, value in ((0, 0), (1, 0), (2, 2), (3, 1), (6, 0), (7, 0), (8, 2), (11, 28), (13, 2), (14, 0), (15, 1)):
            report, publication, owned = fixture()
            report['resources'][0][index] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                validate(report, publication, owned_stats=owned)
        report, publication, owned = fixture()
        report['resources'][0][2:6] = [2, 0x140000, 65536, 65536]
        self.assertTrue(validate(report, publication, owned_stats=owned)['verified'])

    def test_all_raw_fields_are_bounded_and_arrays_have_exact_counts(self):
        report, publication, owned = fixture()
        for name, _, _, _ in BLOCKS:
            for invalid in (None, [], report[name][:-1], report[name]+[report[name][0]]):
                bad = copy.deepcopy(report);bad[name] = invalid
                with self.subTest(name=name), self.assertRaises(ValueError):validate(bad, publication, owned_stats=owned)
        for name in ('stats', 'recordings', 'resources', 'events', 'children'):
            for invalid in (True, -1, U64+1):
                bad = copy.deepcopy(report)
                if name == 'stats':bad[name][0] = invalid
                else:bad[name][0][0] = invalid
                with self.subTest(name=name, invalid=invalid), self.assertRaises(ValueError):validate(bad, publication, owned_stats=owned)
        for name, index in (('events', 4), ('children', 13)):
            bad = copy.deepcopy(report);bad[name][0][index] = U32+1
            with self.assertRaises(ValueError):validate(bad, publication, owned_stats=owned)

    def test_reader_is_bounded_and_uses_real_pointer_exports(self):
        report, _, _ = fixture()
        memory = {};exports = {PREFIX+'Seq':struct.pack('<I', 2), PREFIX+'Stats':struct.pack('<16Q', *report['stats'])}
        for index, (name, suffix, layout, _) in enumerate(BLOCKS):
            pointer = 0x100000 + index*0x100000
            exports[PREFIX+suffix] = struct.pack('<Q', pointer)
            memory[pointer] = b''.join(struct.pack(layout, *row) for row in report[name])
        read = lambda name, size:exports[name][:size]
        mem = lambda pointer, size:memory[pointer][:size]
        self.assertEqual(read_report(read, mem), report)
        exports[PREFIX+'Seq'] = struct.pack('<I', 3)
        with self.assertRaisesRegex(ValueError, 'changed'):read_report(read, mem)
        exports[PREFIX+'Seq'] = struct.pack('<I', 2)
        exports[PREFIX+'Events'] = struct.pack('<Q', U64-8)
        with self.assertRaisesRegex(ValueError, 'pointer/count'):read_report(read, mem)
        exports[PREFIX+'Events'] = struct.pack('<Q', 0x300000)
        memory[0x300000] = memory[0x300000][:-1]
        with self.assertRaisesRegex(ValueError, 'truncated'):read_report(read, mem)


if __name__ == '__main__':
    unittest.main()
