"""Focused fixtures for conservative offline multilayer enumeration."""

import struct
import unittest

from analyze_multilayer_capture import decode_diagnostic, enumerate_records, intervals, plan, COPY_FIELDS


def b4(base, mask):
    result = bytearray(228)
    struct.pack_into('<f', result, 128, base)
    struct.pack_into('<I', result, 224, mask)
    return bytes(result)


class MultilayerCaptureTest(unittest.TestCase):
    def test_packed_indices_and_ten_sites(self):
        read_plan = plan(b4(3, 1), 2, 8192, (8192 + 2) * 128)
        payload = bytearray(128)
        struct.pack_into('<3I', payload, 100, 17, (32001 << 16) | 30002, (30003 << 16) | 32004)
        actual = enumerate_records(read_plan, payload)
        self.assertEqual(actual['candidateIndices'], [17, 30002, 30003, 32001, 32004])
        self.assertEqual(actual['evaluatedSites'], 10)
        self.assertEqual(read_plan['resourceOffset'], 640)
        self.assertTrue(actual['enumerationAdmissible'])

    def test_sparse_mask_ignores_copied_gap(self):
        read_plan = plan(b4(4.75, 5), 0, 8192, 8192 * 128)
        payload = bytearray(384)
        struct.pack_into('<3I', payload, 100, 8, (9 << 16) | 10, (11 << 16) | 12)
        struct.pack_into('<3I', payload, 228, 2**32 - 1, 2**32 - 1, 2**32 - 1)
        struct.pack_into('<3I', payload, 356, 13, (14 << 16) | 15, (16 << 16) | 17)
        actual = enumerate_records(read_plan, payload)
        self.assertEqual(read_plan['records'], [4, 6])
        self.assertEqual(actual['candidateIndices'], list(range(8, 18)))
        self.assertEqual(actual['sparseIntervals'], [dict(offset=8, count=10)])
        self.assertTrue(actual['enumerationAdmissible'])

    def test_zero_mask_needs_no_dynamic_table(self):
        read_plan = plan(b4(3, 0), 0, 8192, 8192 * 128)
        actual = enumerate_records(read_plan, b'')
        self.assertTrue(actual['zeroMask'])
        self.assertFalse(actual['dynamicTableRequired'])
        self.assertEqual(actual['fallbackSubstitutions'], 0)

    def test_out_of_range_declines_whole_enumeration(self):
        read_plan = plan(b4(3, 1), 0, 8192, 8192 * 128)
        payload = bytearray(128)
        struct.pack_into('<I', payload, 104, (32768 << 16) | 17)
        actual = enumerate_records(read_plan, payload)
        self.assertEqual(len(actual['outOfRangeSelections']), 2)
        self.assertFalse(actual['enumerationAdmissible'])
        self.assertEqual(actual['compactDescriptorCount'], 0)
        self.assertEqual(actual['fallbackSubstitutions'], 0)

    def test_domain_extent_and_exact_payload_guards(self):
        for base in (-1, float('inf'), float('nan'), 2**32):
            with self.assertRaises(ValueError):
                plan(b4(base, 1), 0, 8192, 8192 * 128)
        with self.assertRaises(ValueError):
            plan(b4(8191, 2), 0, 8192, 8192 * 128)
        with self.assertRaises(ValueError):
            plan(b4(3, 1), 1, 8192, 8192 * 128)
        with self.assertRaises(ValueError):
            enumerate_records(plan(b4(3, 1), 0, 8192, 8192 * 128), bytes(256))

    def test_intervals_keep_sparse_original_offsets(self):
        self.assertEqual(intervals([5, 2, 3, 5, 10]), [
            dict(offset=2, count=2), dict(offset=5, count=1), dict(offset=10, count=1)])

    def test_diagnostic_resource_and_checksum_guards(self):
        values = [0x4350494e54303031, 123, 1] + [0] * 21
        checksum = 14695981039346656037
        for byte in struct.pack('<23Q', *values[:23]):
            checksum = ((checksum ^ byte) * 1099511628211) & (2**64 - 1)
        values[23] = checksum
        row = [93, 0, 1, 0] + values + [0] * 4
        self.assertEqual(decode_diagnostic(row, COPY_FIELDS, values[0], 123)['resource'], 123)
        with self.assertRaises(ValueError):
            decode_diagnostic(row, COPY_FIELDS, values[0], 124)
        row[13] += 1
        with self.assertRaises(ValueError):
            decode_diagnostic(row, COPY_FIELDS, values[0], 123)


if __name__ == '__main__':
    unittest.main()
