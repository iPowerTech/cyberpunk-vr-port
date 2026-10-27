import copy
import struct
import unittest
from indirect_snapshot_report import analyze, ROW_BYTES
from retained_geometry_report import FIELDS


def fixture():
    sources, natives, snapshots = [], [], []
    for side in range(2):
        original = [0] * 48
        original[0:4] = [side, side, 1, 7]
        original[6:10] = [0x1000 + side, 11 + side, 0x2000 + side, 0x3000]
        original[29:36] = [0x4000, 20, 1, 0x5000 + side, 64, 0, 0]
        original[45] = 0x623CC1
        sources.append(original)
        native = [0] * 56
        native[0] = side; native[2] = 0x623CC1; native[7] = 0x200; native[8] = 2; native[26] = 1
        natives.append(native)
        snapshots.append([side, side, 1, 1, 0, 0, original[6], original[7], side + 1,
                          original[32], original[33], 0x6000 + side, 0, 20, 0xA00,
                          original[32], 0x7000 + side, 0, 0, original[29], 1, 1, original[8], 0])
    source = dict(stats=[3, 7, 2, 0, 2, 1, 1, 2, 2, 2, 2, 2, 0, 0, 0, 0], rows=sources, commands=2)
    bindings = dict(verified=True, rows=natives)
    retained = dict(mode='reference', stage=7, state=3, verified=1,
                    stats=dict(zip(FIELDS, [1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 5, 5, 0, 0, 0, 1])),
                    different=[0] * 12, bytes=[1024, 1024], indirect=[1, 1])
    stats = [3, 7, 2, 2, 2, 0, 40, 2, 2, 0, 2, 2, 0, 0, 0, 0]
    return stats, snapshots, source, bindings, retained


def raw(rows):
    return b''.join(struct.pack('<24Q', *row) for row in rows)


class IndirectSnapshotReportTests(unittest.TestCase):
    def test_exact_private_reference_and_no_native_permission(self):
        stats, rows, source, bindings, retained = fixture()
        report = analyze(stats, raw(rows), source, bindings, retained)
        self.assertEqual(report['frozenIndirectArguments'], 2)
        self.assertEqual(report['copiedBytes'], 40)
        self.assertEqual(report['sourceSubmissionsValidated'], 2)
        self.assertFalse(report['inputContentsFrozen'])
        self.assertFalse(report['nativeExecutionPermitted'])
        self.assertFalse(report['performanceGainProved'])
        self.assertEqual(report['nativeCommandsSuppressed'], 0)

    def test_extent_counters_and_closure(self):
        stats, rows, source, bindings, retained = fixture()
        for block in (raw(rows)[:-1], raw(rows) + bytes(ROW_BYTES), b''):
            with self.subTest(block=len(block)), self.assertRaises((ValueError, RuntimeError)):
                analyze(stats, block, source, bindings, retained)
        for field in range(16):
            changed = stats.copy(); changed[field] += 1
            with self.subTest(field=field), self.assertRaises((ValueError, RuntimeError)):
                analyze(changed, raw(rows), source, bindings, retained)

    def test_each_exact_source_and_private_operand_is_checked(self):
        stats, rows, source, bindings, retained = fixture()
        for field in (field for field in range(24) if field not in (11, 16)):
            changed = copy.deepcopy(rows); changed[0][field] += 1
            with self.subTest(field=field), self.assertRaises((ValueError, RuntimeError)):
                analyze(stats, raw(changed), source, bindings, retained)

    def test_no_source_resource_or_resource_as_allocation_owner(self):
        stats, rows, source, bindings, retained = fixture()
        for field in (11, 16):
            changed = copy.deepcopy(rows); changed[0][field] = changed[0][9]
            with self.subTest(field=field), self.assertRaises(ValueError):
                analyze(stats, raw(changed), source, bindings, retained)
        for field in (11, 16):
            changed = copy.deepcopy(rows); changed[0][field] = 0
            with self.subTest(null=field), self.assertRaises(ValueError):
                analyze(stats, raw(changed), source, bindings, retained)

    def test_native_contract_and_pixel_comparison_are_required(self):
        stats, rows, source, bindings, retained = fixture()
        for field, value in ((2, 0x62462F), (6, 1), (8, 5), (26, 0)):
            changed = copy.deepcopy(bindings); changed['rows'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                analyze(stats, raw(rows), source, changed, retained)
        for field in range(12):
            changed = copy.deepcopy(retained); changed['different'][field] = 1
            with self.subTest(plane=field), self.assertRaises(RuntimeError):
                analyze(stats, raw(rows), source, bindings, changed)


if __name__ == '__main__':
    unittest.main()
