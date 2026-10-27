import copy
import struct
import unittest

from graph_parent_report import name_hash
from native_stage_report import decode as decode_stages
from scene_sections_report import (
    CHILD_BYTES, MAX_ARRAY_CAPACITY, MAX_NODES, PATTERN, ROOT_BYTES, VTABLE_RVA, WORK_RVA, decode,
)


BASE = 0x140000000


def fixture():
    owned = [0] * 24
    owned[:4] = [4, 31, 41, 59]
    owned[8] = 2
    stats = [1, 1, 31, 41, 59, 2, 22, 0]
    roots, children, nodes, definitions, entries = [], [], [], [], []
    for view in (0, 1):
        root_obj, handle = 0x400000 + view * 0x100, 10 + view
        header = bytearray(b'z' * 56)
        struct.pack_into('<Q', header, 0, BASE + VTABLE_RVA)
        struct.pack_into('<Q', header, 24, name_hash('GBuffer_Discard'))
        struct.pack_into('<I', header, 32, 1)
        struct.pack_into('<Q2I', header, 40, 0x600000 + view * 0x100, 16, 11)
        payload = bytearray(48)
        struct.pack_into('<I', payload, 0, view)
        struct.pack_into('<QI', payload, 16, root_obj, view)
        nodes.append(struct.pack('<II48sQQ32s', handle, view, payload,
                                 BASE + VTABLE_RVA, BASE + WORK_RVA, header[:32]))
        roots.append(struct.pack('<7Q56s', root_obj, handle, view, len(children), 11, 6, 0, header))
        for slot, profile in enumerate(PATTERN):
            vtable, work, destructor, size, parameter = profile
            # Setup/bind/unbind/end objects retain repeated ordered handles.
            unique = {6: 0, 7: 1, 9: 4, 10: 5}.get(slot, slot)
            obj = 0x500000 + unique * 0x100
            prefix = bytearray(b'x' * 32)
            struct.pack_into('<Q', prefix, 0, BASE + vtable)
            if parameter is not None:
                struct.pack_into('<I', prefix, 24, parameter)
            if size == 24:
                prefix[24:] = bytes(8)
            if slot in (2, 8):
                late = slot == 8
                count = 4 if late else 2
                struct.pack_into('<Q', prefix, 24, BASE + (0x1134C40 if late else 0x1D41BE0))
                render_header = bytearray(64)
                render_header[:32] = prefix
                struct.pack_into('<Q2I', render_header, 48, 0x700000 + unique * 0x100, 8, count)
                definitions.append(struct.pack('<8Q64s', obj, handle, view, len(entries), count,
                                               BASE + vtable, BASE + work, root_obj, render_header))
                for index in range(count):
                    raw = bytearray(48)
                    struct.pack_into('<I', raw, 8, 17 if index % 2 else 33)
                    raw[12] = index % 2
                    raw[13:16] = b'pad'
                    struct.pack_into('<I', raw, 16, (9 if index < 2 else 8) if late else 7)
                    raw[20] = 0 if index % 2 else 2
                    raw[21:24] = b'xxx'
                    raw[33:36] = b'yyy'
                    struct.pack_into('<I', raw, 36, 0xFFFFFFFF)
                    entries.append(bytes(raw))
            children.append(struct.pack('<5Q2I32s', obj, obj + 0x80, BASE + vtable, BASE + work,
                                        BASE + destructor, slot, size, prefix))
    nodes_raw = b''.join(nodes)
    stages = decode_stages([1, 1, 31, 41, 59, 4, 12, 0], b''.join(definitions), b''.join(entries), nodes_raw, owned, BASE)
    return [stats, b''.join(roots), b''.join(children), nodes_raw, owned, BASE, stages]


def patch(args, blob, offset, value, layout='<Q'):
    raw = bytearray(args[blob])
    struct.pack_into(layout, raw, offset, value)
    args[blob] = bytes(raw)


def patch_render(args, definition, field, offset, value, layout='<Q', entry=0):
    row = args[6]['definitions'][definition]
    target = row if field == 'rawHeader' else row['entries'][entry]
    raw = bytearray.fromhex(target[field])
    struct.pack_into(layout, raw, offset, value)
    target[field] = raw.hex()


class SceneSectionsReportTest(unittest.TestCase):
    def test_complete_exact_observation(self):
        args = fixture()
        report = decode(*args)
        self.assertEqual((report['consumerId'], report['epoch'], report['pairGeneration']), (31, 41, 59))
        self.assertEqual((report['rootCount'], report['childCount'], report['prefixCount'], report['tailCount']), (2, 22, 6, 5))
        root = report['roots'][0]
        self.assertEqual(root['rawHeader'], args[1][56:112].hex())
        self.assertEqual(root['raw'], args[1][:112].hex())
        self.assertEqual(root['children'][3]['raw'], args[2][3 * 80:4 * 80].hex())
        self.assertEqual(root['children'][3]['capturedBytes'], 24)
        self.assertEqual(root['children'][3]['rawPrefix'][-16:], '0' * 16)
        self.assertEqual([item['stages'] for item in root['renderElements']], [[7, 7], [9, 9, 8, 8]])
        self.assertEqual(root['children'][0]['object'], root['children'][6]['object'])
        self.assertTrue(report['observationVerified'])
        for key in ('nativeOwnershipProved', 'clonedDefinitionsProved', 'executionPermitted'):
            self.assertIs(report[key], False)

    def test_root_row_order_is_not_view_order(self):
        args = fixture()
        args[1] = args[1][ROOT_BYTES:] + args[1][:ROOT_BYTES]
        self.assertEqual([root['viewIndex'] for root in decode(*args)['roots']], [1, 0])

    def test_source_handles_keep_native_flag(self):
        args = fixture()
        patch(args, 3, 0, 0x2000000A, '<I')
        patch(args, 1, 8, 0x2000000A)
        for row in args[6]['definitions'][:2]:
            row['nodeHandle'] = 0x2000000A
        self.assertEqual(decode(*args)['roots'][0]['rootHandle'], 0x2000000A)

    def test_stats_abi_and_unsigned_words(self):
        for stats in ([0] * 7, [0] * 9, '12345678', [True] + [1] * 7, [1] * 7 + [-1], [1] * 7 + [1 << 64]):
            args = fixture()
            args[0] = stats
            with self.assertRaises(ValueError):
                decode(*args)

    def test_failed_incomplete_and_wrong_schema(self):
        for field, value in ((0, 2), (1, 0), (1, 2), (5, 0), (5, 3), (6, 21), (6, 23), (7, 1)):
            args = fixture()
            args[0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                decode(*args)

    def test_consumer_epoch_pair_join(self):
        for field in (2, 3, 4):
            for value in (0, 1000):
                args = fixture()
                args[0][field] = value
                with self.assertRaisesRegex(ValueError, 'consumer/epoch/pair'):
                    decode(*args)

    def test_owned_stats_abi(self):
        for owned in ([0] * 23, [0] * 25, [False] * 24):
            args = fixture()
            args[4] = owned
            with self.assertRaises(ValueError):
                decode(*args)

    def test_raw_abi_lengths(self):
        for blob in (1, 2, 3):
            for extra in (False, True):
                args = fixture()
                args[blob] = args[blob] + b'\0' if extra else args[blob][:-1]
                with self.assertRaisesRegex(ValueError, 'ABI/count'):
                    decode(*args)

    def test_image_base_bounds(self):
        for base in (0, -1, True, 1 << 64, (1 << 64) - 0x312E210):
            args = fixture()
            args[5] = base
            with self.assertRaisesRegex(ValueError, 'image base'):
                decode(*args)

    def test_graph_count_matches_owned_and_is_bounded(self):
        args = fixture()
        args[4][8] = 3
        with self.assertRaisesRegex(ValueError, 'source graph ABI/count'):
            decode(*args)
        args[3] = bytes((MAX_NODES + 1) * 104)
        args[4][8] = MAX_NODES + 1
        with self.assertRaisesRegex(ValueError, 'source graph ABI/count'):
            decode(*args)

    def test_graph_identity_and_ordinal_shape(self):
        for offset, value, layout in ((104, 10, '<I'), (104, 0x2000000A, '<I'),
                                      (104, 65536, '<I'), (104, 0x4000000B, '<I'),
                                      (108, 0, '<I'), (112, 0, '<I'), (72, 0, '<Q')):
            args = fixture()
            patch(args, 3, offset, value, layout)
            with self.assertRaisesRegex(ValueError, 'source graph identity'):
                decode(*args)

    def test_root_identity_and_split_contract(self):
        for offset, value in ((0, 0), (0, 0x400001), (0, (1 << 64) - 8), (8, 99), (8, 0x2000000A),
                              (16, 1), (16, 2), (32, 10), (40, 5), (48, 1)):
            args = fixture()
            patch(args, 1, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                decode(*args)

    def test_duplicate_root_view(self):
        args = fixture()
        patch(args, 1, ROOT_BYTES + 16, 0)
        with self.assertRaisesRegex(ValueError, 'root/section identity'):
            decode(*args)

    def test_header_source_prefix_and_mode(self):
        for offset, value, layout in ((56, BASE + 1, '<Q'), (56 + 8, 0, '<Q'),
                                      (56 + 24, name_hash('Lighting'), '<Q'), (56 + 32, 0, '<I'),
                                      (56 + 32, 2, '<I')):
            args = fixture()
            patch(args, 1, offset, value, layout)
            with self.assertRaisesRegex(ValueError, 'source graph/header'):
                decode(*args)

    def test_wrong_root_type_and_work(self):
        for offset, value in ((56, BASE + 1), (64, BASE + 2)):
            args = fixture()
            patch(args, 3, offset, value)
            with self.assertRaises(ValueError):
                decode(*args)

    def test_root_array_bounds(self):
        for offset, value, layout in ((56 + 40, 0, '<Q'), (56 + 40, 0x600001, '<Q'),
                                      (56 + 40, (1 << 64) - 8, '<Q'), (56 + 48, 10, '<I'),
                                      (56 + 48, MAX_ARRAY_CAPACITY + 1, '<I'), (56 + 52, 10, '<I')):
            args = fixture()
            patch(args, 1, offset, value, layout)
            with self.assertRaises(ValueError):
                decode(*args)

    def test_root_child_ranges_overflow_and_overlap(self):
        for first in (0, 10, 12, 1 << 63, (1 << 64) - 1):
            args = fixture()
            patch(args, 1, ROOT_BYTES + 24, first)
            with self.assertRaises(ValueError):
                decode(*args)

    def test_child_pointer_control_and_profile(self):
        for offset, value, layout in ((0, 0, '<Q'), (0, 0x500001, '<Q'), (0, (1 << 64) - 8, '<Q'),
                                      (8, 0, '<Q'), (8, 0x500081, '<Q'), (8, (1 << 64) - 1, '<Q'),
                                      (16, BASE + 1, '<Q'), (24, BASE + 2, '<Q'), (32, BASE + 3, '<Q'),
                                      (40, 1, '<I'), (44, 24, '<I'), (48, BASE + 3, '<Q'), (72, 8, '<I')):
            args = fixture()
            patch(args, 2, offset, value, layout)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                decode(*args)

    def test_each_child_has_its_exact_profile_and_parameter(self):
        for slot, profile in enumerate(PATTERN):
            args = fixture()
            patch(args, 2, slot * CHILD_BYTES + 24, BASE + 12345)
            with self.assertRaisesRegex(ValueError, 'profile'):
                decode(*args)
            if profile[4] is not None:
                args = fixture()
                patch(args, 2, slot * CHILD_BYTES + 48 + 24, profile[4] ^ 1, '<I')
                with self.assertRaisesRegex(ValueError, 'parameter'):
                    decode(*args)

    def test_special24_requires_zero_export_padding(self):
        args = fixture()
        patch(args, 2, 3 * CHILD_BYTES + 48 + 24, 1)
        with self.assertRaisesRegex(ValueError, 'padding'):
            decode(*args)
        args = fixture()
        patch(args, 2, 3 * CHILD_BYTES + 44, 32, '<I')
        with self.assertRaisesRegex(ValueError, 'profile'):
            decode(*args)

    def test_native_stages_identity_and_counts(self):
        for key, value in (('schema', 2), ('schema', True), ('status', 'failed'), ('consumerId', 32), ('epoch', 42),
                           ('pairGeneration', 60), ('definitionCount', 3), ('stageCount', 11)):
            args = fixture()
            args[6][key] = value
            with self.assertRaisesRegex(ValueError, 'different native stages'):
                decode(*args)

    def test_native_stages_missing_duplicate_or_unjoined_child(self):
        for change in ('missing', 'duplicate', 'wrong-object', 'wrong-root', 'wrong-view', 'wrong-handle'):
            args = fixture()
            if change == 'missing':
                args[6]['definitions'].pop()
            elif change == 'duplicate':
                args[6]['definitions'][1] = copy.deepcopy(args[6]['definitions'][0])
            else:
                key, value = {'wrong-object': ('object', '0x999999'), 'wrong-root': ('rootObject', '0x888888'),
                              'wrong-view': ('viewIndex', 1), 'wrong-handle': ('nodeHandle', 99)}[change]
                args[6]['definitions'][0][key] = value
            with self.subTest(change=change), self.assertRaises(ValueError):
                decode(*args)

    def test_native_stages_bad_pointer_hex(self):
        for value in (None, 1, '-1', 'invalid', hex(1 << 64)):
            args = fixture()
            args[6]['definitions'][0]['object'] = value
            with self.assertRaises(ValueError):
                decode(*args)

    def test_render_header_matches_child_and_gates(self):
        for offset, value, layout in ((0, BASE + 1, '<Q'), (8, 0, '<Q'), (24, BASE + 0x1134C40, '<Q'),
                                      (32, BASE + 10, '<Q'), (40, BASE + 20, '<Q'), (48, 0, '<Q'),
                                      (48, (1 << 64) - 8, '<Q'), (56, 1, '<I'),
                                      (56, MAX_ARRAY_CAPACITY + 1, '<I'), (60, 3, '<I')):
            args = fixture()
            patch_render(args, 0, 'rawHeader', offset, value, layout)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode(*args)

    def test_all_render_entries_are_checked_including_late_stage8(self):
        fields = ((0, 1, '<Q'), (8, 0, '<I'), (12, 2, '<B'), (16, 6, '<I'), (20, 3, '<B'),
                  (24, 1, '<I'), (28, 1, '<I'), (32, 1, '<B'), (36, 0, '<I'), (40, 1, '<I'), (44, 1, '<I'))
        for definition in range(4):
            for entry in range(2 if definition % 2 == 0 else 4):
                for offset, value, layout in fields:
                    args = fixture()
                    patch_render(args, definition, 'raw', offset, value, layout, entry)
                    with self.subTest(definition=definition, entry=entry, offset=offset), self.assertRaisesRegex(ValueError, 'stage pattern'):
                        decode(*args)

    def test_render_raw_hex_is_exact(self):
        for field, value in (('rawHeader', '00'), ('rawHeader', 'z' * 128), ('rawHeader', ' ' * 128),
                             ('raw', '00'), ('raw', 'z' * 96), ('raw', ' ' * 96)):
            args = fixture()
            row = args[6]['definitions'][0]
            (row if field == 'rawHeader' else row['entries'][0])[field] = value
            with self.assertRaisesRegex(ValueError, 'raw hex ABI'):
                decode(*args)

    def test_render_report_fields_do_not_override_raw_data(self):
        for key, value in (('count', 4), ('workRva', '0x1'), ('vtableRva', '0x2'), ('stages', [9, 9]), ('entries', [])):
            args = fixture()
            args[6]['definitions'][0][key] = value
            with self.assertRaises(ValueError):
                decode(*args)

    def test_stage_report_ranges_and_indices(self):
        for key, value in (('index', True), ('index', 2), ('firstStage', -1), ('firstStage', 12),
                           ('firstStage', True), ('count', True)):
            args = fixture()
            args[6]['definitions'][0][key] = value
            with self.assertRaises(ValueError):
                decode(*args)
        args = fixture()
        args[6]['definitions'][1]['firstStage'] = 0
        with self.assertRaisesRegex(ValueError, 'ranges overlap'):
            decode(*args)
        for key in ('index', 'localIndex'):
            args = fixture()
            args[6]['definitions'][0]['entries'][0][key] = 1
            with self.assertRaisesRegex(ValueError, 'entry index'):
                decode(*args)

    def test_shared_child_and_control_identity_must_be_consistent(self):
        for offset, value in ((6 * CHILD_BYTES + 8, 0x888888),
                              (6 * CHILD_BYTES + 48 + 8, 0x1234),
                              (1 * CHILD_BYTES + 8, 0x500080)):
            args = fixture()
            patch(args, 2, offset, value)
            with self.assertRaisesRegex(ValueError, 'child/control observation changed'):
                decode(*args)


if __name__ == '__main__':
    unittest.main()
