"""Pure overlay ABI fixtures; no process, graphics API or GPU work."""

import copy
import struct
import unittest

from source_cbv_report import _no_permission
from source_assembly_report import analyze as analyze_assembly
from t5_overlay_report import DOMAIN_BYTES, MAX_BYTES, analyze
from t5_snapshot_report import analyze as analyze_t5
from t5_snapshot_report_test import fixture as t5_fixture, packed


def fixture():
    data = t5_fixture()
    source = data['source']
    t5 = analyze_t5(packed(data['native'], 96), packed(data['snapshots'], 32),
                    data['gpu'], data['intent'], source, packed(data['metadata'], 32), packed(data['pieces'], 16))
    cbvs, cbv_commands, copy_owners, cbv_blob = [], [], [], bytearray()
    commands, tables, selectors, overlay = [], [], [], []
    private_cursors = [0, 0]
    cursor = required = 0
    for ordinal, (s, snapshot) in enumerate(zip(source['rows'], t5['snapshots'])):
        side = s[1]
        b4 = bytearray(228)
        struct.pack_into('<f', b4, 128, snapshot[10] // 128)
        struct.pack_into('<I', b4, 224, (1 << (snapshot[18] // 128)) - 1)
        cbv = [0] * 32
        cbv[:8] = [ordinal, side, 5, 4, 19, 0, 228, 256]
        cbv[8:17] = [1, 0, 1, 2, 0xD000 + ordinal * 256, 0x2000000 + ordinal * 256, 0, 3, 80 + ordinal]
        cbv[17:23] = [0xE000 + side * 256, 0x3000000 + side * DOMAIN_BYTES + private_cursors[side],
                      private_cursors[side], 228, 256, 0xF000 + side * 256]
        cbv[23:29] = [0x10000 + ordinal * 32, 0x11000 + side * 256, s[7], s[8], 100 + ordinal, 200 + ordinal]
        cbv[29:32] = [1, 1, len(cbv_blob) + 1]
        private_cursors[side] += 256
        cbv_blob.extend(b4)
        cbvs.append(cbv)
        cbv_command = [0] * 24
        cbv_command[:11] = [ordinal, side, 1, 0, 1, 0, 1, 1, 228, 256, cbv[17]]
        cbv_command[11:20] = [s[6], s[7], s[8], ordinal + 1, 1, 1, 1, 1, 1]
        cbv_commands.append(cbv_command)
        copy_owner = 0x12000 + ordinal * 256
        copy_owners.append([ordinal, side, s[6], s[7], ordinal + 1, copy_owner, 1, 228])
        selector = [0] * 32
        selector[:3] = [ordinal, side, 2]
        selector[5] = ordinal + 1
        selector[13:18] = [s[6], s[7], s[8], s[11], s[12]]
        selectors.append(selector)
        for parameter, extent in ((19, 1), (23, 6)):
            tables.append([ordinal, side, parameter, extent, 1, extent, 0x13000 + ordinal * 256, 0x14000 + ordinal * 256,
                           1, 1, 0x15000 + ordinal * 256, 0x16000 + ordinal * 256, 0x17000 + ordinal * 256, 0, 0, 0])
        command = [0] * 32
        command[:11] = [ordinal, side, 1, 0, 1, 0, 2, 7, 1, 0, 1]
        command[11:21] = [s[6], s[7], ordinal + 1, s[11], 1, 2, 0, 2, 0, 0]
        command[21] = 1
        command[27:32] = [ordinal * 2, 2, 0, 0, 1]
        commands.append(command)
        target = max(cursor, snapshot[10])
        first = (target - snapshot[10]) // 128
        row = [ordinal, side, 1, 0, s[11], s[6], s[7], ordinal + 1,
               0x18000, 0x19000, snapshot[16], snapshot[19], snapshot[17], snapshot[10], snapshot[18],
               target, first, 8192, 128, cbv[17], cbv[22], copy_owner, cbv[18], cbv[19], 228, ordinal + 1,
               1, 1, 1, 0x1A000, 55, 1]
        overlay.append(row)
        cursor = target + row[14]
        required = max(required, first * 128 + DOMAIN_BYTES)
    count = len(commands)
    assembly_stats = [3, source['stats'][1], count, count, 0, count, 0, count * 2, count * 7, count, 0, 0, 0, 0, 0, 0]
    cbv_report = dict(verified=True, stats=[3, source['stats'][1], count, count, count, 0, 0,
                     count * 228, count * 256, 2, count, 0, count, 0, 0, 0], rows=cbvs,
                     commands=cbv_commands, metadataOnly=False, **_no_permission())
    assembly = analyze_assembly(assembly_stats, packed(commands, 32), packed(tables, 16), b'', source,
                                cbv_report, dict(verified=True, commands=selectors))
    stats = [2, source['stats'][1], count, required, len(data['gpu']), 0x1A000, 55, 0x1B000, 0x1C000, 7, 7,
             count, count, 0, 0, 0]
    return dict(stats=stats, rows=overlay, blob=data['gpu'], source=source, cbvs=cbv_report,
                cbv_blob=bytes(cbv_blob), t5=t5, t5_blob=data['gpu'], assembly=assembly, owners=copy_owners)


def check(data, **overrides):
    return analyze(data['stats'], overrides.get('raw', packed(data['rows'], 32)),
                   overrides.get('blob', data['blob']), data['source'], data['cbvs'],
                   overrides.get('cbv_blob', data['cbv_blob']), data['t5'],
                   overrides.get('t5_blob', data['t5_blob']), data['assembly'],
                   overrides.get('owner_raw', packed(data['owners'], 8)))


def native_fixture():
    data = fixture()
    ends, widths = {}, {}
    for row in data['rows']:
        snapshot = data['t5']['snapshots'][row[0]]
        backing = snapshot[16]
        target = max(ends.get(backing, 0), row[13])
        row[8] = backing
        row[12] = row[15] = snapshot[17] = target
        row[16] = (target - row[13]) // 128
        row[29:31] = row[5:7]
        ends[backing] = target + row[14]
        widths[backing] = max(widths.get(backing, 0), row[16] * 128 + DOMAIN_BYTES)
    data['stats'][3] = sum(widths.values())
    data['stats'][5:11] = [0] * 6
    data['stats'][13] = 1
    return data


class T5OverlayReportTest(unittest.TestCase):
    def reject(self, data, **overrides):
        with self.assertRaises((ValueError, RuntimeError)):
            check(data, **overrides)

    def test_native_source_reuse_has_no_second_copy_or_fence(self):
        data = native_fixture()
        report = check(data)
        self.assertTrue(report['sourceCopiesReused'])
        self.assertFalse(report['destinationFenceComplete'])
        self.assertEqual(report['additionalGpuCopies'], 0)
        self.assertEqual(report['requiredBytes'], 2 * (DOMAIN_BYTES + 2048))
        for row in report['rows']:
            self.assertEqual(row[8], row[10])
            self.assertEqual(row[12], row[15])
            self.assertEqual(row[29:31], row[5:7])

    def test_native_adoption_rejects_fictitious_copy_and_cross_recording_aliases(self):
        for field in (5, 6, 7, 8, 9, 10, 14, 15):
            data = native_fixture()
            data['stats'][field] = 1
            self.reject(data)
        for field in (8, 9, 10, 11, 12, 15, 16, 29, 30):
            data = native_fixture()
            data['rows'][0][field] += 128
            self.reject(data)
        data = native_fixture()
        data['rows'][2][8] = data['rows'][2][10] = data['rows'][0][8]
        self.reject(data)

    def test_current_completed_overlay_exact_bytes_and_preserved_index_domain(self):
        data = fixture()
        report = check(data)
        self.assertTrue(report['verified'])
        self.assertEqual((report['versions'], report['copiedBytes'], report['attached']), (4, 8192, 4))
        self.assertEqual(report['requiredBytes'], DOMAIN_BYTES + 6144)
        self.assertTrue(report['destinationFenceComplete'])
        self.assertEqual(report['rows'], data['rows'])
        self.assertEqual(report['cbvCopyOwners'], data['owners'])
        for row in data['rows']:
            self.assertEqual((row[16] + row[13] // 128) * 128, row[15])
        for field, expected in _no_permission().items():
            self.assertEqual(report[field], expected)
        self.assertFalse(report['privateT5Consumed'])
        self.assertFalse(report['wholeInputEquivalenceProved'])

    def test_none_is_empty_and_queued_failed_never_promote(self):
        data = fixture()
        data['stats'] = [0, data['source']['stats'][1]] + [0] * 14
        data['rows'], data['blob'] = [], b''
        self.assertEqual(check(data)['versions'], 0)
        self.assertFalse(check(data)['destinationFenceComplete'])
        for field in (0, 2, 3, 4, 5, 11, 12, 13, 14, 15):
            changed = copy.deepcopy(data)
            changed['stats'][field] = 1
            self.reject(changed)
        changed = copy.deepcopy(data)
        changed['rows'] = fixture()['rows'][:1]
        self.reject(changed)
        for state in (1, 3, 4):
            changed = fixture()
            changed['stats'][0] = state
            self.reject(changed)

    def test_fences_and_aggregate_bounds_are_strict(self):
        for field, value in ((1, 2), (2, 3), (3, MAX_BYTES + 128), (3, DOMAIN_BYTES), (4, 8191),
                             (5, 0), (6, 0), (7, 0), (8, 0), (9, 0), (10, 6), (10, 2**64 - 1),
                             (11, 3), (12, 3), (13, 1), (14, 1), (15, 1)):
            with self.subTest(field=field):
                data = fixture()
                data['stats'][field] = value
                self.reject(data)

    def test_scope_dense_b4_and_closure_fields_do_not_borrow_receipts(self):
        for field in range(32):
            if field in (28,):
                continue
            with self.subTest(field=field):
                data = fixture()
                data['rows'][0][field] += 1
                self.reject(data)
        data = fixture()
        data['rows'][1] = data['rows'][0][:]
        self.reject(data)
        data = fixture()
        data['rows'][0][25] = 2
        self.reject(data)

    def test_copy_owner_exact_scope_counts_and_current_command_receipt(self):
        for field in range(8):
            with self.subTest(owner_field=field):
                data = fixture()
                data['owners'][0][field] += 1
                self.reject(data)
        for field in (0, 1, 2, 3, 7, 8, 11, 12, 14, 15, 19):
            data = fixture()
            data['cbvs']['commands'][0][field] += 1
            self.reject(data)
        data = fixture()
        data['owners'][0][5] = 0
        data['rows'][0][21] = 0
        self.reject(data)

    def test_gpu_payload_is_exact_and_dense_with_no_trailing_bytes(self):
        data = fixture()
        changed = bytearray(data['blob'])
        changed[100] ^= 1
        self.reject(data, blob=changed)
        for name in ('blob', 'cbv_blob', 't5_blob'):
            for value in (data[name][:-1], data[name] + b'x', [], None):
                self.reject(data, **{name: value})
        for field, value in ((27, 2), (18, 1024), (16, 0), (19, 0), (15, 99), (14, 99)):
            data = fixture()
            data['t5']['snapshots'][0][field] = value
            self.reject(data)
        for field, value in ((31, 2), (20, 227), (17, 0), (22, 0), (25, 99), (26, 99)):
            data = fixture()
            data['cbvs']['rows'][0][field] = value
            self.reject(data)

    def test_actual_b4_bytes_recompute_the_span_not_only_numeric_owner_labels(self):
        data = fixture()
        for offset, value in ((128, float('nan')), (128, float('inf')), (128, -1.0), (128, 8192.0), (128, 3413.0)):
            changed = bytearray(data['cbv_blob'])
            struct.pack_into('<f', changed, offset, value)
            self.reject(data, cbv_blob=changed)
        for mask in (0, 1, 0x80000000):
            changed = bytearray(data['cbv_blob'])
            struct.pack_into('<I', changed, 224, mask)
            self.reject(data, cbv_blob=changed)

    def test_attachment_requires_cpu_assembly_but_never_draw_execution(self):
        data = fixture()
        data['rows'][0][28] = 0
        data['stats'][12] -= 1
        self.assertEqual(check(data)['attached'], 3)
        for field in (2, 4, 21):
            changed = fixture()
            changed['assembly']['commands'][0][field] = 0
            self.reject(changed)
        changed = fixture()
        changed['assembly']['commands'][0][26] = 1
        self.reject(changed)
        for field, value in ((2, 22), (3, 5), (12, 0)):
            changed = fixture()
            changed['assembly']['tables'][1][field] = value
            self.reject(changed)
        for name in ('cbvs', 't5', 'assembly'):
            changed = fixture()
            changed[name]['verified'] = False
            self.reject(changed)
            changed = fixture()
            changed[name]['nativeExecutionPermitted'] = True
            self.reject(changed)

    def test_binary_row_owner_extent_limits_and_integer_stats(self):
        data = fixture()
        for value in (packed(data['rows'], 32)[:-1], b'x', [], None, packed(data['rows'][:1] * 2049, 32)):
            self.reject(data, raw=value)
        for value in (packed(data['owners'], 8)[:-1], b'x', [], None):
            self.reject(data, owner_raw=value)
        for value in (-1, 2**64, True):
            changed = fixture()
            changed['stats'][3] = value
            self.reject(changed)
        self.assertTrue(check(data, raw=memoryview(packed(data['rows'], 32)), blob=bytearray(data['blob']))['verified'])


if __name__ == '__main__':
    unittest.main()
