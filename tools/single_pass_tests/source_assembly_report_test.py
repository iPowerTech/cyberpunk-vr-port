import copy
import struct
import unittest

from source_assembly_report import analyze, require_bank_created
from source_stream_report import analyze as analyze_source
from source_stream_report_test import fixture as source_fixture, raw as source_raw


def pack(rows, words):
    return b''.join(struct.pack(f'<{words}Q', *r) for r in rows)


def fixture():
    source_stats, sources, retained = source_fixture()
    source = analyze_source(source_stats, source_raw(sources), retained)
    commands, tables, missing, cbvs, selectors = [], [], [], [], []
    for ordinal, s in enumerate(sources):
        exact = ordinal < 2
        selector = [0] * 32
        selector[:3] = [ordinal, s[1], 1 if exact else 3]
        selector[5] = ordinal + 1
        selector[11] = int(exact)
        selector[13:18] = [s[6], s[7], s[8], s[11], s[12]]
        selector[24] = int(exact)
        selectors.append(selector)
        cbv = [0] * 32
        cbv[:8] = [ordinal, s[1], 1, 5, 33, 1, 112, 256]
        cbv[8], cbv[10], cbv[12], cbv[23], cbv[24] = 2, 2, 100 + ordinal, 200 + ordinal, 300 + ordinal
        cbvs.append(cbv)
        missing.append([ordinal, s[1], 1, 5, 0, 33, 1, 112, 256, 2, 1,
                        cbv[12], cbv[23], cbv[24], 0, 1])
        tables.append([ordinal, s[1], 33, 2, 1, 2, 400 + ordinal, 500 + ordinal, 1, 1, 1, 1, 0, 0, 0, 0])
        c = [0] * 32
        c[:11] = [ordinal, s[1], int(exact), 0 if exact else 9, 0, 1, 1, 2, 0, int(exact), 1]
        c[11:21] = [s[6], s[7], ordinal + 1, s[11], 1, selector[2], int(exact), 1, 0, 0]
        c[27:32] = [ordinal, 1, ordinal, 1, 1]
        commands.append(c)
    stats = [3, source_stats[1], 4, 2, 2, 0, 4, 4, 8, 0, 0, 9, 0, 0, 0, 0]
    return dict(stats=stats, commands=commands, tables=tables, missing=missing, source=source,
                snapshots=dict(verified=True, rows=cbvs), selectors=dict(verified=True, commands=selectors))


def finite_ready_fixture():
    source_stats, sources, retained = source_fixture()
    source = analyze_source(source_stats, source_raw(sources), retained)
    commands, tables, selectors = [], [], []
    for ordinal, s in enumerate(sources):
        selector = [0] * 32
        selector[:3] = [ordinal, s[1], 2]
        selector[5] = ordinal + 1
        selector[13:18] = [s[6], s[7], s[8], s[11], s[12]]
        selectors.append(selector)
        table = [ordinal, s[1], 33, 2, 1, 2, 400 + ordinal, 500 + ordinal,
                 600 + ordinal, 700 + ordinal, 800 + ordinal, 900 + ordinal,
                 1000 + ordinal, 0, 0, 0]
        tables.append(table)
        command = [0] * 32
        command[:11] = [ordinal, s[1], 1, 0, 1, 0, 1, 2, 0, 0, 0]
        command[11:21] = [s[6], s[7], ordinal + 1, s[11], 1, 2, 0, 1, 0, 0]
        command[21] = 1
        command[27:32] = [ordinal, 1, 0, 0, 0]
        commands.append(command)
    count = len(commands)
    stats = [3, source_stats[1], count, count, 0, count, 0, count, 2 * count, count, 0, 0, 0, 0, 0, 0]
    return dict(stats=stats, commands=commands, tables=tables, missing=[], source=source,
                snapshots=dict(verified=True, rows=[]), selectors=dict(verified=True, commands=selectors))


def shared_cbv_fixture():
    data = fixture()
    shared = []
    for cbv in data['snapshots']['rows']:
        pixel = cbv[:]
        pixel[2], pixel[6] = 5, 228
        shared.extend((cbv, pixel))
    data['snapshots']['rows'] = shared
    for missing in data['missing']:
        missing[7] = 228
    return data


def check(data, **overrides):
    return analyze(data['stats'], overrides.get('commands', pack(data['commands'], 32)),
                   overrides.get('tables', pack(data['tables'], 16)), overrides.get('missing', pack(data['missing'], 16)),
                   data['source'], data['snapshots'], data['selectors'])


class AssemblyReportTests(unittest.TestCase):
    def test_created_bank_does_not_grant_execution(self):
        data = finite_ready_fixture()
        for command, table in zip(data['commands'], data['tables']):
            command[22] = table[13] = 1
        data['stats'][10] = len(data['commands'])
        result = check(data)
        self.assertTrue(result['bankCreated'])
        self.assertEqual(result['bankSourceCount'], 4)
        self.assertFalse(result['privateTablesConsumed'])
        self.assertFalse(result['nativeExecutionPermitted'])
        result['bankStats'] = [1, 1, 4, 8, 0, 0]
        require_bank_created(result)
        for field, value in ((0, 0), (1, 0), (2, 3), (3, 1000001), (4, 0x80070057), (5, 1)):
            with self.subTest(bank_field=field):
                broken = copy.deepcopy(result)
                broken['bankStats'][field] = value
                with self.assertRaises(ValueError):
                    require_bank_created(broken)
        result['ready'] = 3
        with self.assertRaises(ValueError):
            require_bank_created(result)
        data['commands'][0][21] = 0
        with self.assertRaises(ValueError):
            check(data)

    def test_finite_ready_without_cbvs_selector_coverage_or_arena(self):
        result = check(finite_ready_fixture())
        self.assertEqual((result['buildSucceeded'], result['ready'], result['cpuAssembled'], result['missingCount']), (4, 4, 4, 0))
        self.assertTrue(all(not c[8] and not c[9] and not c[10] for c in result['commands']))
        self.assertFalse(result['bankCreated'])
        self.assertFalse(result['nativeExecutionPermitted'])

    def test_qualified_selector_does_not_force_dynamic_coverage(self):
        data = fixture()
        data['commands'][0][9] = 0
        self.assertTrue(check(data)['verified'])
        for field, value in ((5, 99), (13, 99), (14, 99), (16, 99), (23, 1), (24, 0)):
            with self.subTest(selector_field=field):
                data = fixture()
                data['selectors']['commands'][0][field] = value
                with self.assertRaises(ValueError):
                    check(data)

    def test_shared_vs_ps_cbv_is_one_maximum_sized_descriptor_domain(self):
        data = shared_cbv_fixture()
        result = check(data)
        self.assertEqual((result['missingCount'], result['missingUploadCount']), (4, 4))
        self.assertTrue(all(c[31] == 1 and c[8] == 0 for c in result['commands']))
        self.assertTrue(all(m[7] == 228 for m in result['missingCbvs']))
        for field, value in ((2, 5), (7, 112), (7, 229), (11, 999), (12, 999), (13, 999)):
            with self.subTest(missing_field=field, value=value):
                changed = shared_cbv_fixture()
                changed['missing'][0][field] = value
                with self.assertRaises(ValueError):
                    check(changed)
        data['commands'][0][31] = 2
        with self.assertRaises(ValueError):
            check(data)

    def test_shared_cbv_backing_labels_must_agree_across_stages(self):
        for field in (3, 7, 8, 10, 11, 12, 13, 14, 23, 24, 25, 26):
            with self.subTest(snapshot_field=field):
                data = shared_cbv_fixture()
                data['snapshots']['rows'][1][field] += 1
                with self.assertRaises(ValueError):
                    check(data)

    def test_private_copy_count_remains_stage_specific(self):
        data = shared_cbv_fixture()
        for cbv in data['snapshots']['rows']:
            cbv[8], cbv[10] = 1, 1
        for command, missing in zip(data['commands'], data['missing']):
            command[2], command[3] = 0, 10
            command[8] = 2
            missing[9], missing[10], missing[15] = 1, 2, 0
        data['stats'][3], data['stats'][4], data['stats'][11] = 0, 4, 10
        self.assertTrue(check(data)['verified'])
        data['commands'][0][8] = 1
        with self.assertRaises(ValueError):
            check(data)

    def test_private_cbv_ready_requires_finalized_arena(self):
        data = finite_ready_fixture()
        _, sources, _ = source_fixture()
        cbvs = []
        for ordinal, s in enumerate(sources):
            cbv = [0] * 32
            cbv[:8] = [ordinal, s[1], 1, 5, 33, 1, 112, 256]
            cbv[8], cbv[10], cbv[12], cbv[23], cbv[24] = 1, 1, 100 + ordinal, 200 + ordinal, 300 + ordinal
            cbvs.append(cbv)
            data['commands'][ordinal][8] = data['commands'][ordinal][31] = 1
            data['commands'][ordinal][10] = 1
        data['snapshots']['rows'] = cbvs
        self.assertTrue(check(data)['verified'])
        data['commands'][0][10] = 0
        with self.assertRaises(ValueError):
            check(data)

    def test_missing_upload_stays_nonready_and_has_no_execution_grant(self):
        result = check(fixture())
        self.assertEqual((result['ready'], result['missingUploadCount'], result['buildSucceeded']), (0, 4, 2))
        self.assertFalse(result['bankCreated'])
        self.assertFalse(result['nativeExecutionPermitted'])

    def test_reject_other_scope_and_execution(self):
        for field, value in ((0, 9), (1, 1), (11, 2), (12, 2), (13, 2), (14, 2),
                             (22, 1), (23, 1), (24, 1), (25, 1), (31, 0)):
            with self.subTest(field=field):
                data = fixture()
                data['commands'][0][field] = value
                with self.assertRaises(ValueError):
                    check(data)

    def test_reject_missing_or_unqualified_cbv(self):
        for field, value in ((2, 5), (5, 34), (7, 228), (9, 1), (11, 999), (12, 999), (14, 1), (15, 0)):
            data = fixture()
            data['missing'][0][field] = value
            with self.assertRaises(ValueError):
                check(data)
        data = fixture()
        data['commands'][0][5] = data['commands'][0][30] = 0
        with self.assertRaises(ValueError):
            check(data)

    def test_reject_premature_readiness_or_dynamic_coverage(self):
        for ordinal, field in ((0, 4), (2, 9), (2, 17), (0, 21)):
            data = fixture()
            data['commands'][ordinal][field] = 1
            with self.assertRaises(ValueError):
                check(data)
        data = fixture()
        data['snapshots']['verified'] = False
        with self.assertRaises(ValueError):
            check(data)

    def test_reject_table_owner_extents_and_unrelated_rows(self):
        for field, value in ((2, 31), (3, 1), (4, 3), (6, 0), (7, 0), (8, 0), (9, 0), (10, 0), (11, 0), (12, 1), (15, 1)):
            data = fixture()
            data['tables'][0][field] = value
            with self.assertRaises(ValueError):
                check(data)
        data = fixture()
        data['tables'].append(copy.deepcopy(data['tables'][0]))
        with self.assertRaises(ValueError):
            check(data)

    def test_reject_stats_capacity_and_partial_abi(self):
        for field in range(16):
            data = fixture()
            data['stats'][field] += 1
            with self.assertRaises(ValueError):
                check(data)
        with self.assertRaises(ValueError):
            check(fixture(), commands=b'\0')
        with self.assertRaises(ValueError):
            check(fixture(), missing=b'\0' * (65537 * 128))


if __name__ == '__main__':
    unittest.main()
