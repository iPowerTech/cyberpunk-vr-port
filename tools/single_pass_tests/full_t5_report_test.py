import copy
import struct
import unittest
import full_t5_report as report


def fixture():
    source_rows = []
    for ordinal in range(6):
        side = ordinal//3
        row = [0]*48
        row[:5] = [ordinal, side, 0, 77, 7 if ordinal % 3 == 0 else 9]
        row[6:13] = [0x1000+side, 7+side, 0x2000+side, 0x3000, 1, 1, 1]
        row[43] = 31
        source_rows.append(row)
    source = dict(stats=[3, 77, 6, 6, 0, 3, 3, 6, 6, 6, 6, 6, 0, 0, 0, 0],
                  rows=source_rows, commands=6, direct=6, indirect=0)
    selected = [[1, 0], [2, 0], [4, 1], [5, 1]]
    tail = dict(frame=77, sourceCount=4, nativeSources=selected,
                sources=[[ordinal, 1-side] for ordinal, side in selected])
    rows = []
    for ordinal, side in selected:
        reused = int(ordinal % 3 == 2)
        rows.append([ordinal, side, 77, 0x1000+side, 7+side, ordinal+1, 0x4000+side, 0x5000+side,
                     0, report.BYTES, reused, 0 if reused else report.BYTES,
                     reused*report.BYTES, 0, 2, report.MAX_U64])
    stats = [1, 1, 77, 4, 2, 2, 2*report.BYTES, 3, 4, 4, 0, 0,
             report.BYTES, report.BYTES, 4*report.BYTES, 0]
    return stats, rows, source, tail


class FullT5ReportTests(unittest.TestCase):
    def validate(self, stats=None, rows=None, source=None, tail=None):
        original = fixture()
        return report.validate(stats if stats is not None else original[0], rows if rows is not None else original[1],
                               requested=True, source=source if source is not None else original[2],
                               tail=tail if tail is not None else original[3])

    def test_complete_per_occurrence_native_readbacks_and_reuse(self):
        result = self.validate()
        self.assertTrue(result['verified'])
        self.assertEqual(result['summary']['sources'], 4)
        self.assertEqual(result['summary']['copies'], 2)
        self.assertEqual(result['summary']['referenceBytes'], 4*report.BYTES)
        self.assertEqual(result['rows'], fixture()[1])

    def test_new_copy_after_write_uses_another_full_arena_version(self):
        stats, rows, _, _ = fixture()
        rows[1][8] = report.BYTES
        rows[1][10:12] = [0, report.BYTES]
        rows[1][13] = report.BYTES//128
        stats[4:7] = [3, 1, 3*report.BYTES]
        stats[12] = 2*report.BYTES
        self.assertTrue(self.validate(stats, rows)['verified'])

    def test_disabled_capture_must_be_exactly_idle(self):
        stats = [1]+[0]*15
        self.assertTrue(report.validate(stats, [], requested=False)['verified'])
        for index in range(1, 16):
            changed = stats.copy()
            changed[index] = 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                report.validate(changed, [], requested=False)
        with self.assertRaises(ValueError):
            report.validate(stats, [[0]*16], requested=False)

    def test_abi_rejects_booleans_bad_words_schema_and_truncation(self):
        stats, rows, _, _ = fixture()
        for bad in (stats[:-1], stats+[0], [2]+stats[1:], [True]+stats[1:], [-1]+stats[1:], [2**64]+stats[1:]):
            with self.subTest(stats=bad), self.assertRaises(ValueError):
                report.decode(bad, rows)
        for row in (rows[0][:-1], rows[0]+[0], [True]+rows[0][1:], [-1]+rows[0][1:]):
            with self.subTest(row=row), self.assertRaises(ValueError):
                report.decode(stats, [row])
        with self.assertRaises(ValueError):
            report.decode(stats, [[0]*16]*(report.MAX_ROWS+1))

    def test_every_requested_counter_is_checked(self):
        stats, _, _, _ = fixture()
        for index in range(16):
            changed = stats.copy()
            changed[index] += 1
            with self.subTest(index=index), self.assertRaises((ValueError, RuntimeError)):
                self.validate(changed)

    def test_each_identity_range_and_completion_field_is_checked(self):
        _, rows, _, _ = fixture()
        for index in range(16):
            changed = copy.deepcopy(rows)
            changed[1][index] = 0 if index == 15 else changed[1][index]+1
            with self.subTest(index=index), self.assertRaises((ValueError, RuntimeError)):
                self.validate(rows=changed)

    def test_missing_extra_reordered_sources_cannot_pass(self):
        _, rows, _, _ = fixture()
        for changed in (rows[:-1], rows+[rows[-1]], list(reversed(rows)), [rows[1], rows[0], *rows[2:]]):
            with self.subTest(rows=changed), self.assertRaises(ValueError):
                self.validate(rows=changed)

    def test_tail_array_eye_and_native_side_are_not_interchangeable(self):
        _, _, _, tail = fixture()
        changed = copy.deepcopy(tail)
        changed['sources'] = changed['nativeSources']
        with self.assertRaises(ValueError):
            self.validate(tail=changed)
        changed = copy.deepcopy(tail)
        changed['nativeSources'][0][0] = 0
        with self.assertRaises(ValueError):
            self.validate(tail=changed)

    def test_reuse_requires_original_copied_row_not_a_self_reference(self):
        stats, rows, _, _ = fixture()
        rows[0][10:12] = [1, 0]
        rows[1][10:12] = [0, report.BYTES]
        with self.assertRaises(ValueError):
            self.validate(stats, rows)
        stats, rows, _, _ = fixture()
        rows[1][6] = rows[2][6]
        with self.assertRaises(ValueError):
            self.validate(stats, rows)

    def test_duplicate_reference_ranges_and_cross_eye_arenas_rejected(self):
        _, rows, _, _ = fixture()
        rows[1][12] = 0
        with self.assertRaises(ValueError):
            self.validate(rows=rows)
        _, rows, _, _ = fixture()
        for row in rows[2:]:
            row[7] = rows[0][7]
        with self.assertRaises(ValueError):
            self.validate(rows=rows)

    def test_source_stream_generation_and_frame_must_match(self):
        _, _, source, _ = fixture()
        source['rows'][2][7] += 1
        with self.assertRaises(ValueError):
            self.validate(source=source)
        _, _, source, _ = fixture()
        source['stats'][1] += 1
        with self.assertRaises(ValueError):
            self.validate(source=source)

    def test_capture_validation_does_not_depend_on_prediction_or_input_fault(self):
        stats, rows, source, tail = fixture()
        capture = dict(fullT5Requested=True, fullT5Available=True, sourceStream=source, preparedTail=tail,
                       fullT5=self.validate(stats, rows))
        for rejected in (False, True):
            capture['inputRejectionVerified'] = rejected
            self.assertTrue(report.validate_capture(capture, True)['verified'])
        with self.assertRaises(ValueError):
            report.validate_capture(capture, False)
        capture['fullT5']['verified'] = False
        with self.assertRaises(ValueError):
            report.validate_capture(capture, True)

    def test_reader_bounds_and_terminal_raw_preservation(self):
        stats, rows, _, _ = fixture()
        exports = {report.PREFIX+'Stats': struct.pack('<16Q', *stats), report.PREFIX+'Count': struct.pack('<I', len(rows)),
                   report.PREFIX+'RowBytes': struct.pack('<I', 128), report.PREFIX+'Rows': struct.pack('<Q', 0x9000)}
        raw = b''.join(struct.pack('<16Q', *row) for row in rows)
        reads = []
        def read_memory(address, size):
            reads.append((address, size))
            return raw
        def read_export(name, size):
            self.assertEqual(len(exports[name]), size)
            return exports[name]
        self.assertEqual(report.read_report(read_export, read_memory), report.decode(stats, rows))
        self.assertEqual(reads, [(0x9000, len(raw))])
        for name, value in (('RowBytes', 120), ('Count', report.MAX_ROWS+1), ('Rows', 0)):
            old = exports[report.PREFIX+name]
            exports[report.PREFIX+name] = struct.pack('<Q' if name == 'Rows' else '<I', value)
            with self.subTest(name=name), self.assertRaises(ValueError):
                report.read_report(read_export, read_memory)
            exports[report.PREFIX+name] = old
        with self.assertRaises(ValueError):
            report.read_report(read_export, lambda address, size: raw[:-1])


if __name__ == '__main__':
    unittest.main()
