"""Pure source-table ABI checks, without native processes or GPU work."""

import copy
import struct
import unittest

from source_cbv_report import analyze as analyze_cbvs
from source_cbv_report_test import fixture as cbv_fixture, raw_rows as cbv_raw, stats_for as cbv_stats
from source_tables_report import analyze


def packed(rows, words):
    return b''.join(struct.pack(f'<{words}Q', *row) for row in rows)


def table_stats(commands, tables, source):
    unique = {(row[6], row[7]): row for row in tables}
    return [3, source['stats'][1], len(source['rows']), len(commands),
            sum(row[2] == 1 for row in commands), sum(row[2] != 1 for row in commands),
            len(tables), sum(row[3] for row in tables), len(unique),
            sum(row[3] for row in unique.values()), sum(row[9] for row in commands),
            sum(row[10] for row in commands), 0, 0, 0, 0]


def fixture(count=2):
    original, original_cbvs = cbv_fixture()
    sources, cbvs = [], []
    for ordinal in range(count):
        side = ordinal if count == 2 else int(ordinal >= 176)
        source = copy.deepcopy(original['rows'][side])
        source[0] = ordinal
        source[13] |= (1 << 29) | (1 << 63)
        sources.append(source)
        for cbv in original_cbvs:
            if cbv[0] == side:
                cbv = copy.deepcopy(cbv)
                cbv[0] = ordinal
                cbvs.append(cbv)
    main = sum(row[1] == 0 for row in sources)
    source = dict(stats=[3, 5, count, count, 0, main, count - main,
                         count, count, count, count, count, 0, 0, 0, 0],
                  rows=sources, commands=count, direct=count, indirect=0)
    cbv_report = analyze_cbvs(cbv_stats(cbvs, source), cbv_raw(cbvs), source)
    tables, commands = [], []
    for ordinal, source_row in enumerate(sources):
        owned = []
        for parameter, extent in ((0, 4), (2, 2), (19, 1), (29, 8), (63, 3)):
            associated = [row for row in cbvs if row[0] == ordinal and row[4] == parameter]
            if parameter == 19 and not associated:
                continue
            identity = 40 if parameter == 29 else 41 if parameter == 63 else 1 + source_row[1] * 10 + (0 if parameter == 0 else 1 if parameter == 2 else 2)
            gpu = (associated[0][8] - associated[0][5] * 32) if associated else 0xA000 + ordinal * 0x100 + parameter * 32
            cookie = associated[0][9] if associated else 0xA0 + ordinal
            types = [len(associated), int(parameter == 63), 0, int(parameter == 29)]
            owned.append([ordinal, source_row[1], parameter, extent, 0x100000 + identity * 0x100,
                          0x200000 + identity * 0x100, identity, 1, gpu, cookie,
                          sum(types), *types, 0])
        command = [0] * 24
        command[:11] = [ordinal, source_row[1], 1, 0, 2**32 - 1, 2**32 - 1,
                        len(owned), len(owned), sum(row[3] for row in owned), sum(row[10] for row in owned), 1]
        command[11:16] = [source_row[11], source_row[6], source_row[7], source_row[8],
                          sum(row[0] == ordinal for row in cbvs)]
        commands.append(command)
        tables.extend(owned)
    return source, cbv_report, commands, tables


class SourceTablesReportTest(unittest.TestCase):
    def check(self, source, cbvs, commands, tables, stats=None):
        return analyze(table_stats(commands, tables, source) if stats is None else stats,
                       packed(commands, 24), packed(tables, 16), source, cbvs)

    def reject(self, source, cbvs, commands, tables, stats=None):
        with self.assertRaises((ValueError, RuntimeError)):
            self.check(source, cbvs, commands, tables, stats)

    def test_valid_high_parameter_cache_reuse_and_no_permissions(self):
        source, cbvs, commands, tables = fixture()
        report = self.check(source, cbvs, commands, tables)
        self.assertTrue(report['verified'])
        self.assertEqual(report['sourceCommands'], 2)
        self.assertEqual(report['uniqueTableCount'], 7)
        self.assertEqual(report['uniqueDescriptorCount'], 24)
        self.assertEqual(report['resourceDescriptorStrideFromCamera'], 32)
        self.assertIn(63, report['originalParameters'])
        self.assertFalse(report['completeAllShaderTables'])
        self.assertTrue(report['dynamicUsesDeferred'])
        for key in ('inputContentsFrozen', 'nativeExecutionPermission', 'nativeExecutionPermitted',
                    'copyPermission', 'performanceGain', 'performanceGainProved',
                    'allReflectedFiniteUsesIndependentlyEnumerated', 'finiteDeclaredLayoutsIndependentlyCompared'):
            self.assertIs(report[key], False)
        self.assertEqual(report['nativeSuppression'], 0)
        self.assertEqual(report['commands'], commands)
        self.assertEqual(report['rows'], tables)

    def test_complete_354_occurrences(self):
        source, cbvs, commands, tables = fixture(354)
        report = self.check(source, cbvs, commands, tables)
        self.assertEqual(report['sourceCommands'], 354)
        self.assertEqual(report['ownedCommands'], 354)
        self.assertEqual(report['deferredUseCount'], 354)

    def test_command_identity_completion_and_reserved_fields(self):
        for field, value in ((0, 1), (1, 1), (2, 0), (3, 8), (4, 0), (5, 0),
                             (6, 0), (7, 0), (8, 0), (9, 0), (10, 0),
                             (11, 1), (12, 1), (13, 1), (14, 1), (15, 0), (16, 1), (23, 1)):
            with self.subTest(field=field):
                source, cbvs, commands, tables = fixture()
                commands[0][field] = value
                self.reject(source, cbvs, commands, tables)

    def test_table_identity_width_totals_and_missing_rows(self):
        for field, value in ((0, 1), (1, 1), (2, 64), (3, 0), (3, 5), (4, 0),
                             (5, 0), (6, 0), (7, 0), (8, 0), (9, 0),
                             (10, 0), (11, 0), (12, 1), (14, 1), (15, 1)):
            with self.subTest(field=field, value=value):
                source, cbvs, commands, tables = fixture()
                tables[0][field] = value
                self.reject(source, cbvs, commands, tables)
        source, cbvs, commands, tables = fixture()
        tables.pop()
        self.reject(source, cbvs, commands, tables)
        source, cbvs, commands, tables = fixture()
        tables.insert(1, tables[0][:])
        self.reject(source, cbvs, commands, tables)
        source, cbvs, commands, tables = fixture()
        tables[0], tables[1] = tables[1], tables[0]
        self.reject(source, cbvs, commands, tables)

    def test_reused_identity_is_immutable_but_source_range_may_change(self):
        source, cbvs, commands, tables = fixture()
        repeated = [row for row in tables if row[6] == 41]
        self.assertNotEqual(repeated[0][8:10], repeated[1][8:10])
        self.check(source, cbvs, commands, tables)
        for field, value in ((4, 123), (5, 123), (7, 2), (3, 4)):
            source, cbvs, commands, tables = fixture()
            next(row for row in tables if row[0] == 1 and row[6] == 41)[field] = value
            self.reject(source, cbvs, commands, tables)

    def test_independent_cbv_mapping_gpu_slot_and_heap_cookie(self):
        for field, value in ((4, 2), (5, 0), (8, 0x9021), (9, 0x92), (15, 1), (16, 1)):
            source, cbvs, commands, tables = fixture()
            cbvs['rows'][0][field] = value
            self.reject(source, cbvs, commands, tables)
        source, cbvs, commands, tables = fixture()
        tables[0][8] += 1
        self.reject(source, cbvs, commands, tables)
        source, cbvs, commands, tables = fixture()
        tables[1][9] += 1
        self.reject(source, cbvs, commands, tables)

    def test_aggregate_and_binary_abi_failures(self):
        for field in range(16):
            source, cbvs, commands, tables = fixture()
            stats = table_stats(commands, tables, source)
            stats[field] += 1
            self.reject(source, cbvs, commands, tables, stats)
        source, cbvs, commands, tables = fixture()
        stats = table_stats(commands, tables, source)
        for raw_commands, raw_tables in ((b'', packed(tables, 16)), (packed(commands, 24), b''),
                                        (packed(commands, 24) + b'x', packed(tables, 16)),
                                        (packed(commands, 24), memoryview(packed(tables, 16))[::2])):
            with self.assertRaises((ValueError, RuntimeError)):
                analyze(stats, raw_commands, raw_tables, source, cbvs)
        for invalid in (None, {}, {'verified': False}):
            with self.assertRaises((ValueError, RuntimeError)):
                analyze(stats, packed(commands, 24), packed(tables, 16), source, invalid)


if __name__ == '__main__':
    unittest.main()
