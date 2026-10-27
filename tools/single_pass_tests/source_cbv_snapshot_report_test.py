"""Hermetic source-CBV snapshot receipts; no native process or GPU work."""

import struct
import unittest

from source_cbv_snapshot_report import ARENA_BYTES, COMMAND_BYTES, ROW_BYTES, analyze
from source_details_report import analyze as analyze_details
from source_details_report_test import b0, details_stats, fixture as detail_fixture, packed as packed_details
from source_cbv_producer_report import analyze as analyze_producers
from source_cbv_producer_report_test import producer_rows
from source_cbv_report import analyze as analyze_cbvs
from source_cbv_report_test import raw_rows as packed_cbvs, stats_for as cbv_stats


def packed(rows, words):
    return b''.join(struct.pack(f'<{words}Q', *row) for row in rows)


def snapshot_stats(rows, commands, source, state=3):
    return [state, source['stats'][1], len(commands), len(rows),
            sum(row[8] == 1 for row in rows), sum(row[8] == 2 for row in rows),
            sum(row[8] == 3 for row in rows), sum(row[20] for row in rows),
            sum(row[21] for row in rows), len({row[1] for row in rows if row[8] == 1 and row[29]}),
            sum(bool(command[18]) for command in commands),
            next((row[9] for row in rows if row[8] == 3 and row[9]), 0) or
            next((command[3] for command in commands if command[3]), 0),
            sum(bool(command[19]) for command in commands), 0, 0, 0]


def fixture(all_upload=False):
    source, cbvs, details = detail_fixture()
    for cbv, detail in zip(cbvs['rows'], details):
        if all_upload:
            detail[8] = 2
        if detail[8] == 1:
            detail[18:24] = [2, 1, 0, 0x800000, 0, cbv[14]]
            detail[28:31] = [2, 1, 3]
            detail[33] = 3
            b0(detail, cbv)
        else:
            detail[18:24] = [1, 1, 0xAC3, 0, 0, 0]
            detail[28:30] = [1, 0xAC3]
    details_report = analyze_details(details_stats(details, source), packed_details(details), source, cbvs)
    rows = []
    cursors = [0, 0]
    blob = bytearray()
    for index, (cbv, detail) in enumerate(zip(cbvs['rows'], details)):
        row = [0] * 32
        row[:8] = [*cbv[:7], cbv[11]]
        row[8] = 1 if detail[8] == 1 else 2
        row[10:17] = [detail[8], detail[18], cbv[12], cbv[10], cbv[10] - cbv[13], detail[33], cbv[21]]
        row[23:29] = [cbv[8], cbv[9], cbv[16], cbv[15], cbv[17], cbv[18]]
        if row[8] == 1:
            side = row[1]
            row[17:23] = [0xA000000 + side * 0x100, 0xB000000 + side * 0x1000000 + cursors[side],
                          cursors[side], cbv[6], cbv[11], 0xC000000 + side * 0x100]
            row[29:32] = [1, 1, len(blob) + 1]
            cursors[side] += cbv[11]
            blob.extend(bytes([index + 1]) * cbv[6])
        rows.append(row)
    commands = []
    for original in source['rows']:
        command = [0] * 24
        ordinal = original[0]
        copied = [row for row in rows if row[0] == ordinal and row[8] == 1]
        deferred = [row for row in rows if row[0] == ordinal and row[8] == 2]
        command[:3] = [ordinal, original[1], 1 if copied else 2]
        command[4:11] = [len(copied), len(deferred), len(copied) + len(deferred), len(copied),
                         sum(row[20] for row in copied), sum(row[21] for row in copied), copied[-1][17] if copied else 0]
        command[11:15] = [original[6], original[7], original[8], ordinal + 1]
        if copied:
            command[15:20] = [1] * 5
        commands.append(command)
    return source, cbvs, details_report, rows, commands, bytes(blob)


def direct_upload_fixture():
    source, cbvs, details, rows, commands, _ = fixture()
    cbvs['rows'][1][22] = 112
    cbvs = analyze_cbvs(cbv_stats(cbvs['rows'], source), packed_cbvs(cbvs['rows']), source)
    rows[1][8] = 1
    cursors, blob = [0, 0], bytearray()
    for index, row in enumerate(rows):
        if row[8] != 1:
            continue
        side = row[1]
        row[17:23] = [0xA000000 + side * 0x100, 0xB000000 + side * 0x1000000 + cursors[side],
                      cursors[side], row[6], row[7], 0xC000000 + side * 0x100]
        row[29:32] = [1, 1, len(blob) + 1]
        cursors[side] += row[7]
        blob.extend(bytes([index + 1]) * row[6])
    for command in commands:
        own = [row for row in rows if row[0] == command[0]]
        copied = [row for row in own if row[8] == 1]
        command[4:11] = [len(copied), sum(row[8] == 2 for row in own), len(own), len(copied),
                         sum(row[20] for row in copied), sum(row[21] for row in copied), copied[-1][17]]
    producer = analyze_producers(packed_cbvs(producer_rows(source, cbvs['rows'])), source, cbvs)
    return (source, cbvs, details, rows, commands, bytes(blob)), producer


def transient_upload_fixture():
    data,_=direct_upload_fixture()
    source,cbvs,details,rows,commands,_=data
    cbvs['rows'][2][7]=3
    cbvs=analyze_cbvs(cbv_stats(cbvs['rows'],source),packed_cbvs(cbvs['rows']),source)
    rows[2][8]=1
    cursors,blob=[0,0],bytearray()
    for index,row in enumerate(rows):
        if row[8]!=1:continue
        side=row[1]
        row[17:23]=[0xA000000+side*0x100,0xB000000+side*0x1000000+cursors[side],cursors[side],row[6],row[7],0xC000000+side*0x100]
        row[29:32]=[1,1,len(blob)+1];cursors[side]+=row[7];blob.extend(bytes([index+1])*row[6])
    for command in commands:
        own=[row for row in rows if row[0]==command[0]];copied=[row for row in own if row[8]==1]
        command[4:11]=[len(copied),sum(row[8]==2 for row in own),len(own),len(copied),sum(row[20] for row in copied),sum(row[21] for row in copied),copied[-1][17]]
    producers=[row+[0]*16 for row in producer_rows(source,cbvs['rows'])]
    p=producers[2];p[7]=cbvs['rows'][2][11];p[8]=0x1F08C2;p[17:22]=p[12:17]
    p[24:27]=[77,cbvs['rows'][2][22],0xF00000];p[27:32]=p[12:17];p[32]=1
    producer=analyze_producers(packed(producers,40),source,cbvs)
    return (source,cbvs,details,rows,commands,bytes(blob)),producer


class SourceCbvSnapshotReportTest(unittest.TestCase):
    def test_completed_transient_upload_owning_copy_and_partial_writer_rejection(self):
        data,producer=transient_upload_fixture()
        report=self.check(data,producer_report=producer)
        self.assertEqual(report['copiedUploadCount'],2)
        self.assertFalse(report['nativeExecutionPermitted'])
        producer['rows'][2][25]-=1
        with self.assertRaises(ValueError):self.check(data,producer_report=producer)

    def check(self, data, stats=None, raw_rows=None, raw_commands=None, **kwargs):
        source, cbvs, details, rows, commands, blob = data
        return analyze(snapshot_stats(rows, commands, source) if stats is None else stats,
                       packed(rows, 32) if raw_rows is None else raw_rows,
                       packed(commands, 24) if raw_commands is None else raw_commands,
                       source, cbvs, details, kwargs.get('blob', blob), kwargs.get('producer_report'))

    def reject(self, data, **kwargs):
        with self.assertRaises((ValueError, RuntimeError)):
            self.check(data, **kwargs)

    def test_mixed_default_upload_exact_logical_payload_and_no_permissions(self):
        data = fixture()
        report = self.check(data)
        self.assertTrue(report['verified'])
        self.assertTrue(report['gpuPayloadCaptured'])
        self.assertEqual([report[key] for key in ('copiedCount', 'deferredUploadCount', 'failedCount')], [3, 2, 0])
        self.assertEqual((report['copiedDefaultCount'], report['copiedUploadCount']), (3, 0))
        self.assertEqual(report['copiedBytes'], 1808)
        self.assertEqual(report['paddedBytes'], 2304)
        self.assertEqual(report['blobBytes'], len(data[-1]))
        self.assertEqual(report['finalizedArenas'], 2)
        self.assertEqual(report['rows'], data[3])
        self.assertEqual(report['commands'], data[4])
        for value in [report, *report['cbvs']]:
            for key in ('inputContentsFrozen', 'nativeExecutionPermission', 'nativeExecutionPermitted',
                        'performanceGain', 'performanceGainProved', 'copyPermission', 'privateCbvsConsumed'):
                self.assertIs(value[key], False)
            for key in ('nativeSuppression', 'nativeCommandsSuppressed', 'suppressed'):
                self.assertEqual(value[key], 0)
        for key in ('uploadContentsFrozen', 'sourceSnapshotInputsComplete', 'gpuInputEquivalenceProved'):
            self.assertIs(report[key], False)

    def test_genuine_direct_upload_owning_copy_closes_same_original_submission(self):
        data, producer = direct_upload_fixture()
        report = self.check(data, producer_report=producer)
        self.assertEqual((report['copiedCount'], report['copiedDefaultCount'], report['copiedUploadCount'], report['deferredUploadCount']), (4, 3, 1, 1))
        self.assertEqual((report['copiedBytes'], report['paddedBytes']), (1920, 2560))
        self.assertEqual(report['cbvs'][1]['statusName'], 'CopiedUpload')
        self.assertTrue(report['cbvs'][1]['arenaFinalized'])
        self.assertTrue(report['cbvs'][1]['sourceSubmissionValidated'])
        self.assertTrue(report['cbvs'][1]['gpuPayloadCaptured'])
        self.assertFalse(report['uploadContentsFrozen'])
        self.assertFalse(report['copyPermission'])
        self.reject(data)
        self.assertTrue(self.check(data, producer_report=producer, blob=None)['metadataOnly'])

    def test_direct_upload_requires_eligible_current_producer_membership(self):
        for field, value in ((7, 512), (8, 0), (9, 0x1F0BF7), (12, 0), (17, 0), (18, 99), (19, 99), (20, 99), (21, 99), (22, 0)):
            with self.subTest(field=field):
                data, producer = direct_upload_fixture()
                producer['rows'][1][field] = value
                self.reject(data, producer_report=producer)
        for field in ('sourceCommands', 'cbvCount', 'directEligibleCount'):
            data, producer = direct_upload_fixture()
            producer[field] += 1
            self.reject(data, producer_report=producer)
        for transform in (lambda rows: rows[:-1], lambda rows: rows + [rows[0][:]],
                          lambda rows: [rows[1], rows[0], *rows[2:]]):
            data, producer = direct_upload_fixture()
            producer['rows'] = transform(producer['rows'])
            self.reject(data, producer_report=producer)
        data, producer = direct_upload_fixture()
        producer['verified'] = False
        self.reject(data, producer_report=producer)
        data, producer = direct_upload_fixture()
        producer['rows'][1][7:9] = [0, 0]
        producer['rows'][1][17:23] = [0] * 6
        unknown = analyze_producers(b''.join(struct.pack('<40Q',*row) for row in producer['rows']), data[0], data[1])
        self.assertEqual(unknown['directEligibleCount'], 0)
        self.reject(data, producer_report=unknown)

    def test_upload_other_origins_gpu_upload_and_unknown_read_state_stay_declined(self):
        for field, value in ((18, 2), (19, 0), (20, 0), (26, 1), (27, 1), (28, 2), (29, 0)):
            with self.subTest(detail_field=field):
                data, producer = direct_upload_fixture()
                data[2]['rows'][1][field] = value
                self.reject(data, producer_report=producer)
        data, producer = direct_upload_fixture()
        data[3][2][8] = 1
        self.reject(data, producer_report=producer)
        data, producer = direct_upload_fixture()
        data[3][1][10] = 5
        data[2]['rows'][1][8] = 5
        self.reject(data, producer_report=producer)
        for field in (29, 30):
            data, producer = direct_upload_fixture()
            data[3][1][field] = 0
            self.reject(data, producer_report=producer)

    def test_direct_upload_payload_and_command_ranges_remain_exact(self):
        for value in (0, 1, 850):
            data, producer = direct_upload_fixture()
            data[3][1][31] = value
            self.reject(data, producer_report=producer)
        data, producer = direct_upload_fixture()
        self.reject(data, producer_report=producer, blob=data[-1][:-1])
        data[4][0][4] -= 1
        self.reject(data, producer_report=producer)

    def test_metadata_only_and_all_upload_empty_commands(self):
        data = fixture()
        report = self.check(data, blob=None)
        self.assertTrue(report['metadataOnly'])
        self.assertFalse(report['gpuPayloadCaptured'])
        self.assertTrue(all(not row['gpuPayloadCaptured'] for row in report['cbvs']))
        data = fixture(all_upload=True)
        report = self.check(data)
        self.assertEqual(report['copiedCount'], 0)
        self.assertEqual(report['deferredUploadCount'], 5)
        self.assertEqual(report['finalizedArenas'], 0)
        self.assertFalse(report['gpuPayloadCaptured'])
        self.assertTrue(all(command[2] == 2 for command in data[4]))
        self.reject(data, blob=b'x')

    def test_exact_source_cbv_and_detail_association(self):
        for field in (*range(8), *range(10, 17), *range(23, 29)):
            with self.subTest(field=field):
                data = fixture()
                data[3][0][field] += 1
                self.reject(data)
        data = fixture()
        data[3][1] = data[3][0][:]
        self.reject(data)
        data = fixture()
        data[3][0], data[3][3] = data[3][3], data[3][0]
        self.reject(data)
        data = fixture()
        data[2]['rows'][0][35] += 1
        self.reject(data)
        data = fixture()
        data[1]['ownedCount'] += 1
        self.reject(data)

    def test_private_alignment_owner_arena_and_reflected_not_padded_copy(self):
        for field, value in ((17, 0), (17, 0xA008), (18, 0), (18, 0xB000001),
                             (19, 1), (19, 256), (19, ARENA_BYTES), (20, 1024),
                             (21, 848), (22, 0), (22, 0xA000000), (22, 0xA008)):
            with self.subTest(field=field, value=value):
                data = fixture()
                data[3][0][field] = value
                self.reject(data)
        for field in (17, 18, 19, 22):
            data = fixture()
            data[3][4][field] += 256
            self.reject(data)
        data = fixture()
        data[3][3][17] = data[3][0][17]
        data[3][4][17] = data[3][0][17]
        for command in data[4]:
            command[10] = data[3][0][17]
        self.reject(data)

    def test_upload_deferred_and_default_read_state_fail_closed(self):
        for field in (*range(17, 23), 29, 30, 31):
            data = fixture()
            data[3][1][field] = 1
            self.reject(data)
        data = fixture()
        data[3][1][8] = 1
        self.reject(data)
        for field, value in ((19, 0), (26, 1), (27, 1), (28, 3), (29, 0), (29, 0x9), (30, 1)):
            data = fixture()
            data[2]['rows'][0][field] = value
            self.reject(data)
        for status in (0, 3, 4):
            data = fixture()
            data[3][0][8] = status
            self.reject(data)
        data = fixture()
        data[3][0][9] = 5
        self.reject(data)

    def test_command_closure_markers_association_and_aggregates(self):
        for field in (*range(15, 20), 29, 30):
            data = fixture()
            if field < 24:
                data[4][0][field] = 0
            else:
                data[3][0][field] = 0
            self.reject(data)
        for field in (*range(15), *range(20, 24)):
            data = fixture()
            data[4][0][field] += 1
            self.reject(data)
        data = fixture(all_upload=True)
        data[4][0][2] = 1
        self.reject(data)
        data = fixture(all_upload=True)
        data[4][0][15] = 1
        self.reject(data)

    def test_blob_exact_offsets_no_padding_overlap_gap_or_trailing_data(self):
        data = fixture()
        for blob in (b'', data[-1][:-1], data[-1] + b'x', data[-1] + bytes(496),
                     'not bytes', memoryview(data[-1])[::2]):
            self.reject(data, blob=blob)
        for index, value in ((0, 0), (0, 2), (3, 1), (3, 850), (4, 0), (4, 2**64 - 1)):
            data = fixture()
            data[3][index][31] = value
            self.reject(data)
            self.reject(data, blob=None)
        data = fixture()
        self.assertTrue(self.check(data, blob=memoryview(data[-1]))['gpuPayloadCaptured'])

    def test_stats_state_totals_and_binary_abi_failures(self):
        for field in range(16):
            data = fixture()
            stats = snapshot_stats(data[3], data[4], data[0])
            stats[field] += 1
            self.reject(data, stats=stats)
        data = fixture()
        self.assertEqual(len(packed(data[3], 32)), len(data[3]) * ROW_BYTES)
        self.assertEqual(len(packed(data[4], 24)), len(data[4]) * COMMAND_BYTES)
        for raw in (b'', packed(data[3], 32)[:-1], packed(data[3], 32) + b'x',
                    'not bytes', memoryview(packed(data[3], 32))[::2]):
            self.reject(data, raw_rows=raw)
        for raw in (b'', packed(data[4], 24)[:-1], 'not bytes',
                    memoryview(packed(data[4], 24)).cast('Q')):
            self.reject(data, raw_commands=raw)
        data = fixture()
        data[2]['verified'] = False
        self.reject(data)


if __name__ == '__main__':
    unittest.main()
