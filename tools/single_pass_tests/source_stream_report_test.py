import struct
import unittest
from source_stream_report import analyze


def fixture():
    rows = []
    for i, (side, kind) in enumerate(((0, 0), (0, 1), (1, 0), (1, 2))):
        r = [0] * 48
        r[0:4] = [i, side, kind, 1]
        for field in (6, 7, 8, 9, 10, 11, 12, 15, 16, 36, 37, 42):
            r[field] = 1
        r[13] = 1 << 33
        r[40] = side
        r[43] = 31
        if kind:
            r[29:34] = [1, 32, 1, 1, 4]
        rows.append(r)
    stats = [3, 1, 4, 2, 2, 2, 2, 4, 4, 4, 4, 4, 0, 0, 0, 0]
    retained = dict(stats=dict(mainDraws=2, peerDraws=2), indirect=[1, 1])
    return stats, rows, retained


def raw(rows):
    return b''.join(struct.pack('<48Q', *r) for r in rows)


class SourceStreamTests(unittest.TestCase):
    def test_complete_order_and_high_root_tables(self):
        stats, rows, retained = fixture()
        report = analyze(stats, raw(rows), retained)
        self.assertEqual(report['commands'], 4)
        self.assertEqual(report['graphicsTableMasks'], [1 << 33])
        self.assertFalse(report['inputContentsFrozen'])
        self.assertFalse(report['nativeExecutionPermitted'])

    def test_reject_unknown_order_bindings_and_parent(self):
        for field, value in ((0, 9), (1, 2), (2, 3), (3, 2), (7, 0), (11, 0), (36, 0),
                             (40, 1), (41, 220), (43, 15), (44, 902), (47, 1)):
            with self.subTest(field=field):
                stats, rows, retained = fixture()
                rows[0][field] = value
                with self.assertRaises((ValueError, RuntimeError)):
                    analyze(stats, raw(rows), retained)

    def test_no_masked_incomplete_or_permission_stats(self):
        for field, value in ((0, 2), (2, 3), (3, 1), (4, 1), (5, 1), (8, 3), (11, 3),
                             (12, 1), (13, 1), (14, 1), (15, 1)):
            with self.subTest(field=field):
                stats, rows, retained = fixture()
                stats[field] = value
                with self.assertRaises((ValueError, RuntimeError)):
                    analyze(stats, raw(rows), retained)

    def test_indirect_stride_offset_and_total_coverage(self):
        for field, value in ((29, 0), (30, 0), (32, 0), (33, 3), (35, 3)):
            stats, rows, retained = fixture()
            rows[1][field] = value
            with self.assertRaises(ValueError):
                analyze(stats, raw(rows), retained)
        stats, rows, retained = fixture()
        retained['stats']['mainDraws'] = 3
        with self.assertRaises(RuntimeError):
            analyze(stats, raw(rows), retained)
        stats, rows, retained = fixture()
        retained['indirect'] = [1, 0]
        with self.assertRaises(RuntimeError):
            analyze(stats, raw(rows), retained)


if __name__ == '__main__':
    unittest.main()
