import ast
import fnmatch
from pathlib import Path
import struct
import unittest
from staged_sources_report import analyze, analyze_baseline


def fixture():
    sources = []
    rows = []
    guards = []
    for ordinal, (eye, ready, materials, checks) in enumerate(((0, 1, 7, 1), (0, 0, 0, 2), (1, 1, 5, 2), (1, 1, 6, 2))):
        source = [0] * 48
        source[:4] = [ordinal, eye, 0, 7]
        for field in range(6, 13):
            source[field] = 1000 + field + ordinal * 20
        source[43] = 31
        sources.append(source)
        rows.append([ordinal, eye, ready, 0 if ready else 1, source[6], source[7], ordinal + 1, materials])
        guards.append([ordinal, eye, source[6], source[7], ordinal + 1, source[11], 1, 0, 15, checks, 228, 9, 0, 1, 1, 0])
    stats = [2, 7, 4, 3, 1, 18, 5, 0, 0, 0, 0, 0]
    source = dict(stats=[3, 7, 4, 4, 0, 2, 2, 4, 4, 4, 4, 4, 0, 0, 0, 0],
                  rows=sources, commands=4, direct=4, indirect=0)
    return stats, rows, source, dict(verified=True, rows=guards)


def raw(rows):
    return b''.join(struct.pack('<8Q', *row) for row in rows)


class StagedSourcesTests(unittest.TestCase):
    def test_source_baseline_captures_both_complete_writable_target_sets(self):
        _,_,source,_=fixture()
        for colors in (3,4):
            values=[7,colors,64*1024*1024,1,0,1,0,0]*2
            report=analyze_baseline(values,source)
            self.assertTrue(report['fullPreIntervalBaseline'])
            self.assertFalse(report['nativePassEquivalenceProved'])
            self.assertFalse(report['nativeExecutionPermission'])
            self.assertEqual([eye['colors'] for eye in report['eyes']],[colors,colors])
        for eye in range(2):
            for field,value in ((0,8),(1,2),(1,5),(2,0),(2,64*1024*1024+1),(3,0),(3,2),(4,1),(5,0),(5,2),(6,1),(7,1)):
                values=[7,3,1024*1024,1,0,1,0,0]*2;values[eye*8+field]=value
                with self.subTest(eye=eye,field=field,value=value),self.assertRaises(ValueError):
                    analyze_baseline(values,source)
        for values in (None,[],[0]*15,[0]*17,[True]*16,[-1]*16,[2**64]*16):
            with self.subTest(values=values),self.assertRaises(ValueError):analyze_baseline(values,source)

    def test_reader_resolves_all_staged_exports(self):
        tree = ast.parse(Path(__file__).with_name('read_preparation.py').read_text(encoding='utf-8'))
        calls = [node for node in ast.walk(tree) if isinstance(node, ast.Call) and
                 isinstance(node.func, ast.Name) and node.func.id == 'resolve']
        self.assertEqual(len(calls), 1)
        patterns = [node.value for node in calls[0].args[2].elts]
        self.assertTrue(any(fnmatch.fnmatchcase('CyberpunkVR_RetainedBaseline',pattern) for pattern in patterns))
        for name in ('Stats', 'Rows', 'Count', 'RowBytes'):
            self.assertTrue(any(fnmatch.fnmatchcase('CyberpunkVR_RetainedStaged' + name, pattern) for pattern in patterns))

    def test_ready_and_skipped_packets_before_readback(self):
        stats, rows, source, guarded = fixture()
        report = analyze(stats, raw(rows), source, guarded)
        self.assertEqual((report['ready'], report['skipped'], report['materials'], report['checks']), (3, 1, 18, 5))
        self.assertEqual(report['readyPerEye'], [1, 2])
        self.assertTrue(report['constructedBeforeCpuReadback'])
        self.assertFalse(report['laterVerificationComplete'])
        self.assertFalse(report['nativeExecutionPermission'])
        self.assertFalse(report['sourceContentsCpuVerified'])

    def test_later_progress_is_bounded_and_completion_is_exact(self):
        for progress in ([0, 0], [2, 1], [3, 3]):
            stats, rows, source, guarded = fixture(); stats[10:] = progress
            report = analyze(stats, raw(rows), source, guarded)
            self.assertEqual(report['laterVerificationComplete'], progress == [3, 3])
            if progress == [3, 3]:
                analyze(stats, raw(rows), source, guarded, complete=True)
            else:
                with self.assertRaises(ValueError):
                    analyze(stats, raw(rows), source, guarded, complete=True)

    def test_stats_abi_bounds_and_aggregates(self):
        for field, value in ((0, 0), (0, 1), (0, 3), (1, 8), (2, 5), (3, 4), (4, 2), (5, 19),
                             (5, 65537), (6, 2), (6, 7), (7, 1), (8, 1), (9, 1), (10, 4), (11, 4)):
            stats, rows, source, guarded = fixture(); stats[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                analyze(stats, raw(rows), source, guarded)
        for invalid in (None, [], [0] * 11, [0] * 13, [True] * 12, [-1] * 12, [2**64] * 12):
            _, rows, source, guarded = fixture()
            with self.subTest(stats=invalid), self.assertRaises(ValueError):
                analyze(invalid, raw(rows), source, guarded)

    def test_exact_occurrence_status_and_material_counts(self):
        for field, value in ((0, 1), (1, 1), (2, 2), (3, 1), (3, 2**32), (4, 0), (5, 0), (6, 2), (7, 8), (7, 65537)):
            stats, rows, source, guarded = fixture(); rows[0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                analyze(stats, raw(rows), source, guarded)
        for field, value in ((3, 0), (7, 1)):
            stats, rows, source, guarded = fixture(); rows[1][field] = value
            with self.subTest(skipped_field=field), self.assertRaises(ValueError):
                analyze(stats, raw(rows), source, guarded)

    def test_both_eyes_need_ready_packets_even_with_matching_totals(self):
        stats, rows, source, guarded = fixture()
        rows[0][2:4] = [0, 1]; rows[0][7] = 0
        stats[3:7] = [2, 2, 11, 4]
        with self.assertRaises(ValueError):
            analyze(stats, raw(rows), source, guarded)

    def test_guarded_check_total_and_source_identity(self):
        stats, rows, source, guarded = fixture(); stats[6] = 4
        analyze(stats, raw(rows), source)
        with self.assertRaises(ValueError):
            analyze(stats, raw(rows), source, guarded)
        for field in (0, 1, 2, 3, 4, 5, 6, 7, 9):
            stats, rows, source, guarded = fixture(); guarded['rows'][2][field] += 1
            with self.subTest(field=field), self.assertRaises(ValueError):
                analyze(stats, raw(rows), source, guarded)

    def test_row_abi_and_source_stream_are_required(self):
        stats, rows, source, guarded = fixture()
        for data in (None, b'', raw(rows)[:-1], raw(rows[:-1]), raw(rows) + raw(rows[:1]), bytes(2049 * 64)):
            with self.subTest(size=len(data) if data is not None else None), self.assertRaises(ValueError):
                analyze(stats, data, source, guarded)
        source['rows'][0][43] = 0
        with self.assertRaises(RuntimeError):
            analyze(stats, raw(rows), source, guarded)


if __name__ == '__main__':
    unittest.main()
