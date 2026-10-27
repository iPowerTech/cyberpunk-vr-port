"""Pure t5 ABI fixtures; no process, graphics API or GPU work."""

import struct
import unittest

from source_stream_report import analyze as analyze_source
from source_stream_report_test import fixture as source_fixture, raw as source_raw
from t5_snapshot_report import analyze, analyze_coverage

SOURCE_BYTES = 1024 * 1024
READ_BYTES = 2048


def packed(rows, words):
    return b''.join(struct.pack(f'<{words}Q', *row) for row in rows)


def raw_record(row):
    raw = bytearray(176)
    struct.pack_into('<I', raw, 0, row[10])
    raw[6] = (row[8] << 4) | row[6]
    struct.pack_into('<2Q', raw, 8, row[9], row[11])
    struct.pack_into('<Q', raw, 0x48, row[18])
    row[48:70] = struct.unpack('<22Q', raw)


def raw_slot(row):
    raw = bytearray(16)
    struct.pack_into('<2I2H', raw, 0, 0, 32, row[39], row[40])
    raw[14] = 23
    row[72:74] = struct.unpack('<2Q', raw)


def fixture(statuses=(1, 1, 1, 1), native_statuses=(1, 1, 1, 1), states=(2, 2, 2, 2)):
    source_stats, source_rows, retained = source_fixture()
    for original in source_rows:
        side = original[1]
        original[6:13] = [0x1000 + side * 256, 21 + side, 0x2000 + side * 256,
                          0x3000, 0x4000 + original[0] * 256, 0x5000, 100 + original[0]]
        original[13] = (1 << 19) | (1 << 23) | (1 << 31)
    source = analyze_source(source_stats, source_raw(source_rows), retained)
    native, metadata, snapshots, pieces = [], [], [], []
    gpu = bytearray()
    intent = bytearray()
    private_cursor = [0, 0]
    for ordinal, original in enumerate(source_rows):
        side, status, native_status = original[1], statuses[ordinal], native_statuses[ordinal]
        resource, base = 0x6000 + ordinal * 256, 0x1000000 + ordinal * SOURCE_BYTES
        descriptor = 0xA000 + ordinal * 32
        meta = [0] * 32
        meta[:14] = [ordinal, side, 1, 0, 23, 5, descriptor, 3, 2, 600 + ordinal,
                     resource, 0, 8192, 128]
        if status != 2:
            meta[14:20] = [436736, READ_BYTES, 3412, 65535, 16, 1]
        meta[21:28] = [1, 1, 0, 0, 0, 0, original[7]]
        meta[28:30] = [int(status != 2), 1]
        n = [0] * 96
        n[:3] = [ordinal, side, native_status]
        n[15:17] = [original[6], original[7]]
        if native_status == 1:
            kind = 0 if ordinal % 2 == 0 else 3
            n[3:15] = [1, 5, 0, 8, 100 + ordinal, kind, base, SOURCE_BYTES, resource,
                        original[9], original[8], 0x7000 + side * 256]
            n[17:24] = [n[12] + 0x5C0AF8 + (n[7] - 1) * 176, 0x9000 + ordinal * 32,
                        80 + ordinal, states[ordinal], 0x2C0 if states[ordinal] == 2 else 0, kind, 1]
            n[34:45] = [base, SOURCE_BYTES, 0, SOURCE_BYTES, 23, 0, 8, 2**32 - 1, 1, 1, 0]
            raw_record(n)
            raw_slot(n)
        row = [0] * 32
        row[:10] = [ordinal, side, status, 0 if status != 3 else 9, resource, base,
                    n[7], n[8], n[20], n[21]]
        row[10:16] = [meta[14], meta[15], descriptor, 3, original[8], original[7]]
        row[20] = ordinal + 1
        if status != 2:
            row[25:27] = [1000 + ordinal, len(intent) + 1]
            payload = bytes([41 + ordinal]) * READ_BYTES
            intent.extend(payload)
            pieces.append([ordinal,side,len(pieces),1000+ordinal,0x1234,1000+ordinal,0x5678,row[10],0,READ_BYTES,row[10],0,READ_BYTES,0,0,0])
            if status == 1:
                row[16:20] = [0xB000 + side * 256, private_cursor[side], READ_BYTES, 0xC000 + side * 256]
                private_cursor[side] += READ_BYTES
                row[21:25] = [1, 1, 1, 1]
                row[27:30] = [len(gpu) + 1, 1, 1]
                gpu.extend(payload)
        native.append(n)
        metadata.append(meta)
        snapshots.append(row)
    return dict(source=source, native=native, metadata=metadata, snapshots=snapshots, pieces=pieces,
                gpu=bytes(gpu), intent=bytes(intent))


class T5SnapshotReportTest(unittest.TestCase):
    def coverage_fixture(self):
        data = fixture()
        coverage = []
        for meta in data['metadata']:
            coverage.append([*meta[:2], 1, 0, 0x5356434f56303031, meta[10], 1, 1,
                             1, 1, 1, 1, 0, SOURCE_BYTES, SOURCE_BYTES, 1,
                             meta[14], meta[15], meta[15], 2**64 - 1])
        return data, coverage

    def test_current_coverage_full_and_hole_never_grant_gpu_permission(self):
        data, coverage = self.coverage_fixture()
        coverage[0][18:20] = [READ_BYTES - 128, coverage[0][16] + 128]
        result = analyze_coverage(packed(coverage, 20), packed(data['metadata'], 32), data['source'])
        self.assertEqual((result['fullyCovered'], result['partial'], result['uncovered']), (3, 1, 0))
        self.assertFalse(result['nativeExecutionPermitted'])
        coverage[0][18:20] = [0, coverage[0][16]]
        self.assertEqual(analyze_coverage(packed(coverage, 20), packed(data['metadata'], 32), data['source'])['uncovered'], 1)

    def test_current_coverage_rejects_foreign_resource_stale_marker_and_hidden_holes(self):
        for field, value in ((0, 99), (1, 1), (3, 1), (4, 0), (5, 99), (6, 0), (8, 99),
                             (10, 99), (14, SOURCE_BYTES * 3), (15, 65537), (16, 0), (17, 65537), (18, READ_BYTES + 1), (19, 0)):
            with self.subTest(field=field):
                data, coverage = self.coverage_fixture()
                coverage[0][field] = value
                with self.assertRaises(ValueError):
                    analyze_coverage(packed(coverage, 20), packed(data['metadata'], 32), data['source'])

    def check(self, data, **overrides):
        return analyze(overrides.get('native_raw', packed(data['native'], 96)),
                       overrides.get('snapshot_raw', packed(data['snapshots'], 32)),
                       overrides.get('gpu_blob', data['gpu']), overrides.get('intent_blob', data['intent']),
                       data['source'], overrides.get('metadata_raw', packed(data['metadata'], 32)),overrides.get('piece_raw',packed(data['pieces'],16)))

    def reject(self, data, **overrides):
        with self.assertRaises((ValueError, RuntimeError)):
            self.check(data, **overrides)

    def test_committed_kind0_kind3_exact_2k_gpu_match_without_permissions(self):
        data = fixture()
        result = self.check(data)
        self.assertTrue(result['verified'])
        self.assertEqual((result['nativeCaptured'], result['copied'], result['matched'], result['mismatched']), (4, 4, 4, 0))
        self.assertEqual((result['gpuBytes'], result['candidateBytes']), (8192, 8192))
        self.assertEqual(result['native'], data['native'])
        self.assertEqual(result['snapshots'], data['snapshots'])
        self.assertEqual({row[8] for row in data['native']}, {0, 3})
        self.assertTrue(all(row[20:22] == [2, 0x2C0] for row in data['native']))
        for key in ('privateRangesConsumed', 'wholeInputEquivalenceProved', 'inputContentsFrozen',
                    'nativeExecutionPermission', 'nativeExecutionPermitted', 'performanceGain',
                    'performanceGainProved', 'copyPermission'):
            self.assertIs(result[key], False)
        for key in ('nativeSuppression', 'nativeCommandsSuppressed', 'suppressed'):
            self.assertEqual(result[key], 0)

    def test_actual_mismatch_is_diagnostic_and_false_published_match_rejected(self):
        data = fixture()
        gpu = bytearray(data['gpu'])
        gpu[100] ^= 1
        data['gpu'] = bytes(gpu)
        data['snapshots'][0][28] = 2
        result = self.check(data)
        self.assertEqual((result['matched'], result['mismatched']), (3, 1))
        self.assertFalse(result['wholeInputEquivalenceProved'])
        self.assertFalse(result['inputContentsFrozen'])
        self.assertFalse(result['nativeExecutionPermitted'])
        data['snapshots'][0][28] = 1
        self.reject(data)

    def test_gpu_only_snapshot_does_not_certify_a_missing_cpu_candidate(self):
        data=fixture()
        for row in data['snapshots']:
            row[25]=row[26]=row[28]=0
        data['intent']=b''
        data['pieces']=[]
        result=self.check(data)
        self.assertEqual((result['copied'],result['gpuOnly'],result['matched']),(4,4,0))
        self.assertFalse(result['wholeInputEquivalenceProved'])
        data['snapshots'][0][28]=1
        self.reject(data)

    def test_composed_candidate_has_complete_actual_piece_receipts(self):
        data=fixture();pieces=[]
        for row in data['snapshots']:
            row[25]=row[0]+1;row[30]=1
            for index in range(2):
                begin=row[10]+index*1024
                pieces.append([row[0],row[1],len(pieces),row[25],0x1234,100+row[0]*2+index,0x5678,begin,index*1024,1024,begin,index*1024,1024,0,0,0])
        report=self.check(data,piece_raw=packed(pieces,16))
        self.assertEqual((report['composed'],report['matched']),(4,4))
        for field in (1,2,3,4,5,6,7,8,9,10,11,12,13):
            changed=[row[:] for row in pieces]
            changed[0][field]=0 if field in (4,6,9) else changed[0][field]+1
            with self.subTest(field=field):
                self.reject(data,piece_raw=packed(changed,16))
        self.reject(data)
        data = fixture()
        for value in (0, 2, 3):
            data['snapshots'][0][28] = value
            self.reject(data)

    def test_exact_source_eye_context_generation_and_raw_binding_records(self):
        for field in (0, 1, 12, 13, 15, 16, 17, 18, 22):
            with self.subTest(native_field=field):
                data = fixture()
                data['native'][0][field] += 1
                self.reject(data)
        for field in (0, 1, 4, 5, 12, 13, 14, 15, 20):
            with self.subTest(snapshot_field=field):
                data = fixture()
                data['snapshots'][0][field] += 1
                self.reject(data)
        for offset, size in ((0, 4), (6, 1), (8, 8), (16, 8), (0x48, 8)):
            with self.subTest(raw_record_offset=offset):
                data = fixture()
                raw = bytearray(struct.pack('<22Q', *data['native'][0][48:70]))
                raw[offset] ^= 1
                data['native'][0][48:70] = struct.unpack('<22Q', raw)
                self.reject(data)
        for offset in (0, 8, 10, 13, 14):
            with self.subTest(raw_slot_offset=offset):
                data = fixture()
                raw = bytearray(struct.pack('<2Q', *data['native'][0][72:74]))
                raw[offset] = 2 if offset == 13 else raw[offset] ^ 1
                data['native'][0][72:74] = struct.unpack('<2Q', raw)
                self.reject(data)

    def test_declined_native_candidate_and_unknown_state_have_no_gpu_claim(self):
        for native_status in (0, *range(2, 12)):
            with self.subTest(native_status=native_status):
                data = fixture(statuses=(3, 3, 3, 3), native_statuses=(native_status,) * 4)
                result = self.check(data)
                self.assertEqual((result['nativeCaptured'], result['copied'], result['declined'], result['gpuBytes']), (0, 0, 4, 0))
                self.assertEqual(result['matched'], 0)
                self.assertFalse(result['copyPermission'])
        for status in (0, 3, 4, 5, 6, 7, 8, 9):
            data = fixture(statuses=(3, 3, 3, 3), states=(status,) * 4)
            self.assertEqual(self.check(data)['declined'], 4)
            copied = fixture(states=(status, 2, 2, 2))
            self.reject(copied)
        data = fixture(statuses=(3, 3, 3, 3))
        data['snapshots'][0][21] = data['snapshots'][0][29] = 1
        self.assertEqual(self.check(data)['copied'], 0)
        for field in (16, 17, 18, 19, 22, 23, 24, 27, 28):
            data = fixture(statuses=(3, 3, 3, 3))
            data['snapshots'][0][field] = 1
            self.reject(data)

    def test_placed_and_shared_shape_are_declined_not_copy_certificates(self):
        placed = fixture(statuses=(3, 3, 3, 3))
        for meta, native in zip(placed['metadata'], placed['native']):
            meta[21:27] = [2, 1, 0, 0xE000, 65536, SOURCE_BYTES]
            native[24:34] = [1, 0xD000, 1, native[11], SOURCE_BYTES, 0xE000, 0xF000, 0xD100, 0xF000, 1]
        self.assertEqual(self.check(placed)['declined'], 4)
        for field, value in ((21, 2), (24, 0xE000), (25, 65536), (26, SOURCE_BYTES)):
            copied = fixture()
            copied['metadata'][0][field] = value
            self.reject(copied)
        shared = fixture(statuses=(3, 3, 3, 3))
        for meta, native, row, piece in zip(shared['metadata'], shared['native'],
                                          shared['snapshots'], shared['pieces']):
            native[9] = native[34] + SOURCE_BYTES // 2
            native[10] = SOURCE_BYTES // 2
            native[36:38] = [SOURCE_BYTES // 2, SOURCE_BYTES // 2]
            meta[11:13] = [4096, 4096]
            meta[14] += SOURCE_BYTES // 2
            row[10] = meta[14]
            piece[7] = piece[10] = row[10]
            raw_record(native)
        result = self.check(shared)
        self.assertEqual((result['nativeCaptured'], result['declined'], result['gpuBytes']), (4, 4, 0))
        self.assertFalse(result['wholeInputEquivalenceProved'])
        copied = fixture()
        copied['native'][0][10] -= 128
        raw_record(copied['native'][0])
        self.reject(copied)

    def test_empty_mask0_and_empty_occurrence_set(self):
        data = fixture(statuses=(2, 2, 2, 2))
        result = self.check(data)
        self.assertEqual((result['empty'], result['copied'], result['matched'], result['gpuBytes'], result['candidateBytes']), (4, 0, 0, 0, 0))
        self.assertFalse(result['wholeInputEquivalenceProved'])
        for field in (11, 16, 17, 18, 19, 21, 22, 23, 24, 25, 26, 27, 28, 29):
            data = fixture(statuses=(2, 2, 2, 2))
            data['snapshots'][0][field] = 128 if field == 11 else 1
            self.reject(data)
        data = fixture(statuses=(2, 2, 2, 2))
        data['native'].clear()
        data['metadata'].clear()
        data['snapshots'].clear()
        result = self.check(data)
        self.assertEqual((result['nativeCaptured'], result['copied'], result['empty'], result['declined']), (0, 0, 0, 0))

    def test_typed_metadata_private_bounds_and_exact_boolean_closure(self):
        for field, value in ((2, 0), (3, 2), (4, 22), (5, 4), (8, 1), (9, 0),
                             (11, 1), (12, 8191), (13, 64), (19, 0), (20, 1), (27, 0), (30, 1), (31, 1)):
            with self.subTest(metadata_field=field):
                data = fixture()
                data['metadata'][0][field] = value
                self.reject(data)
        for field in (6, 7, 8, 9, 21, 22, 23, 24, 29, 30, 31):
            with self.subTest(snapshot_marker=field):
                data = fixture()
                data['snapshots'][0][field] += 1
                self.reject(data)
        for field, value in ((16, 0x6000), (17, 1), (17, 8 * SOURCE_BYTES), (19, 0x6000),
                             (19, 0xB000), (10, 1), (10, SOURCE_BYTES), (11, 4097), (18, READ_BYTES - 1)):
            with self.subTest(snapshot_storage=field, value=value):
                data = fixture()
                data['snapshots'][0][field] = value
                self.reject(data)
        for field in (23, 42, 43, 44, 45, 47, 70, 71, 74, 95):
            with self.subTest(native_marker=field):
                data = fixture()
                data['native'][0][field] = 0 if field in (23, 42) else data['native'][0][field] + 1
                self.reject(data)

    def test_binary_counts_and_payload_extents_are_exact(self):
        data = fixture()
        for field, words in (('native_raw', 96), ('snapshot_raw', 32), ('metadata_raw', 32)):
            name = {'native_raw': 'native', 'snapshot_raw': 'snapshots', 'metadata_raw': 'metadata'}[field]
            raw = packed(data[name], words)
            for value in (raw[:-1], raw + b'x', raw[:words * 8], 'not binary'):
                self.reject(data, **{field: value})
        for field in ('gpu_blob', 'intent_blob'):
            name = {'gpu_blob': 'gpu', 'intent_blob': 'intent'}[field]
            for value in (b'', data[name][:-1], data[name] + b'x'):
                self.reject(data, **{field: value})
        for field in (26, 27):
            for value in (0, 2, 2**64 - 1):
                data = fixture()
                data['snapshots'][0][field] = value
                self.reject(data)
        data = fixture()
        self.assertTrue(self.check(data, gpu_blob=bytearray(data['gpu']), intent_blob=bytearray(data['intent']))['verified'])
        self.reject(data, gpu_blob=list(data['gpu']), intent_blob=list(data['intent']))


if __name__ == '__main__':
    unittest.main()
