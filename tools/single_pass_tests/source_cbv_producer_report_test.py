"""Hermetic current-source producer joins; no process, script or GPU work."""
import unittest
import struct

from source_cbv_producer_report import DIRECT_COMPLETION, ROW_BYTES, analyze
from source_cbv_report import analyze as analyze_cbvs
from source_cbv_report_test import fixture as cbv_fixture, raw_rows, stats_for


def producer_rows(source, cbvs):
    rows = []
    for cbv in cbvs:
        original = source['rows'][cbv[0]]
        row = [0] * 24
        row[:7] = [*cbv[:6], cbv[22]]
        row[9:17] = [cbv[19], cbv[12], cbv[10], 0xE000 + cbv[0] * 0x100,
                      original[6], original[7], original[8], original[9]]
        if (cbv[2], cbv[3], cbv[7], cbv[19], cbv[21]) == (1, 5, 3, 0x1F2960, 0) and cbv[22]:
            row[7], row[8] = (cbv[22] + 255) & ~255, DIRECT_COMPLETION
            row[17:22] = row[12:17]
            row[22] = 1
        rows.append(row)
    return rows


def fixture():
    source, cbvs = cbv_fixture()
    cbvs[1][22] = 112
    report = analyze_cbvs(stats_for(cbvs, source), raw_rows(cbvs), source)
    return source, report, producer_rows(source, cbvs)


def check(data, raw=None):
    source, cbvs, rows = data
    return analyze(raw_rows(rows) if raw is None else raw, source, cbvs)


class ProducerReportTests(unittest.TestCase):
    def transient_fixture(self):
        source, cbv_report, old_rows=fixture()
        cbvs=cbv_report['rows']
        cbvs[2][7]=3
        cbv_report=analyze_cbvs(stats_for(cbvs,source),raw_rows(cbvs),source)
        rows=[row+[0]*16 for row in producer_rows(source,cbvs)]
        row=rows[2];row[7]=cbvs[2][11];row[8]=0x1F08C2;row[17:22]=row[12:17]
        row[24:27]=[77,cbvs[2][22],0xF00000];row[27:32]=row[12:17];row[32]=1
        return source,cbv_report,rows

    def test_exact_transient_writer_join_and_legacy_abi(self):
        source,cbvs,rows=self.transient_fixture()
        report=analyze(b''.join(struct.pack('<40Q',*row) for row in rows),source,cbvs)
        self.assertEqual(report['transientEligibleCount'],1)
        self.assertFalse(report['copyPermission'])
        legacy=fixture()
        self.assertEqual(check(legacy)['transientEligibleCount'],0)

    def test_transient_unknown_partial_foreign_and_escaped_producers_decline(self):
        for field,value in ((8,0x22010D),(24,0),(25,143),(26,0),(27,999),(29,999),(32,2),(33,1)):
            with self.subTest(field=field):
                source,cbvs,rows=self.transient_fixture();rows[2][field]=value
                with self.assertRaises(ValueError):
                    analyze(b''.join(struct.pack('<40Q',*row) for row in rows),source,cbvs)

    def test_exact_direct_join_has_no_permission_upgrade(self):
        data = fixture()
        report = check(data)
        self.assertEqual((report['sourceCommands'], report['cbvCount'], report['directEligibleCount']), (2, 5, 1))
        self.assertEqual(report['rows'], [row+[0]*16 for row in data[2]])
        self.assertTrue(report['producers'][1]['inlineDirectEligible'])
        for entry in [report, *report['producers']]:
            for key in ('copyPermission', 'inputContentsFrozen', 'nativeExecutionPermission', 'nativeExecutionPermitted', 'performanceGainProved'):
                self.assertIs(entry[key], False)

    def test_unknown_creation_and_legacy_zero_logical_stay_diagnostic(self):
        data = fixture()
        data[2][1][7:9] = [0, 0]
        data[2][1][17:23] = [0] * 6
        self.assertEqual(check(data)['directEligibleCount'], 0)
        source, rows = cbv_fixture()
        cbvs = analyze_cbvs(stats_for(rows, source), raw_rows(rows), source)
        self.assertEqual(analyze(raw_rows(producer_rows(source, rows)), source, cbvs)['directEligibleCount'], 0)

    def test_exact_membership_creator_source_and_current_lease(self):
        for field in (*range(7), *range(9, 17), 23):
            with self.subTest(field=field):
                data = fixture()
                data[2][1][field] += 1
                with self.assertRaises(ValueError):
                    check(data)
        for transform in (lambda rows: rows[:-1], lambda rows: rows + [rows[0][:]],
                          lambda rows: [rows[1], rows[0], *rows[2:]]):
            data = fixture()
            data = (data[0], data[1], transform(data[2]))
            with self.assertRaises(ValueError):
                check(data)

    def test_fake_creation_lease_size_completion_or_eligibility_rejected(self):
        for field, value in ((7, 0), (7, 255), (7, 512), (8, 0), (8, DIRECT_COMPLETION + 1), (22, 2)):
            data = fixture()
            data[2][1][field] = value
            with self.assertRaises(ValueError):
                check(data)
        for field in range(17, 22):
            data = fixture()
            data[2][1][field] += 1
            with self.assertRaises(ValueError):
                check(data)
        data = fixture()
        data[2][1][12] = data[2][1][17] = 0xFA00
        with self.assertRaises(ValueError):
            check(data)
        data = fixture()
        data[1]['rows'][1][22] = data[2][1][6] = 111
        with self.assertRaises(ValueError):
            check(data)
        data = fixture()
        data[2][2][22] = 1
        with self.assertRaises(ValueError):
            check(data)

    def test_verified_flag_and_aggregate_do_not_override_rows(self):
        for field in ('sourceCommands', 'cbvCount', 'ownedCount'):
            data = fixture()
            data[1][field] += 1
            with self.assertRaises(ValueError):
                check(data)
        data = fixture()
        data[1]['verified'] = False
        with self.assertRaises(ValueError):
            check(data)
        data = fixture()
        data[0].update(copyPermission=True, nativeExecutionPermitted=True)
        self.assertFalse(check(data)['copyPermission'])

    def test_exact_binary_extent_and_contiguous_abi(self):
        data = fixture()
        raw = raw_rows(data[2])
        self.assertEqual(len(raw), len(data[2]) * ROW_BYTES)
        for invalid in (b'', raw[:-1], raw + b'x', 'not bytes', memoryview(raw)[::2], memoryview(raw).cast('Q')):
            with self.assertRaises(ValueError):
                check(data, invalid)
        self.assertTrue(check(data, memoryview(raw))['verified'])


if __name__ == '__main__':
    unittest.main()
