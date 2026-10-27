import struct
import unittest

from indirect_binding_report import analyze, GENERIC_READ, RECORD_BASE, ROW_BYTES


def fixture():
    source_rows = []
    for ordinal, (side, kind, caller) in enumerate(((0, 0, 0), (0, 1, 0x623CC1),
                                                    (1, 2, 0x62462F), (1, 1, 0x624830))):
        source = [0] * 48
        source[0:4] = [ordinal, side, kind, 7]
        source[6:10] = [0x10000 + side * 0x100, 25 + side, 0x20000 + side * 0x100, 0x50000000]
        source[43] = 31
        if kind:
            source[29:36] = [0x40000 + ordinal * 0x100, 20 if kind == 1 else 16,
                             1, 0x60000 + ordinal * 0x100, 64 if ordinal == 1 else 0, 0, 0]
            source[45] = caller
        source_rows.append(source)
    source_stats = [3, 7, 4, 1, 3, 2, 2, 4, 4, 4, 4, 4, 0, 0, 0, 0]
    source_report = dict(stats=source_stats, rows=source_rows, commands=4, direct=1, indirect=3,
                         inputContentsFrozen=False, nativeCommandsSuppressed=0, nativeExecutionPermitted=False)
    rows = []
    for index, source in enumerate(source_rows[1:]):
        row = [0] * 56
        row[0:4] = [source[0], source[1], source[45], 1]
        row[5:10] = [11 + index, 2 if index == 0 else 1,
                     0x200 if index == 0 else 0 if index == 1 else GENERIC_READ,
                     2 if index == 0 else 5 if index == 1 else 8,
                     2 if index == 0 else 1]
        row[12:18] = [0x100000 + index * 0x1000, 128 if index == 0 else 256 if index == 1 else 20,
                      source[33], source[30], 1, 4 if index == 1 else 0]
        row[10] = row[12] + (64 if index == 0 else 0)
        row[11] = 20 if index == 0 else row[13]
        row[18:28] = [source[2], source[30], source[31], source[7], source[8], source[6],
                      source[9], source[32], int(row[10] == row[12] and row[11] == row[13]), 1]
        row[4] = row[24] + RECORD_BASE + (row[5] - 1) * 0xB0
        record = bytearray(0xB0)
        struct.pack_into('<I', record, 0, row[11])
        record[4:8] = bytes((0x12, 0x34, (row[6] << 4) | 0xB, 0x56))
        struct.pack_into('<QQ', record, 8, row[10], row[25])
        for offset in (0x18, 0x20, 0x28, 0x40, 0x58):
            struct.pack_into('<Q', record, offset, 0xABC0000000000000 + offset + index)
        struct.pack_into('<I', record, 0x68, 0x80000000 + index)
        row[32:54] = struct.unpack('<22Q', record)
        rows.append(row)
    stats = [3, 7, 3, 3, 0, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0]
    return stats, rows, source_report


def raw(rows):
    return b''.join(struct.pack('<56Q', *row) for row in rows)


def update_record(row, offset, fmt, value):
    record = bytearray(struct.pack('<22Q', *row[32:54]))
    struct.pack_into(fmt, record, offset, value)
    row[32:54] = struct.unpack('<22Q', record)


class IndirectBindingReportTests(unittest.TestCase):
    def test_exact_order_ranges_and_raw_metadata(self):
        stats, rows, source = fixture()
        report = analyze(stats, raw(rows), source)
        self.assertTrue(report['verified'])
        self.assertEqual(report['rows'], rows)
        self.assertEqual(report['sourceIndirectCount'], 3)
        self.assertEqual(report['fullBackingCount'], 2)
        self.assertEqual(report['twentyByteArgumentCount'], 2)
        self.assertEqual(report['trackedStateCount'], 1)
        self.assertEqual(report['unknownStateCount'], 1)
        self.assertEqual(report['actualCallers'], [0x623CC1, 0x62462F, 0x624830])
        first = report['bindings'][0]
        self.assertEqual(first['argumentGPU'], first['logicalGPU'])
        self.assertFalse(first['fullBacking'])
        self.assertEqual(first['rawRecord']['rawWords'], rows[0][32:54])
        self.assertEqual(len(first['rawRecord']['rawHex']), 0xB0 * 2)
        for offset in (0x18, 0x20, 0x28, 0x40, 0x58):
            self.assertEqual(first['rawRecord']['metadata'][f'qword{offset:02x}'], 0xABC0000000000000 + offset)
        self.assertEqual(first['rawRecord']['dword68'], 0x80000000)

    def test_no_permission_upgrade_even_full_backing_or_known_state(self):
        stats, rows, source = fixture()
        source.update(copyPermission=True, inputContentsFrozen=True, nativeSuppression=999,
                      nativeExecutionPermitted=True, performanceGainProved=True)
        report = analyze(stats, raw(rows), source)
        for object_ in [report, *report['bindings'], *(binding['state'] for binding in report['bindings'])]:
            self.assertFalse(object_['copyPermission'])
        self.assertFalse(report['inputContentsFrozen'])
        self.assertFalse(report['nativeExecutionPermitted'])
        self.assertFalse(report['performanceGainProved'])
        self.assertEqual(report['nativeSuppression'], 0)
        self.assertEqual(report['nativeCommandsSuppressed'], 0)
        self.assertFalse(report['bindings'][1]['state']['known'])
        self.assertTrue(report['bindings'][1]['state']['informationOnly'])

    def test_extent_and_header_shape(self):
        stats, rows, source = fixture()
        for malformed in (raw(rows)[:-1], raw(rows) + bytes(ROW_BYTES), b'', 'not bytes'):
            with self.subTest(raw=type(malformed)):
                with self.assertRaises(ValueError):
                    analyze(stats, malformed, source)
        for field, value in ((0, 0), (1, 0), (2, 2049), (8, 1), (15, 1), (2, True)):
            with self.subTest(field=field):
                changed = stats.copy()
                changed[field] = value
                with self.assertRaises((ValueError, RuntimeError)):
                    analyze(changed, raw(rows), source)
        with self.assertRaises(ValueError):
            analyze(stats[:-1], raw(rows), source)

    def test_every_source_association(self):
        for field, value in ((0, 2), (1, 1), (2, 0x624830), (14, 0), (20, 2),
                             (21, 99), (22, 0xB0000), (23, 0xB1000), (24, 0xB2000), (25, 0xB3000)):
            with self.subTest(field=field):
                stats, rows, source = fixture()
                rows[0][field] = value
                with self.assertRaises((ValueError, RuntimeError)):
                    analyze(stats, raw(rows), source)
        stats, rows, source = fixture()
        rows.reverse()
        with self.assertRaises(ValueError):
            analyze(stats, raw(rows), source)

    def test_source_header_count_and_ownership(self):
        for mutation in ('frame', 'count', 'indirect', 'owned', 'order', 'kind', 'generation', 'caller', 'reserved'):
            with self.subTest(mutation=mutation):
                stats, rows, source = fixture()
                if mutation == 'frame':
                    source['stats'][1] += 1
                elif mutation == 'count':
                    source['stats'][2] -= 1
                elif mutation == 'indirect':
                    source['indirect'] -= 1
                elif mutation == 'owned':
                    source['rows'][1][43] = 15
                elif mutation == 'order':
                    source['rows'][1][0] = 8
                elif mutation == 'kind':
                    source['rows'][1][2] = 3
                elif mutation == 'generation':
                    source['rows'][1][7] += 1
                elif mutation == 'caller':
                    source['rows'][1][45] = 0
                else:
                    source['stats'][15] = 1
                with self.assertRaises((ValueError, RuntimeError)):
                    analyze(stats, raw(rows), source)

    def test_raw_record_torn_fields(self):
        for offset, fmt, value in ((0, '<I', 19), (6, '<B', 0x2A), (6, '<B', 0x3B),
                                   (8, '<Q', 0x1000), (0x10, '<Q', 0x9000)):
            with self.subTest(offset=offset, value=value):
                stats, rows, source = fixture()
                update_record(rows[0], offset, fmt, value)
                with self.assertRaises(ValueError):
                    analyze(stats, raw(rows), source)
        for field, value in ((4, 1), (5, 0), (5, 0x100001), (6, 16), (28, 1), (31, 1), (54, 1), (55, 1)):
            stats, rows, source = fixture()
            rows[0][field] = value
            with self.assertRaises(ValueError):
                analyze(stats, raw(rows), source)

    def test_logical_and_backing_extents(self):
        for field, value in ((10, 0), (11, 0), (12, 0), (13, 0), (10, 1),
                             (12, 2**64 - 16), (13, 32), (15, 21), (26, 1), (26, 2)):
            with self.subTest(field=field):
                stats, rows, source = fixture()
                rows[0][field] = value
                with self.assertRaises((ValueError, RuntimeError)):
                    analyze(stats, raw(rows), source)
        stats, rows, source = fixture()
        rows[0][14] = source['rows'][1][33] = 60
        with self.assertRaises(ValueError):
            analyze(stats, raw(rows), source)
        stats, rows, source = fixture()
        rows[0][14] = source['rows'][1][33] = 68
        with self.assertRaises(ValueError):
            analyze(stats, raw(rows), source)

    def test_sites_signatures_and_count_operands(self):
        for field, value in ((2, 0x623CC2), (18, 2), (19, 32), (20, 2), (17, 2**32)):
            stats, rows, source = fixture()
            rows[0][field] = value
            source_field = {2: 45, 18: 2, 19: 30, 20: 31}.get(field)
            if source_field is not None:
                source['rows'][1][source_field] = value
            with self.assertRaises(ValueError):
                analyze(stats, raw(rows), source)
        for field in (34, 35):
            stats, rows, source = fixture()
            source['rows'][1][field] = 4
            with self.assertRaises(ValueError):
                analyze(stats, raw(rows), source)

    def test_state_information_and_unknown_rejection(self):
        for status in (0, 3, 4, 6, 7, 10):
            stats, rows, source = fixture()
            rows[0][8] = status
            with self.assertRaises(ValueError):
                analyze(stats, raw(rows), source)
        for field, value in ((7, 0), (7, 4), (7, 0x1200), (7, 2**32), (9, 1), (16, 2)):
            stats, rows, source = fixture()
            rows[0][field] = value
            with self.assertRaises(ValueError):
                analyze(stats, raw(rows), source)
        stats, rows, source = fixture()
        rows[1][7] = 0x200
        with self.assertRaises(ValueError):
            analyze(stats, raw(rows), source)

    def test_aggregate_and_revalidation_require_all_occurrences(self):
        for field, value in ((0, 1), (0, 2), (0, 4), (3, 2), (4, 1), (5, 1), (6, 1), (7, 0)):
            stats, rows, source = fixture()
            stats[field] = value
            with self.assertRaises(RuntimeError):
                analyze(stats, raw(rows), source)
        for field in (3, 27):
            stats, rows, source = fixture()
            rows[0][field] = 0
            with self.assertRaises(RuntimeError):
                analyze(stats, raw(rows), source)
        stats, rows, source = fixture()
        rows[2][27] = 0
        with self.assertRaises(RuntimeError):
            analyze(stats, raw(rows), source)


if __name__ == '__main__':
    unittest.main()
