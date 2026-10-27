import struct
import unittest

from graph_parent_report import name_hash
from native_stage_report import (
    MAX_DEFINITIONS, MAX_GRAPH_NODES, MAX_STAGES, RENDER_VTABLE_RVA, RENDER_WORK_RVA,
    SCENE_VTABLE_RVA, SCENE_WORK_RVA, decode,
)


BASE = 0x140000000


def fixture(*, shared=True, direct=False, empty=False):
    stats = [1, 1, 73, 81, 92, 2, 0 if empty else 6, 0]
    owned = [0] * 24
    owned[0:4] = [4, 73, 81, 92]
    owned[8] = 2
    nodes, definitions, stages = [], [], []
    for view in (0, 1):
        handle, root = 10 + view, 0x31000 + view * 0x100
        obj = root if direct else 0x41000 + (0 if shared else view * 0x100)
        node_raw = bytearray(48)
        struct.pack_into('<I', node_raw, 0, view)
        struct.pack_into('<Q', node_raw, 16, root)
        struct.pack_into('<I', node_raw, 24, view)
        definition = bytearray(32)
        struct.pack_into('<Q', definition, 24, name_hash('GBuffer_Discard'))
        node_vtable, node_work = ((RENDER_VTABLE_RVA, RENDER_WORK_RVA) if direct else
                                  (SCENE_VTABLE_RVA, SCENE_WORK_RVA))
        struct.pack_into('<Q', definition, 0, BASE + node_vtable)
        nodes.append(struct.pack('<II48sQQ32s', handle, view, node_raw,
                                 BASE + node_vtable, BASE + node_work, definition))
        header = bytearray(64)
        struct.pack_into('<Q', header, 0, BASE + RENDER_VTABLE_RVA)
        header[8:24] = bytes(range(16))
        struct.pack_into('<4Q2I', header, 24, BASE + 0x1000, BASE + 0x2000,
                         BASE + 0x3000, 0x50000, 8, 0 if empty else 3)
        definitions.append(struct.pack('<8Q64s', obj, handle, view, len(stages),
                                       0 if empty else 3, BASE + RENDER_VTABLE_RVA,
                                       BASE + RENDER_WORK_RVA, root, header))
        if not empty:
            for stage in (3, 9, 9):
                raw = bytearray(48)
                struct.pack_into('<QIB', raw, 0, BASE + 0x4000 + stage, 0x100 + view, 2)
                raw[13:16] = b'abc'
                struct.pack_into('<I', raw, 16, stage)
                raw[20:24] = b'\x04xyz'
                struct.pack_into('<2I', raw, 24, 0x123, 0x456)
                raw[32:36] = b'\x07def'
                struct.pack_into('<3I', raw, 36, 8, 20, 30)
                stages.append(bytes(raw))
    return [stats, b''.join(definitions), b''.join(stages), b''.join(nodes), owned, BASE]


def change_u64(args, blob_index, offset, value):
    raw = bytearray(args[blob_index])
    struct.pack_into('<Q', raw, offset, value)
    args[blob_index] = bytes(raw)


def change_u32(args, blob_index, offset, value):
    raw = bytearray(args[blob_index])
    struct.pack_into('<I', raw, offset, value)
    args[blob_index] = bytes(raw)


class NativeStageReportTest(unittest.TestCase):
    def test_complete_child_snapshot_preserves_opaque_bytes(self):
        args = fixture()
        result = decode(*args)
        self.assertEqual((result['consumerId'], result['epoch'], result['pairGeneration']), (73, 81, 92))
        self.assertEqual((result['definitionCount'], result['stageCount']), (2, 6))
        self.assertEqual(result['stages'], {3: 2, 9: 4})
        self.assertEqual((result['stage9Count'], result['tailStage9Count']), (4, 4))
        item = result['definitions'][0]
        self.assertEqual(item['rawHeader'], args[1][64:128].hex())
        self.assertEqual(item['entries'][0]['raw'], args[2][:48].hex())
        self.assertEqual(item['gate']['rva'], '0x1000')
        self.assertEqual(item['before']['rva'], '0x2000')
        self.assertEqual(item['after']['rva'], '0x3000')
        self.assertEqual(item['tailFirstStage'], 1)
        self.assertEqual(item['stages'], [3, 9, 9])
        entry = item['entries'][0]
        self.assertEqual((entry['viewFlags'], entry['mode'], entry['technique'], entry['state']), (0x100, 2, 4, 7))
        self.assertEqual((entry['filter'], entry['prebuiltList'], entry['partition']), ([0x123, 0x456], 8, [20, 30]))
        self.assertEqual(result['pairedSharedObjects'], [dict(object='0x41000', definitions=[0, 1], views=[0, 1])])
        for field in ('nativeOwnershipProved', 'childPathProved', 'partitionSemanticsProved', 'executionPermitted'):
            self.assertIs(result[field], False)

    def test_direct_roots_are_supported(self):
        result = decode(*fixture(direct=True))
        self.assertEqual(result['pairedSharedObjects'], [])
        self.assertEqual(result['definitions'][0]['object'], result['roots'][0]['object'])

    def test_empty_arrays_are_observations_not_draw_permission(self):
        result = decode(*fixture(empty=True))
        self.assertEqual(result['stageCount'], 0)
        self.assertIsNone(result['definitions'][0]['tailFirstStage'])
        self.assertFalse(result['executionPermitted'])

    def test_only_contiguous_suffix_counts_as_tail(self):
        args = fixture()
        for index, stage in enumerate((9, 3, 9, 9, 9, 3)):
            change_u32(args, 2, index * 48 + 16, stage)
        result = decode(*args)
        self.assertEqual(result['stage9Count'], 4)
        self.assertEqual(result['tailStage9Count'], 1)

    def test_null_or_nonimage_callbacks_remain_opaque(self):
        args = fixture()
        change_u64(args, 1, 64 + 24, 0)
        change_u64(args, 2, 0, 0x8000)
        result = decode(*args)
        self.assertIsNone(result['definitions'][0]['gate']['rva'])
        self.assertEqual(result['definitions'][0]['entries'][0]['callback'], dict(address='0x8000', rva=None))

    def test_invalid_stats_abi(self):
        for values in ([1] * 7, [1] * 9, '12345678', [True] + [1] * 7,
                       [1] * 7 + [-1], [1] * 7 + [1 << 64]):
            with self.subTest(values=values):
                args = fixture()
                args[0] = values
                with self.assertRaises(ValueError):
                    decode(*args)

    def test_missing_failed_or_wrong_schema_rejected(self):
        for slot, value in ((0, 0), (0, 2), (1, 0), (1, 2), (7, 1)):
            with self.subTest(slot=slot, value=value):
                args = fixture()
                args[0][slot] = value
                with self.assertRaises(ValueError):
                    decode(*args)

    def test_count_limits_precede_blob_reads(self):
        for slot, value in ((5, 0), (5, MAX_DEFINITIONS + 1), (6, MAX_STAGES + 1)):
            with self.subTest(slot=slot, value=value):
                args = fixture()
                args[0][slot] = value
                with self.assertRaisesRegex(ValueError, 'bounded'):
                    decode(*args)

    def test_consumer_epoch_and_generation_must_match(self):
        for index in (2, 3, 4):
            for value in (0, 12345):
                with self.subTest(index=index, value=value):
                    args = fixture()
                    args[0][index] = value
                    with self.assertRaisesRegex(ValueError, 'consumer/epoch/pair'):
                        decode(*args)

    def test_owned_stats_abi(self):
        for owned in ([0] * 23, [0] * 25, [True] * 24):
            args = fixture()
            args[4] = owned
            with self.assertRaises(ValueError):
                decode(*args)

    def test_binary_sizes_are_exact(self):
        for index in (1, 2, 3):
            for change in (-1, 1):
                with self.subTest(index=index, change=change):
                    args = fixture()
                    args[index] = args[index][:-1] if change < 0 else args[index] + b'\0'
                    with self.assertRaisesRegex(ValueError, 'ABI/count'):
                        decode(*args)

    def test_bad_image_base(self):
        for base in (0, -1, True, 1 << 64, (1 << 64) - 1):
            args = fixture()
            args[5] = base
            with self.assertRaises(ValueError):
                decode(*args)

    def test_render_definition_type_and_object_are_exact(self):
        for offset, value in ((0, 0), (0, 0x41001), (40, BASE + 99), (48, BASE + 100), (56, 0)):
            args = fixture()
            change_u64(args, 1, offset, value)
            with self.assertRaises(ValueError):
                decode(*args)

    def test_root_handle_object_and_view_join(self):
        for offset, value in ((8, 99), (8, 0x2000000A), (16, 1), (16, 2), (56, 0x99900)):
            args = fixture()
            change_u64(args, 1, offset, value)
            with self.assertRaises(ValueError):
                decode(*args)

    def test_root_type_and_frontier_name_join(self):
        for offset, value in ((56, BASE + 1), (64, BASE + 2), (72, 0), (96, name_hash('Lighting'))):
            args = fixture()
            change_u64(args, 3, offset, value)
            with self.assertRaises(ValueError):
                decode(*args)

    def test_direct_root_cannot_claim_another_child(self):
        args = fixture(direct=True)
        change_u64(args, 1, 0, 0x44400)
        with self.assertRaisesRegex(ValueError, 'Direct RenderElements'):
            decode(*args)

    def test_raw_header_vtable_count_capacity_storage(self):
        for offset, value, size in ((64, BASE + 1, 8), (64 + 60, 2, 4),
                                    (64 + 56, 2, 4), (64 + 56, MAX_STAGES + 1, 4),
                                    (64 + 48, 0, 8), (64 + 48, 0x50001, 8),
                                    (64 + 48, (1 << 64) - 8, 8)):
            args = fixture()
            (change_u64 if size == 8 else change_u32)(args, 1, offset, value)
            with self.assertRaises(ValueError):
                decode(*args)

    def test_raw_header_oversized_per_node_count(self):
        args = fixture()
        change_u64(args, 1, 32, MAX_STAGES + 1)
        change_u32(args, 1, 64 + 56, MAX_STAGES + 1)
        change_u32(args, 1, 64 + 60, MAX_STAGES + 1)
        with self.assertRaises(ValueError):
            decode(*args)

    def test_overlapping_and_out_of_bounds_stage_ranges(self):
        for first in (0, 2, 4, 1 << 63):
            args = fixture()
            change_u64(args, 1, 128 + 24, first)
            with self.assertRaises(ValueError):
                decode(*args)

    def test_unclaimed_stage_entry(self):
        args = fixture()
        args[0][6] += 1
        args[2] += bytes(48)
        with self.assertRaisesRegex(ValueError, 'unclaimed'):
            decode(*args)

    def test_missing_second_view(self):
        args = fixture()
        args[0][5:] = [1, 3, 0]
        args[1] = args[1][:128]
        args[2] = args[2][:144]
        with self.assertRaisesRegex(ValueError, 'both source views'):
            decode(*args)

    def test_duplicate_child_rejected_even_with_distinct_stage_storage(self):
        args = fixture()
        args[0][5] += 1
        args[0][6] += 3
        row = bytearray(args[1][:128])
        struct.pack_into('<Q', row, 24, 6)
        args[1] += bytes(row)
        args[2] += args[2][:144]
        with self.assertRaisesRegex(ValueError, 'Duplicate RenderElements'):
            decode(*args)

    def test_graph_duplicate_handles_and_publication_order(self):
        for offset, value in ((104, 10), (104, 0x2000000A), (108, 0), (112, 0),
                              (104, 65536), (104, 0x4000000B), (104, 0x8000000B)):
            args = fixture()
            change_u32(args, 3, offset, value)
            with self.assertRaisesRegex(ValueError, 'source graph node'):
                decode(*args)

    def test_normalized_handle_allowed_but_exact_receipt_join_required(self):
        args = fixture()
        change_u32(args, 3, 0, 0x2000000A)
        with self.assertRaisesRegex(ValueError, 'exact source graph node'):
            decode(*args)
        change_u64(args, 1, 8, 0x2000000A)
        self.assertEqual(decode(*args)['roots'][0]['nodeHandle'], 0x2000000A)

    def test_source_graph_owned_count_and_bound(self):
        args = fixture()
        args[4][8] += 1
        with self.assertRaisesRegex(ValueError, 'Source graph node ABI/count'):
            decode(*args)
        args[3] = bytes((MAX_GRAPH_NODES + 1) * 104)
        args[4][8] = MAX_GRAPH_NODES + 1
        with self.assertRaisesRegex(ValueError, 'Source graph node ABI/count'):
            decode(*args)

    def test_empty_stage_array_still_requires_valid_storage(self):
        args = fixture(empty=True)
        change_u64(args, 1, 64 + 48, 0)
        with self.assertRaisesRegex(ValueError, 'stage storage'):
            decode(*args)

    def test_multiple_roots_in_one_view(self):
        args = fixture(shared=False)
        change_u32(args, 3, 104 + 8 + 24, 0)
        change_u64(args, 1, 128 + 16, 0)
        with self.assertRaisesRegex(ValueError, 'More than one'):
            decode(*args)


if __name__ == '__main__':
    unittest.main()
