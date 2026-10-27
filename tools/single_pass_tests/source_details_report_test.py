"""Hermetic 128Q source-detail fixtures; no artifacts, processes or GPU work."""

import struct
import unittest

from source_cbv_report import analyze as analyze_cbvs
from source_cbv_report_test import fixture as cbv_fixture, raw_rows, stats_for
from source_details_report import ROW_BYTES, analyze


def packed(rows):
    return b''.join(struct.pack('<128Q', *row) for row in rows)


def details_stats(rows, source):
    return [3, source['stats'][1], source['stats'][2], len(rows),
            sum(bool(row[7]) for row in rows), sum(not row[7] for row in rows),
            sum(row[8] == 1 for row in rows), sum(row[8] == 2 for row in rows),
            sum(row[8] == 5 for row in rows), sum(bool(row[18]) for row in rows),
            sum(bool(row[31]) for row in rows), sum(bool(row[64]) for row in rows),
            sum(bool(row[84]) for row in rows), 0, 0, 0]


def b0(row, cbv):
    raw = bytearray(0xB0)
    struct.pack_into('<I', raw, 0, cbv[22])
    raw[6] = (row[33] << 4) | 6
    struct.pack_into('<2Q', raw, 8, cbv[10], cbv[12])
    if row[33] == 4:
        struct.pack_into('<2Q', raw, 8, 0xDEADBEEF, 0x12345678)
    row[40:62] = struct.unpack('<22Q', raw)


def fixture():
    source, cbvs = cbv_fixture()
    cbv_report = analyze_cbvs(stats_for(cbvs, source), raw_rows(cbvs), source)
    rows = []
    for index, cbv in enumerate(cbvs):
        owner = source['rows'][cbv[0]]
        row = [0] * 128
        row[:8] = [index, *cbv[:6], 1]
        row[8] = 2 if cbv[7] in (2, 3) else 1
        row[11:18] = [cbv[14], cbv[13], cbv[10] - cbv[13], cbv[16], cbv[15], owner[11], owner[6]]
        row[24:31] = [1, 1, 0, 0, 1 if row[8] == 2 else 8, 0xAC3, 4 if cbv[7] == 2 else 1 if cbv[21] else 0]
        row[31:39] = [int(bool(cbv[21])), 0xF00000 + index * 0xB0 if cbv[21] else 0,
                      row[30], cbv[21], cbv[12], cbv[10], cbv[11], cbv[6]]
        if row[31]:
            b0(row, cbv)
        row[106:111] = [3, 3, 3, 3, 4 if row[33] == 4 else 0]
        row[118:124] = [1, 1, 0, 0, owner[9] if cbv[21] else 0, 0xE00000 + cbv[0] * 0x100]
        rows.append(row)
    return source, cbv_report, rows


class SourceDetailsReportTest(unittest.TestCase):
    def check(self, source, cbvs, rows, stats=None, raw=None):
        return analyze(details_stats(rows, source) if stats is None else stats,
                       packed(rows) if raw is None else raw, source, cbvs)

    def reject(self, source, cbvs, rows, stats=None, raw=None):
        with self.assertRaises((ValueError, RuntimeError)):
            self.check(source, cbvs, rows, stats, raw)

    def test_registered_default_created_upload_and_class4_arena(self):
        source, cbvs, rows = fixture()
        report = self.check(source, cbvs, rows)
        self.assertTrue(report['verified'])
        self.assertEqual(report['rows'], rows)
        self.assertEqual(report['heapTypes'], {1: 3, 2: 2})
        self.assertEqual(report['observations'][0]['heapType'], 1)
        self.assertEqual(report['observations'][1]['heapType'], 2)
        self.assertEqual(rows[1][31:35], [0, 0, 0, 0])
        self.assertEqual(rows[2][33], 4)
        self.assertEqual(rows[2][110], 4)
        self.assertFalse(report['observations'][0]['numericStateKnown'])
        for name in ('inputContentsFrozen', 'nativeExecutionPermission', 'copyPermission',
                     'allocationPinned', 'performanceGainProved'):
            self.assertIs(report[name], False)
        self.assertEqual(report['nativeCommandsSuppressed'], 0)
        for observation in report['observations']:
            self.assertTrue(observation['borrowedHeaderOnly'])
            for name in ('allocationPinned', 'inputContentsFrozen', 'copyPermission'):
                self.assertIs(observation[name], False)

    def test_exact_source_association_and_backing_identity(self):
        for field in (*range(7), *range(11, 18), *range(33, 39)):
            with self.subTest(field=field):
                source, cbvs, rows = fixture()
                rows[0][field] += 1
                self.reject(source, cbvs, rows)
        source, cbvs, rows = fixture()
        rows[0], rows[1] = rows[1], rows[0]
        self.reject(source, cbvs, rows)

    def test_raw_b0_matches_registered_but_arena_fields_are_not_owners(self):
        for offset, fmt, value in ((0, '<I', 1), (6, '<B', 0x15), (6, '<B', 0x26),
                                   (8, '<Q', 1), (16, '<Q', 1)):
            source, cbvs, rows = fixture()
            raw = bytearray(struct.pack('<22Q', *rows[0][40:62]))
            struct.pack_into(fmt, raw, offset, value)
            rows[0][40:62] = struct.unpack('<22Q', raw)
            self.reject(source, cbvs, rows)
        source, cbvs, rows = fixture()
        self.check(source, cbvs, rows)
        for field in (31, 32):
            source, cbvs, rows = fixture()
            rows[0][field] = 0
            self.reject(source, cbvs, rows)
        source, cbvs, rows = fixture()
        rows[1][40] = 1
        self.reject(source, cbvs, rows)
        source, cbvs, rows = fixture()
        rows[2][64:67] = [1, 0x500000, 0xAABB]
        rows[2][106] = 7
        self.reject(source, cbvs, rows)

    def test_unknown_creation_and_known_common_zero_are_distinct(self):
        source, cbvs, rows = fixture()
        self.assertIsNone(self.check(source, cbvs, rows)['observations'][0]['initialState'])
        rows[0][18:21] = [1, 1, 0]
        report = self.check(source, cbvs, rows)
        self.assertTrue(report['observations'][0]['initialStateKnown'])
        self.assertEqual(report['observations'][0]['initialState'], 0)
        rows[0][19] = 0
        self.assertIsNone(self.check(source, cbvs, rows)['observations'][0]['initialState'])
        rows[0][18:24] = [2, 1, 0, 0x400000, 0x2000, 0x4000]
        self.check(source, cbvs, rows)
        for values in ([0, 1, 0, 0, 0, 0], [1, 0, 1, 0, 0, 0],
                       [1, 1, 0, 1, 0, 0], [2, 1, 0, 0, 0, 4096],
                       [2, 1, 0, 1, 2**64 - 1, 4096], [1, 2, 0, 0, 0, 0]):
            source, cbvs, rows = fixture()
            rows[0][18:24] = values
            self.reject(source, cbvs, rows)

    def test_heap_state_context_and_unknown_numeric_state(self):
        for field, value in ((8, 0), (8, 6), (24, 0), (25, 3), (26, 2**32),
                             (27, 1), (28, 0), (28, 10), (123, 0)):
            source, cbvs, rows = fixture()
            rows[0][field] = value
            self.reject(source, cbvs, rows)
        for status in (3, 4, 5, 6, 7):
            source, cbvs, rows = fixture()
            rows[0][28:30] = [status, 0]
            self.check(source, cbvs, rows)
            rows[0][29] = 1
            self.reject(source, cbvs, rows)
        source, cbvs, rows = fixture()
        rows[0][28:30] = [2, 0]
        self.assertTrue(self.check(source, cbvs, rows)['observations'][0]['numericStateKnown'])
        rows[0][28:30] = [1, 0xAC3]
        self.reject(source, cbvs, rows)
        source, cbvs, rows = fixture()
        rows[1][29] = 0
        self.reject(source, cbvs, rows)

    def test_readable_unknown_abi_headers_are_borrowed_not_pinned(self):
        for status in (1, 7, 8):
            source, cbvs, rows = fixture()
            row = rows[0]
            row[64:79] = [1, 0x500000, *range(100, 113)]
            row[79:84] = [1, 0x600000, 11, 22, 33]
            row[96:106] = [1, 0x700000, 0x700100, *range(200, 207)]
            row[106:110] = [status, status, 3, status]
            report = self.check(source, cbvs, rows)
            observed = report['observations'][0]
            self.assertTrue(observed['allocationHeaderCaptured'])
            self.assertEqual(observed['allocationObservation'], status)
            self.assertFalse(observed['allocationPinned'])
            self.assertFalse(observed['copyPermission'])

    def test_unreadable_headers_pool_and_observation_presence(self):
        source, cbvs, rows = fixture()
        rows[0][106:110] = [4, 4, 4, 4]
        self.assertFalse(self.check(source, cbvs, rows)['observations'][0]['allocationHeaderCaptured'])
        for field, value in ((106, 7), (107, 1), (109, 8), (108, 1), (106, 9), (110, 5), (111, 2)):
            source, cbvs, rows = fixture()
            rows[0][field] = value
            self.reject(source, cbvs, rows)
        source, cbvs, rows = fixture()
        rows[4][84:91] = [1, 0x800000, 0x800100, 0, 0, 512, 1]
        rows[4][108] = 1
        self.assertTrue(self.check(source, cbvs, rows)['observations'][4]['poolKnown'])
        rows[4][89] = 256
        self.reject(source, cbvs, rows)

    def test_aggregate_reserved_and_binary_abi_failures(self):
        for field in range(16):
            source, cbvs, rows = fixture()
            stats = details_stats(rows, source)
            stats[field] += 1
            self.reject(source, cbvs, rows, stats=stats)
        for field in (7, 39, 62, 63, 91, 92, 95, 127):
            source, cbvs, rows = fixture()
            rows[0][field] = 0 if field == 7 else 1
            self.reject(source, cbvs, rows)
        source, cbvs, rows = fixture()
        self.assertEqual(len(packed(rows)), len(rows) * ROW_BYTES)
        for raw in (b'', packed(rows)[:-1], packed(rows) + b'x', 'not bytes',
                    memoryview(packed(rows))[::2]):
            self.reject(source, cbvs, rows, raw=raw)
        self.reject(source, {}, rows)


if __name__ == '__main__':
    unittest.main()
