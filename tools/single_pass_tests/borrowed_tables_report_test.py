import copy
import struct
import unittest

import borrowed_tables_report as report
from full_t5_report_test import fixture as source_fixture


def fixture():
    _, _, source, tail = source_fixture()
    rows, ranges = [], []
    for ordinal, side in ((1, 0), (2, 0), (4, 1), (5, 1)):
        first = len(ranges)
        rows.append([ordinal, side, 77, 0x1000+side, 7+side, 1, 1, 47, first, 2, 9, 1, 1, 1, 0, 0])
        table = 0x100000+ordinal*64
        ranges.extend([[ordinal, 0, 2, 1, 1, 32, 1, table, table+32, 3, 10, 0],
                       [ordinal, 31, 0, 0, 8, 32, 32768, 0x200000, 0x200000, 3, 20, 1]])
    inventories = [[1, 0, 77, 1, 3, 0x200000, 8, 20, 8, 2, 1, 4, 1, 3, 2, 1, 2, 1, 1, 1, 0, 0, 0, 0]]
    pageable = dict(stats=[1, 1, 77, 2, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0], failure=[0]*24,
        rows=[[1, 0, 77, 1, 100, 0x1000, 100, 110, 0, 120, 1, 1, 1, 0, 0, 0],
              [4, 1, 77, 2, 200, 0x1001, 210, 220, 1, 230, 2, 1, 1, 0, 0, 0]])
    value = report.decode([1, 1, 77, 4, 4, 8, 36, 4, 4, 0, 0, 0, 0, 0, 0, 0], rows, ranges, inventories, [0]*8, pageable)
    return value, source, tail


class BorrowedTablesTests(unittest.TestCase):
    def checked(self, value=None, source=None, tail=None):
        base, original, expected = fixture()
        return report.validate(value if value is not None else base, requested=True,
            source=original if source is None else source, tail=expected if tail is None else tail)

    def test_complete_no_overwrite_is_not_resource_or_bank_authority(self):
        value = self.checked()
        self.assertTrue(value['verified'])
        self.assertTrue(value['pageableUse']['allQualified'])
        for key in ('executionPermission', 'resourceReadPermission', 'nativeSuppressionPermission',
                    'fullDynamicBankValid', 'inventoryResourceOwnershipProved'):
            self.assertIs(value[key], False)
        self.assertEqual(value['inventories'][0][9], 2)
        self.assertEqual(value['inventories'][0][15], 1)
        self.assertFalse(value['pageableUse']['fullDynamicBankValid'])

    def test_capture_and_post_prefix_refusals_never_pass(self):
        for index in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15):
            value, _, _ = fixture();value['stats'][index] += 1
            with self.subTest(index=index), self.assertRaises(ValueError):self.checked(value)
        for index in (3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15):
            value, _, _ = fixture();value['rows'][0][index] = 0
            if index in (8, 14, 15):value['rows'][0][index] = 1
            with self.subTest(row=index), self.assertRaises(ValueError):self.checked(value)
        value, _, _ = fixture();value['failure'][2] = 7
        with self.assertRaises(ValueError):self.checked(value)

    def test_exact_tail_and_source_joins(self):
        for key in ('rows', 'ranges', 'inventories'):
            value, _, _ = fixture();value[key] = value[key][1:]
            with self.subTest(key=key), self.assertRaises(ValueError):self.checked(value)
        value, source, tail = fixture();tail['nativeSources'][0][1] = 1
        with self.assertRaises(ValueError):self.checked(value, source, tail)
        value, source, tail = fixture();source['rows'][1][10] = 88
        with self.assertRaises(ValueError):self.checked(value, source, tail)
        value, _, _ = fixture();value['rows'][1][7] = 48
        with self.assertRaises(ValueError):self.checked(value)

    def test_range_bounds_identity_and_inventory_joins(self):
        for index, word in ((0, 2), (1, 64), (2, 4), (3, 2**32), (4, 0), (4, report.MAX_SLOTS+1),
                            (5, 0), (6, 0), (7, 0), (8, 1), (9, 0), (10, 0), (11, 2)):
            value, _, _ = fixture();value['ranges'][0][index] = word
            with self.subTest(index=index, word=word), self.assertRaises(ValueError):self.checked(value)
        for index, word in ((0, 2), (1, 1), (2, 78), (3, 3), (4, 4), (5, 7), (6, 9), (7, 21),
                            (8, 9), (9, 3), (13, 5), (14, 3), (16, 4), (18, 5), (19, 5), (20, 1)):
            value, _, _ = fixture();value['inventories'][0][index] = word
            with self.subTest(inventory=index), self.assertRaises(ValueError):self.checked(value)
        value, _, _ = fixture();value['inventories'] *= 2
        with self.assertRaises(ValueError):self.checked(value)

    def test_pageable_refusal_is_observation_not_whole_bank_failure(self):
        value, _, _ = fixture();page = value['pageableUse']
        page['stats'] = [1, 1, 77, 2, 1, 1, 1, 0, 1, 0, 0, 7, 351, 5, 0, 0]
        page['rows'][1][6:] = [0, 0, 0, 0, 0, 0, 0, 7, 351, 0]
        page['failure'][7:9] = [7, 351]
        checked = self.checked(value)
        self.assertTrue(checked['verified']);self.assertFalse(checked['pageableUse']['allQualified'])
        value, _, _ = fixture();page = value['pageableUse'];page['stats'][5] = 1;page['stats'][7] = 1
        page['stats'][11:14] = [8, 338, 5];page['rows'][1][12:15] = [0, 8, 338];page['failure'][7:9] = [8, 338]
        self.assertFalse(self.checked(value)['pageableUse']['allQualified'])
        value, _, _ = fixture();value['pageableUse']['stats'][10] = 1
        self.assertFalse(self.checked(value)['pageableUse']['allQualified'])

    def test_pageable_inconsistent_records_and_permissions_reject(self):
        for index, word in ((0, 0), (1, 1), (2, 76), (3, 0), (4, 0), (5, 99), (6, 999),
                            (7, 0), (9, 0), (10, 3), (11, 2), (12, 0), (13, 1), (15, 1)):
            value, _, _ = fixture();value['pageableUse']['rows'][0][index] = word
            with self.subTest(index=index), self.assertRaises(ValueError):self.checked(value)
        for index in (3, 4, 5, 6, 7, 8, 9, 14, 15):
            value, _, _ = fixture();value['pageableUse']['stats'][index] += 1
            with self.subTest(stats=index), self.assertRaises(ValueError):self.checked(value)

    def test_first_pageable_refusal_is_temporal_not_asset_row_order(self):
        value, _, _ = fixture();page = value['pageableUse']
        page['stats'] = [1, 1, 77, 2, 1, 0, 1, 1, 1, 0, 0, 11, 409, 2, 0, 0]
        page['rows'][0][12:15] = [0, 8, 413]
        page['rows'][1][:3] = [1, 0, 77];page['rows'][1][5] = 0x1000
        page['rows'][1][6:] = [0, 0, 0, 0, 0, 0, 0, 11, 409, 0]
        page['failure'][7:9] = [11, 409]
        self.assertFalse(self.checked(value)['pageableUse']['allQualified'])

    def test_pageable_search_is_optional_observation_without_permission(self):
        value, _, _ = fixture();page = value['pageableUse']
        page['stats'] = [1, 1, 77, 2, 1, 1, 1, 0, 1, 0, 0, 11, 409, 5, 0, 0]
        page['rows'][1][6:] = [0, 0, 0, 0, 0, 0, 0, 11, 409, 0]
        page['failure'][7:9] = [11, 409]
        page['search'] = [1, 1, 0, 210, 0x1001, 1, 2, 12, 0, 1, 0, 1, 0, 3, 220, 0x1000, 0, 17, 1, 0]
        self.assertEqual(self.checked(value)['pageableUse']['search'], page['search'])
        page['search'][19] = 1
        with self.assertRaises(ValueError):self.checked(value)

    def test_disabled_mode_is_exactly_idle(self):
        disabled = report.decode([1]+[0]*15, [], [], [], [0]*8,
            dict(stats=[1]+[0]*15, rows=[], failure=[0]*24))
        self.assertTrue(report.validate(disabled, requested=False)['verified'])
        for index in range(1, 16):
            value = copy.deepcopy(disabled);value['stats'][index] = 1
            with self.subTest(index=index), self.assertRaises(ValueError):report.validate(value, requested=False)
        value = copy.deepcopy(disabled);value['pageableUse']['stats'][2] = 77
        with self.assertRaises(ValueError):report.validate(value, requested=False)

    def test_capture_join_and_raw_evidence(self):
        value, source, tail = fixture();checked = self.checked(value)
        capture = dict(borrowedTablesRequested=True, borrowedTablesAvailable=True, borrowedTables=checked,
                       sourceStream=source, preparedTail=tail)
        self.assertEqual(report.validate_capture(capture, True), checked)
        with self.assertRaises(ValueError):report.validate_capture(capture, False)
        capture['borrowedTables']['fullDynamicBankValid'] = True
        with self.assertRaises(ValueError):report.validate_capture(capture, True)
        value, _, _ = fixture();value['stats'][9] = 1;original = copy.deepcopy(value)
        with self.assertRaises(ValueError):self.checked(value)
        self.assertEqual(value, original)

    def test_bad_abi_words_and_shapes_are_rejected(self):
        for word in (True, False, -1, 2**64, '1', None):
            value, _, _ = fixture();value['stats'][1] = word
            with self.subTest(word=word), self.assertRaises(ValueError):self.checked(value)
        for key, row in (('rows', [0]*15), ('ranges', [0]*13), ('inventories', [0]*23)):
            value, _, _ = fixture();value[key] = [row]
            with self.subTest(key=key), self.assertRaises(ValueError):self.checked(value)
        value, _, _ = fixture();value['failure'] = [0]*9
        with self.assertRaises(ValueError):self.checked(value)
        value, _, _ = fixture();value['rows'] *= 129
        with self.assertRaises(ValueError):self.checked(value)

    def test_double_read_bounded_payloads_and_optional_pageables(self):
        value, _, _ = fixture();exports = {};memory = {}
        def put(name, fmt, values):exports[report.PREFIX+name] = struct.pack(fmt, *values)
        put('BorrowedTablesStats', '<16Q', value['stats']);put('BorrowedFailure', '<8Q', value['failure'])
        for index, (name, suffix, words, _) in enumerate(report.ARRAYS):
            pointer = 0x1000+index*0x10000;rows = value[name]
            put(suffix+'Count', '<I', [len(rows)]);put(suffix+'RowBytes', '<I', [words*8]);put(suffix+'Rows', '<Q', [pointer])
            memory[pointer] = b''.join(struct.pack(f'<{words}Q', *row) for row in rows)
        read_export = lambda name, size:exports[name]
        read_memory = lambda pointer, size:memory[pointer][:size]
        self.assertEqual(report.read_report(read_export, read_memory)['rows'], value['rows'])
        put('PageableUseStats', '<16Q', value['pageableUse']['stats']);put('PageableUseFailure', '<24Q', value['pageableUse']['failure'])
        put('PageableUseCount', '<I', [2]);put('PageableUseRowBytes', '<I', [128]);put('PageableUseRows', '<Q', [0x31000])
        memory[0x31000] = b''.join(struct.pack('<16Q', *row) for row in value['pageableUse']['rows'])
        observed = report.read_report(read_export, read_memory, pageable_available=True)
        self.assertEqual(observed['pageableUse']['rows'], value['pageableUse']['rows'])
        put('PageableUseCount', '<I', [2049])
        with self.assertRaises(ValueError):report.read_report(read_export, read_memory, pageable_available=True)
        put('PageableUseCount', '<I', [2])
        put('BorrowedRangeCount', '<I', [16385])
        with self.assertRaises(ValueError):report.read_report(read_export, read_memory)
        put('BorrowedRangeCount', '<I', [len(value['ranges'])]);put('BorrowedRangeRows', '<Q', [2**64-1])
        with self.assertRaises(ValueError):report.read_report(read_export, read_memory)
        put('BorrowedRangeRows', '<Q', [0x11000])
        with self.assertRaises(ValueError):report.read_report(read_export, lambda pointer, size:b'')
        calls = 0
        def changing(name, size):
            nonlocal calls
            if name.endswith('BorrowedTablesStats'):
                calls += 1;stats = value['stats'].copy();stats[2] += calls
                return struct.pack('<16Q', *stats)
            return exports[name]
        with self.assertRaises(ValueError):report.read_report(changing, read_memory, attempts=2)


if __name__ == '__main__':unittest.main()
