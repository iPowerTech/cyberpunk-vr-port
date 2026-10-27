import copy
import struct
import unittest

import fixed_assets_report as report
from borrowed_tables_report_test import fixture as borrowed_fixture


def fixture():
    borrowed, source, tail = borrowed_fixture()
    borrowed['inventories'][0][8:20] = [8, 1, 1, 5, 1, 3, 2, 1, 4, 1, 0, 0]
    stats = [1, 1, 77, 1, 1, 0, 8, 2, 2, 1, 1, 0, 0, 0, 0, 0]
    counts = [1, 1, 2, 1, 1, 0, 1, 1, 0, 0]
    rows = [[1, 0, 77, 1, 3, 0x200000, 8, 20, 1, 0, 0, 2, 0, 8, 1, 1, 47, 0, 0, 0, 0, 0, 0, 0]]
    assets = [[0, 0, 101, 0x10000, 101, 2, 0, 0, 0x40000000, 7, 0x10000, 0x20000, 0x30000, 0, 0, 0],
              [0, 1, 102, 0x10008, 0, 7, 1, 124, 0x40000000, 8, 0x10008, 0, 0, 5, 0, 0]]
    slots = [[0, 0, report.NO_ASSET, 0, 0, 1, 0, 0],
             [0, 1, 0, 101, 0x10000, 2, 0, 0], [0, 2, 0, 101, 0x10000, 2, 0, 0],
             [0, 3, 0, 101, 0x10000, 4, 0, 0], [0, 4, 1, 102, 0x10008, 7, 0, 0],
             [0, 5, report.NO_ASSET, 0, 0, 0, 0, 0], [0, 6, report.NO_ASSET, 0, 0, 3, 0, 0],
             [0, 7, report.NO_ASSET, 103, 0, 6, 0, 0]]
    return report.decode(stats, counts, rows, assets, slots), source, tail, borrowed


class FixedAssetsTests(unittest.TestCase):
    def checked(self, value=None, source=None, tail=None, borrowed=None):
        original, originals, expected, witness = fixture()
        return report.validate(original if value is None else value, requested=True,
                               source=originals if source is None else source,
                               tail=expected if tail is None else tail,
                               borrowed=witness if borrowed is None else borrowed)

    def test_partial_classification_proves_bootstrap_not_gpu_permission(self):
        value = self.checked()
        self.assertTrue(value['verified']);self.assertTrue(value['bootstrapExercised'])
        self.assertFalse(value['allSlotsQualified'])
        for key in ('resourceReadPermission', 'executionPermission', 'residencyPermission', 'nativeSuppressionPermission'):
            self.assertIs(value[key], False)
        self.assertEqual(value['summary']['immutableAssets'], 1)
        self.assertEqual(value['statusCounts']['UnsupportedView'], 1)

    def test_all_slots_qualified_requires_every_actual_slot(self):
        value, source, tail, borrowed = fixture()
        value['stats'][5:11] = [1, 8, 1, 7, 1, 0];value['counts'] = [0, 1, 7, 0, 0, 0, 0, 0, 0, 0]
        value['assets'] = value['assets'][:1];value['rows'][0][9] = 1;value['rows'][0][11] = 1
        value['slots'] = [value['slots'][0]]+[[0, i, 0, 101, 0x10000, 2, 0, 0] for i in range(1, 8)]
        borrowed['inventories'][0][8:20] = [8, 0, 1, 7, 0, 1, 1, 0, 7, 0, 0, 0]
        checked = self.checked(value, source, tail, borrowed)
        self.assertTrue(checked['allSlotsQualified']);self.assertFalse(checked['executionPermission'])
        value['rows'][0][9] = 0
        with self.assertRaises(ValueError):self.checked(value, source, tail, borrowed)

    def test_typed_null_inventory_column_is_not_other_descriptor_kinds(self):
        value, source, tail, borrowed = fixture()
        borrowed['inventories'][0][10] = 2;borrowed['inventories'][0][12] = 0
        self.assertFalse(self.checked(value, source, tail, borrowed)['allSlotsQualified'])
        borrowed['inventories'][0][10] = 0;borrowed['inventories'][0][12] = 2
        with self.assertRaises(ValueError):self.checked(value, source, tail, borrowed)

    def test_canonical_samples_join_exact_capture_slot_cookie_and_status(self):
        value, _, _, _ = fixture()
        value['samples'] = [[0, 1, 2, 0]+[2, 1, 101]+[0]*17]
        self.assertEqual(self.checked(value)['samples'], value['samples'])
        for index in (0, 1, 2, 3, 6):
            bad=copy.deepcopy(value);bad['samples'][0][index]+=1
            if index==1:bad['samples'][0][index]=3
            with self.subTest(index=index),self.assertRaises(ValueError):self.checked(bad)
        value['samples']*=2
        with self.assertRaises(ValueError):self.checked(value)

    def test_wrong_stats_reserved_permission_counts_and_source_joins_fail(self):
        for index in range(16):
            value, _, _, _ = fixture();value['stats'][index] += 1
            with self.subTest(stats=index), self.assertRaises(ValueError):self.checked(value)
        for index in range(24):
            value, _, _, _ = fixture();value['rows'][0][index] += 1
            with self.subTest(row=index), self.assertRaises(ValueError):self.checked(value)
        for index in range(10):
            value, _, _, _ = fixture();value['counts'][index] += 1
            with self.subTest(count=index), self.assertRaises(ValueError):self.checked(value)
        value, source, tail, borrowed = fixture();source['rows'][1][6] += 1
        with self.assertRaises(ValueError):self.checked(value, source, tail, borrowed)

    def test_asset_cookie_pin_failure_stage_and_slot_dedup(self):
        for index, word in ((0, 1), (1, 1), (2, 0), (3, 0), (4, 102), (5, 1), (6, 1), (7, 1),
                            (8, 0), (9, 32769), (10, 0x10001), (11, 0), (12, 0), (13, 1), (14, 1), (15, 1)):
            value, _, _, _ = fixture();value['assets'][0][index] = word
            with self.subTest(asset=index), self.assertRaises(ValueError):self.checked(value)
        for index, word in ((0, 1), (1, 2), (2, 2), (3, 102), (4, 0x10008), (5, 9), (6, 1), (7, 1)):
            value, _, _, _ = fixture();value['slots'][1][index] = word
            with self.subTest(slot=index), self.assertRaises(ValueError):self.checked(value)
        value, _, _, _ = fixture();value['assets'][1][2:4] = value['assets'][0][2:4]
        with self.assertRaises(ValueError):self.checked(value)
        for stage in (0, 3, 4, 5):
            value, _, _, _ = fixture();value['assets'][1][6] = stage
            with self.subTest(stage=stage), self.assertRaises(ValueError):self.checked(value)

    def test_cookie_and_immutable_refusals_remain_honest_partial_results(self):
        for status, stage in ((8, 3), (9, 4), (9, 5)):
            value, _, _, _ = fixture();value['assets'][1][4:8] = [999 if status == 8 else 102, status, stage, 130]
            value['slots'][4][5] = status;value['counts'][7] = 0;value['counts'][status] = 1
            with self.subTest(status=status, stage=stage):self.assertFalse(self.checked(value)['allSlotsQualified'])

    def test_final_immutable_recheck_keeps_prior_per_view_refusal(self):
        value, _, _, _ = fixture()
        value['assets'][0][5:8] = [9, 5, 93]
        value['assets'][1] = [0, 1, 102, 0x10008, 102, 2, 0, 0, 0x40000000, 8, 0x10008, 0x20008, 0x30000, 0, 0, 0]
        for index in (1, 2):value['slots'][index][5] = 9
        value['slots'][4][5] = 2
        value['stats'][8:11] = [1, 1, 1];value['counts'] = [1, 1, 1, 1, 1, 0, 1, 0, 0, 2]
        self.assertFalse(self.checked(value)['allSlotsQualified'])
        value['assets'][0][6] = 4
        with self.assertRaises(ValueError):self.checked(value)

    def test_unusable_null_view_is_not_falsely_required_to_be_typed_null(self):
        value, source, tail, borrowed = fixture()
        value['slots'][0][5] = 4;value['counts'][1] = 0;value['counts'][4] += 1
        self.assertFalse(self.checked(value, source, tail, borrowed)['allSlotsQualified'])

    def test_raw_failed_attempts_are_preserved_beyond_held_range_cap(self):
        value, _, _, _ = fixture();value['rows'] *= 5;value['stats'][3] = 5;value['stats'][11] = 4
        raw = report.decode(*(value[name] for name in ('stats', 'counts', 'rows', 'assets', 'slots')))
        self.assertEqual(len(raw['rows']), 5)
        with self.assertRaises(ValueError):self.checked(raw)

    def test_requested_zero_successful_bootstrap_cannot_pass(self):
        value, _, _, _ = fixture();value['stats'][8:11] = [0, 0, 2]
        value['assets'][0][5:8] = [9, 4, 130]
        for index in (1, 2, 3):value['slots'][index][5] = 9
        value['counts'] = [1, 1, 0, 1, 0, 0, 1, 1, 0, 3]
        with self.assertRaisesRegex(ValueError, 'successful native bootstrap'):self.checked(value)

    def test_disabled_observer_and_capture_validation(self):
        value = report.decode([1]+[0]*15, [0]*10, [], [], [])
        checked = report.validate(value, requested=False)
        report.validate_capture(dict(fixedAssetsRequested=False, fixedAssetsAvailable=True, fixedAssets=checked), False)
        for index in range(1, 16):
            bad = copy.deepcopy(value);bad['stats'][index] = 1
            with self.subTest(index=index), self.assertRaises(ValueError):report.validate(bad, requested=False)
        value, source, tail, borrowed = fixture();checked = self.checked()
        capture = dict(fixedAssetsRequested=True, fixedAssetsAvailable=True, fixedAssets=checked,
                       sourceStream=source, preparedTail=tail, borrowedTables=borrowed)
        report.validate_capture(capture, True)
        capture['fixedAssets']['executionPermission'] = True
        with self.assertRaises(ValueError):report.validate_capture(capture, True)

    def test_bounded_reader_checks_payload_bytes_twice(self):
        value, _, _, _ = fixture();exports = {};memory = {}
        exports[report.PREFIX+'FixedAssetsStats'] = struct.pack('<16Q', *value['stats'])
        exports[report.PREFIX+'FixedAssetsCounts'] = struct.pack('<10Q', *value['counts'])
        for index, (name, suffix, words, _) in enumerate(report.ARRAYS):
            pointer = 0x10000+index*0x10000;rows = value[name]
            exports[report.PREFIX+suffix+'Count'] = struct.pack('<I', len(rows))
            exports[report.PREFIX+suffix+'RowBytes'] = struct.pack('<I', words*8)
            exports[report.PREFIX+suffix+'Rows'] = struct.pack('<Q', pointer)
            memory[pointer] = b''.join(struct.pack(f'<{words}Q', *row) for row in rows)
        reads = []
        def read_memory(pointer, count):reads.append((pointer, count));return memory[pointer]
        output = report.read_report(lambda name, size:exports[name], read_memory)
        self.assertEqual(output, value);self.assertEqual(len(reads), 6)
        exports[report.PREFIX+'FixedAssetsCount'] = struct.pack('<I', 257)
        with self.assertRaises(ValueError):report.read_report(lambda name, size:exports[name], read_memory)
        exports[report.PREFIX+'FixedAssetsCount'] = struct.pack('<I', 1)
        def torn(pointer, count):
            result = bytearray(memory[pointer]);result[-1] = len(reads) % 2;reads.append(pointer);return bytes(result)
        with self.assertRaises(ValueError):report.read_report(lambda name, size:exports[name], torn)


if __name__ == '__main__':
    unittest.main()
