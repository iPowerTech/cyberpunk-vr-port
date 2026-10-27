import struct
import unittest
import ast
from pathlib import Path

from source_selector_capture_report import analyze
from source_stream_report import analyze as analyze_stream
from source_stream_report_test import fixture as stream_fixture, raw


def pack(rows, words):
    return b''.join(struct.pack(f'<{words}Q', *row) for row in rows)


def fixture():
    stats, sources, retained = stream_fixture()
    for source in sources:
        source[13] |= 1 << 19
    source_report = analyze_stream(stats, raw(sources), retained)
    commands, inputs, slots, snapshots = [], [], [], []
    blob = b'abcd' * len(sources)
    for ordinal, source in enumerate(sources):
        command = [0] * 32
        command[:3] = [ordinal, source[1], 1]
        command[4:12] = [1, ordinal + 1, 1, 1, 1, 1, 1, 1]
        command[13:18] = [source[6], source[7], source[8], source[11], source[12]]
        command[18], command[24] = 1, 1
        commands.append(command)
        row = [0] * 24
        row[:8] = [ordinal, source[1], 5, 4, 19, 0, 4, 1]
        row[8:12] = [1, 1, 1, ordinal + 1]
        row[13:23] = [1, ordinal + 1, ordinal * 4 + 1, 4, 100, 200, 300, 400, source[7], source[8]]
        inputs.append(row)
        snapshot = [0] * 32
        snapshot[:7] = row[:7]
        snapshot[8], snapshot[12], snapshot[13], snapshot[20] = 1, 300, 400, 4
        snapshot[23:27] = [100, 200, source[7], source[8]]
        snapshot[29:32] = [1, 1, ordinal * 4 + 1]
        snapshots.append(snapshot)
        slots.append([ordinal, source[1], 33, 31498, 0, 0, 1000, 2000, 2, 4000, 5000, 6000, 1, 1, 31499, 0])
    selector_stats = [3, 1, 4, 4, 4, 0, 4, 0, 0, 4, 4, 4, 4, 1, 1, 0]
    return selector_stats, commands, inputs, slots, [], blob, source_report, dict(verified=True, rows=snapshots), blob


class SourceSelectorCaptureReportTest(unittest.TestCase):
    def test_reader_preserves_native_record_blocks(self):
        tree = ast.parse(Path(__file__).with_name('read_preparation.py').read_text(encoding='utf-8'))
        branches = [node for node in ast.walk(tree) if isinstance(node, ast.If) and isinstance(node.test, ast.Attribute)
                    and node.test.attr == 'retained_source_selectors']
        self.assertTrue(branches)
        for branch in branches:
            assigned = {node.id for node in ast.walk(branch) if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Store)}
            self.assertNotIn('blocks', assigned)

    def check(self, data):
        stats, commands, inputs, slots, t5, intended, sources, snapshots, gpu = data
        return analyze(stats, pack(commands, 32), pack(inputs, 24), pack(slots, 16), pack(t5, 32), intended, sources, snapshots, gpu)

    def test_original_offsets_compact_heap_and_actual_gpu_bytes(self):
        report = self.check(fixture())
        self.assertEqual(report['gpuMatchedCommands'], 4)
        self.assertEqual(report['compactDescriptors'], 4)
        self.assertEqual(report['slots'][0][3:5], [31498, 0])
        for name in ('registeredLedgerCertified', 'shaderDrawConsumptionProved', 'allSourceInputsComplete', 'privateTablesConsumed', 'nativeExecutionPermission', 'performanceGainProved'):
            self.assertIs(report[name], False)

    def test_gpu_match_cannot_be_forged(self):
        data = list(fixture())
        data[-1] = b'xbcd' + data[-1][4:]
        with self.assertRaises(ValueError):
            self.check(data)
        data[2][0][13] = 2
        data[1][0][11] = 0
        data[0][10] -= 1
        report = self.check(data)
        self.assertEqual(report['gpuMismatchedInputs'], 1)

    def test_exact_input_and_original_recording(self):
        for field in (0, 1, 2, 3, 4, 5, 6, 14, 17, 18, 19, 20, 21, 22, 23):
            with self.subTest(field=field):
                data = fixture()
                data[2][0][field] += 1
                with self.assertRaises((ValueError, RuntimeError)):
                    self.check(data)

    def test_compact_offsets_and_command_counts(self):
        for target, field in ((3, 0), (3, 2), (3, 4), (3, 5), (3, 8), (3, 14), (3, 15), (1, 6), (1, 7), (1, 9), (1, 10), (1, 12), (1, 13), (1, 25)):
            with self.subTest(target=target, field=field):
                data = fixture()
                data[target][0][field] += 1
                with self.assertRaises((ValueError, RuntimeError)):
                    self.check(data)

    def test_snapshot_report_and_blob_extents(self):
        for index in (5, 8):
            data = list(fixture())
            data[index] = data[index][:-1]
            with self.assertRaises(ValueError):
                self.check(data)
        data = fixture()
        data[7]['verified'] = False
        with self.assertRaises(ValueError):
            self.check(data)

    def test_missing_intent_remains_declined(self):
        data = list(fixture())
        data[1][0][2], data[1][0][7], data[1][0][8], data[1][0][9], data[1][0][10], data[1][0][11] = 3, 0, 0, 0, 0, 0
        row = data[2][0]
        row[7] = 0
        for field in (8, 9, 10, 12, 13, 14, 15, 16):
            row[field] = 0
        data[3].pop(0)
        for row in data[2][1:]:
            row[15] -= 4
        data[5] = data[5][4:]
        data[0][4], data[0][6], data[0][8], data[0][9], data[0][10], data[0][11], data[0][12] = 3, 3, 1, 3, 3, 3, 3
        report = self.check(data)
        self.assertEqual(report['declinedCommands'], 1)


if __name__ == '__main__':
    unittest.main()
