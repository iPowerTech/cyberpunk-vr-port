"""Pure ABI tests; no native process, D3D12 or GPU required."""

import copy
import struct
import unittest

from source_cbv_report import ROW_BYTES, STATUS_NAMES, analyze


def source_report():
    rows = []
    for ordinal in range(2):
        row = [0] * 48
        row[0:4] = [ordinal, ordinal, 0, 5]
        row[6:13] = [0x1000 + ordinal * 0x100, 12 + ordinal, 0x2000 + ordinal * 0x100,
                     0x3000, 0x4000, 0x5000, 100 + ordinal]
        row[13] = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 19)
        row[15:17] = [2, 4]
        row[36:43] = [0x6000, 0x7000, 3, 0, ordinal, 1, 0x8000]
        row[43] = 31
        rows.append(row)
    stats = [3, 5, 2, 2, 0, 1, 1, 2, 2, 2, 2, 2, 0, 0, 0, 0]
    return dict(stats=stats, rows=rows, commands=2, direct=2, indirect=0)


def cbv(source, stage, register, parameter, offset, logical, status, address):
    row = [0] * 24
    row[:8] = [source[0], source[1], stage, register, parameter, offset, logical, status]
    descriptor_bytes = (logical + 255) & ~255
    row[8:15] = [0x9000 + register * 32, 0x91, address, descriptor_bytes,
                 0xA000 + source[0] * 0x100 + register * 8, address, max(2048, descriptor_bytes)]
    row[15:21] = [source[8], source[7], 0xB0 + register,
                  0xC0 + register if status in (2, 3) else 0,
                  0x1F2960 if status == 3 else 0x1F0BF7 if status == 2 else 0,
                  0xD000 + register * 32]
    row[21:24] = [0 if status == 3 else 100 + register, 0 if status == 3 else logical, 1]
    return row


def fixture():
    source = source_report()
    rows = [
        cbv(source['rows'][0], 1, 1, 0, 1, 848, 1, 0x100000),
        cbv(source['rows'][0], 1, 5, 2, 0, 112, 3, 0x110000),
        cbv(source['rows'][0], 5, 4, 19, 0, 144, 2, 0x120000),
        cbv(source['rows'][1], 1, 1, 0, 1, 848, 1, 0x130000),
        cbv(source['rows'][1], 1, 5, 2, 0, 112, 1, 0x140000),
    ]
    return source, rows


def stats_for(rows, source, state=3):
    stats = [state, source['stats'][1], source['stats'][2], len(rows)] + [0] * 12
    for row in rows:
        stats[4 if row[23] else 5] += 1
        stats[6 if row[2] == 1 else 7] += 1
        if row[23] and row[7] in (1, 2, 3):
            stats[7 + row[7]] += 1
        elif not stats[11]:
            stats[11] = row[7]
    return stats


def raw_rows(rows):
    return b''.join(struct.pack('<24Q', *row) for row in rows)


class SourceCbvReportTest(unittest.TestCase):
    def reject(self, rows=None, source=None, stats=None, raw=None):
        original_source, original_rows = fixture()
        source = original_source if source is None else source
        rows = original_rows if rows is None else rows
        stats = stats_for(rows, source) if stats is None else stats
        raw = raw_rows(rows) if raw is None else raw
        with self.assertRaises((ValueError, RuntimeError)):
            analyze(stats, raw, source)

    def test_valid_distinct_reflected_and_padded_footprints(self):
        source, rows = fixture()
        result = analyze(stats_for(rows, source), raw_rows(rows), source)
        self.assertTrue(result['verified'])
        self.assertEqual([result[key] for key in ('ownedCount', 'registeredCount', 'transientCount', 'createdCount')], [5, 3, 1, 1])
        self.assertEqual(result['cbvs'][0]['reflectedLogicalBytes'], 848)
        self.assertEqual(result['cbvs'][0]['descriptorBytes'], 1024)
        self.assertEqual(result['cbvs'][1]['reflectedLogicalBytes'], 112)
        self.assertEqual(result['cbvs'][1]['descriptorBytes'], 256)
        self.assertEqual(result['cbvs'][1]['nativeHandle'], 0)
        self.assertEqual(result['rows'], rows)
        self.assertEqual(result['cbvs'][0]['rootFromSource'], source['rows'][0][11])
        self.assertFalse(result['rootPipelineIdentityIndependentlyCompared'])

    def test_native_logical_extent_is_information_not_cbv_view_bound(self):
        source, rows = fixture()
        rows[0][22] = 4096
        result = analyze(stats_for(rows, source), raw_rows(rows), source)
        self.assertEqual(result['cbvs'][0]['logicalNativeBytes'], 4096)
        self.assertFalse(result['cbvs'][0]['nativeLogicalBytesWithinBackingWidth'])
        self.assertTrue(result['cbvs'][0]['nativeLogicalExtentInformationOnly'])

    def test_physical_extent_and_descriptor_alignment(self):
        for field, value in ((6, 0), (6, 65537), (6, 1025), (10, 0), (10, 0x100001),
                             (11, 0), (11, 1023), (11, 65792), (13, 0), (14, 0),
                             (13, 0x100100), (14, 1023), (13, 2**64 - 1024),
                             (10, 2**64 - 256)):
            with self.subTest(field=field, value=value):
                source, rows = fixture()
                rows[0][field] = value
                self.reject(rows, source)
        source, rows = fixture()
        rows[0][10] += 1024
        result = analyze(stats_for(rows, source), raw_rows(rows), source)
        self.assertEqual(result['cbvs'][0]['physicalOffset'], 1024)

    def test_source_association_context_generation_eye_order(self):
        for field, value in ((0, 2), (1, 1), (15, 0x2001), (16, 99)):
            with self.subTest(field=field):
                source, rows = fixture()
                rows[0][field] = value
                self.reject(rows, source)
        source, rows = fixture()
        rows = rows[3:] + rows[:3]
        self.reject(rows, source)

    def test_slot_visibility_type_and_recording_table_mask(self):
        for field, value in ((2, 0), (2, 2), (3, 14), (4, 64), (5, 2**32), (23, 2)):
            with self.subTest(field=field):
                source, rows = fixture()
                rows[0][field] = value
                self.reject(rows, source)
        source, rows = fixture()
        source['rows'][0][13] &= ~1
        self.reject(rows, source)

    def test_every_status_must_close_owned_receipt(self):
        for status in (0, *range(4, len(STATUS_NAMES))):
            for owned in (0, 1):
                with self.subTest(status=status, owned=owned):
                    source, rows = fixture()
                    rows[0][7], rows[0][23] = status, owned
                    self.reject(rows, source)
        source, rows = fixture()
        rows[0][7] = len(STATUS_NAMES)
        self.reject(rows, source)
        source, rows = fixture()
        rows[0][23] = 0
        self.reject(rows, source)
        self.reject(stats=stats_for(rows, source, state=4), rows=rows, source=source)
        for index in (1, 2):
            source, rows = fixture()
            rows[index][23] = 0
            self.reject(rows, source)

    def test_native_handle_creator_and_origin_rules(self):
        for field, value in ((8, 0), (9, 0), (12, 0), (17, 0), (20, 0),
                             (21, 0), (21, 0x100001), (22, 0), (22, 2**32), (19, 0x123456)):
            with self.subTest(field=field):
                source, rows = fixture()
                rows[0][field] = value
                self.reject(rows, source)
        for index in (1, 2):
            for field in (18, 19):
                source, rows = fixture()
                rows[index][field] = 0
                self.reject(rows, source)
        source, rows = fixture()
        rows[0][18], rows[0][19] = 123, 0x1F01A6
        self.assertTrue(analyze(stats_for(rows, source), raw_rows(rows), source)['verified'])
        for field in (21, 22):
            source, rows = fixture()
            rows[2][field] = 0
            self.reject(rows, source)
        source, rows = fixture()
        rows[1][21] = 1
        self.reject(rows, source)
        source, rows = fixture()
        rows[1][19] = 0x1F0BF7
        self.reject(rows, source)
        rows[1][21], rows[1][22] = 18, 112
        self.assertTrue(analyze(stats_for(rows, source), raw_rows(rows), source)['verified'])

    def test_direct_creator_can_report_real_logical_bytes_without_a_native_handle(self):
        source, rows = fixture()
        rows[1][22] = 112
        result = analyze(stats_for(rows, source), raw_rows(rows), source)
        self.assertEqual((result['cbvs'][1]['nativeHandle'], result['cbvs'][1]['logicalNativeBytes']), (0, 112))
        self.assertFalse(result['copyPermission'])
        rows[1][22] = 2**32
        self.reject(rows, source)

    def test_duplicate_and_missing_mandatory_camera_rows(self):
        source, rows = fixture()
        rows.insert(1, rows[0][:])
        self.reject(rows, source)
        for index in (0, 1, 3, 4):
            source, rows = fixture()
            del rows[index]
            self.reject(rows, source)
        source, _ = fixture()
        self.reject([], source)

    def test_registered_setter_origin_without_creator(self):
        source, rows = fixture()
        rows[0][18] = 123
        result = analyze(stats_for(rows, source), raw_rows(rows), source)
        self.assertEqual(result['cbvs'][0]['originId'], 123)
        self.assertEqual(result['cbvs'][0]['creatorRva'], 0)
        self.assertEqual(result['cbvs'][0]['statusName'], 'Registered')
        rows[0][18], rows[0][19] = 0, 0x1F01A6
        self.reject(rows, source)

    def test_raw_extent_and_contiguous_binary_type(self):
        source, rows = fixture()
        stats = stats_for(rows, source)
        raw = raw_rows(rows)
        self.assertEqual(len(raw), len(rows) * ROW_BYTES)
        for invalid in (raw[:-1], raw + bytes(8), raw[:ROW_BYTES], 'not bytes', [0] * len(raw),
                        memoryview(raw)[::2], memoryview(raw).cast('Q')):
            with self.subTest(type=type(invalid)):
                self.reject(rows, source, stats, invalid)
        self.assertTrue(analyze(stats, memoryview(raw), source)['verified'])
        self.assertTrue(analyze(stats, bytearray(raw), source)['verified'])

    def test_header_extent_frame_and_aggregate(self):
        source, rows = fixture()
        stats = stats_for(rows, source)
        for field in range(16):
            with self.subTest(field=field):
                changed = stats[:]
                changed[field] += 1
                self.reject(rows, source, changed)
        for state in (0, 1, 2, 4, 5):
            changed = stats[:]
            changed[0] = state
            self.reject(rows, source, changed)
        self.reject(rows, source, stats[:-1])
        changed = stats[:]
        changed[0] = True
        self.reject(rows, source, changed)

    def test_source_ownership_and_aggregate_not_trusted_by_flags(self):
        source, rows = fixture()
        for field in (6, 7, 8, 9, 10, 11, 12, 43):
            changed = copy.deepcopy(source)
            changed['rows'][0][field] = 0
            self.reject(rows, changed)
        for field in (0, 1, 2, 3, 4, 5, 7, 11, 12, 15):
            changed = copy.deepcopy(source)
            changed['stats'][field] += 1
            self.reject(rows, changed)
        for key in ('commands', 'direct', 'indirect'):
            changed = copy.deepcopy(source)
            changed[key] += 1
            self.reject(rows, changed)
        self.reject(rows, {}, stats_for(rows, source))

    def test_no_permission_upgrade_even_when_input_claims_it(self):
        source, rows = fixture()
        source.update(inputContentsFrozen=True, nativeExecutionPermission=True, performanceGain=True)
        result = analyze(stats_for(rows, source), raw_rows(rows), source)
        for entry in (result, *result['cbvs']):
            for key in ('inputContentsFrozen', 'nativeExecutionPermission', 'nativeExecutionPermitted',
                        'performanceGain', 'performanceGainProved', 'copyPermission'):
                self.assertIs(entry[key], False)
            for key in ('nativeSuppression', 'nativeCommandsSuppressed', 'suppressed'):
                self.assertEqual(entry[key], 0)
        self.assertFalse(result['allReflectedCbvUsesIndependentlyEnumerated'])


if __name__ == '__main__':
    unittest.main()
