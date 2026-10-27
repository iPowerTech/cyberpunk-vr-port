"""Exact-source compact selector receipts, not a rendering admission certificate."""

import struct
from collections import Counter

from source_cbv_report import _source_rows, _words, _no_permission
from source_cbv_snapshot_report import _raw_rows


def analyze(stats, raw_commands, raw_inputs, raw_slots, raw_t5, intent_blob,
            source_report, snapshot_report, gpu_blob, t5_report=None):
    stats = _words(stats, 16, 'source selector stats')
    source_stats, sources = _source_rows(source_report)
    if stats[0] != 3 or stats[1] != source_stats[1] or stats[2] != len(sources) or stats[15]:
        raise ValueError('Source selectors did not close the exact source occurrence set')
    commands = _raw_rows(raw_commands, 32, stats[2], 2048, 'selector commands')
    inputs = _raw_rows(raw_inputs, 24, stats[3], 4096, 'selector inputs')
    slots = _raw_rows(raw_slots, 16, stats[4], 65536, 'selector slots')
    t5 = _raw_rows(raw_t5, 32, stats[5], 2048, 'selector t5')
    t5_by_ordinal = {row[0]: row for row in t5}
    if not isinstance(snapshot_report, dict) or snapshot_report.get('verified') is not True:
        raise ValueError('Missing independently checked GPU snapshot report')
    snapshots = snapshot_report['rows']
    grouped_inputs, grouped_slots = {}, {}
    seen, cursor = set(), 0
    for row in inputs:
        ordinal, eye, visibility, register, parameter, offset, size = row[:7]
        key = tuple(row[:6])
        if ordinal >= len(sources) or eye != sources[ordinal][1] or key in seen or visibility not in (1, 5) or register != 4:
            raise ValueError('Selector input original occurrence or b4 identity differs')
        seen.add(key)
        if (row[21], row[22]) != (sources[ordinal][7], sources[ordinal][8]) or not 0 < size <= 65536:
            raise ValueError('Selector input recording or logical range differs')
        if row[11] != ordinal + 1:
            raise ValueError('Selector input lost its original callback occurrence label')
        if parameter != (1 if visibility == 1 else 19) or offset or row[7] not in (0, 1) or row[13] not in (0, 1, 2) or row[23]:
            raise ValueError('Selector input layout/status or ledger permission differs')
        if not sources[ordinal][13] & (1 << parameter):
            raise ValueError('Selector b4 parameter was not initialized at its original occurrence')
        if row[7]:
            if not all(row[n] for n in (8, 9, 10, 11, 15, 16, 17, 18, 19, 20)) or row[16] != size or row[15] != cursor + 1:
                raise ValueError('Provisional selector input lost its exact immutable payload')
            if size > len(intent_blob) - cursor:
                raise ValueError('Selector intent blob is truncated')
            intended = intent_blob[cursor:cursor + size]
            cursor += size
        elif any(row[n] for n in (8, 9, 10, 12, 13, 14, 15, 16)):
            raise ValueError('Missing provisional input acquired payload or GPU verification')
        if row[13]:
            if not row[14] or row[14] > len(snapshots):
                raise ValueError('GPU match lacks an exact original snapshot')
            snapshot = snapshots[row[14] - 1]
            if tuple(snapshot[:7]) != tuple(row[:7]) or snapshot[8] != 1 or (snapshot[12], snapshot[13], snapshot[23], snapshot[24], snapshot[25], snapshot[26]) != (
                    row[19], row[20], row[17], row[18], row[21], row[22]) or not all(snapshot[n] for n in (29, 30, 31)):
                raise ValueError('GPU match borrowed another original input or recording')
            start = snapshot[31] - 1
            if snapshot[20] != size or start > len(gpu_blob) or size > len(gpu_blob) - start:
                raise ValueError('GPU selector snapshot payload is truncated')
            matched = intended == gpu_blob[start:start + size]
            if row[13] != (1 if matched else 2):
                raise ValueError('Published GPU byte match differs from the actual payloads')
        grouped_inputs.setdefault(ordinal, []).append(row)
    if cursor != len(intent_blob):
        raise ValueError('Selector intent blob has unrelated trailing bytes')
    seen = set()
    for row in slots:
        ordinal, eye, parameter, original_offset, compact_index, kind = row[:6]
        key = ordinal, parameter, original_offset
        if ordinal >= len(sources) or eye != sources[ordinal][1] or key in seen or kind or row[8] != 2 or parameter >= 64 or row[15] or not 0 <= original_offset < row[14] <= 32768:
            raise ValueError('Compact slot original index/type/range differs')
        seen.add(key)
        if not sources[ordinal][13] & (1 << parameter) or not all(row[n] for n in (6, 7, 8, 10, 11, 12, 13)):
            raise ValueError('Compact slot lacks current descriptor/owner/table identity')
        grouped_slots.setdefault(ordinal, []).append(row)
    statuses = Counter()
    for ordinal, command in enumerate(commands):
        source = sources[ordinal]
        if command[:2] != (ordinal, source[1]) or command[2] not in range(1, 7) or tuple(command[13:18]) != (
                source[6], source[7], source[8], source[11], source[12]) or command[12] or any(command[30:]) or command[25] not in (0, 1):
            raise ValueError('Selector command original identity/status or permission differs')
        own_inputs, own_slots = grouped_inputs.get(ordinal, []), grouped_slots.get(ordinal, [])
        if command[6] != len(own_inputs) or command[7] != sum(row[7] for row in own_inputs) or command[10] != len(own_slots):
            raise ValueError('Selector command counts differ from owned inputs/slots')
        if command[11] not in (0, 1) or command[24] not in (0, 1):
            raise ValueError('Selector command verification flags are invalid')
        if command[2] != 6 and command[5] != ordinal + 1:
            raise ValueError('Selector command lost its original callback occurrence label')
        if command[2] == 1:
            tables = {}
            for row in own_slots:
                tables.setdefault(row[2], []).append(row)
            empty_multilayer = command[25] and not command[8] and not own_slots
            if (not empty_multilayer and (not command[8] or not own_slots)) or command[9] != len(tables):
                raise ValueError('Captured selector command is incomplete')
            for rows in tables.values():
                if rows[0][14] != max(row[3] for row in rows) + 1:
                    raise ValueError('Compact table extent differs from the exact evaluated offsets')
                if [row[4] for row in rows] != list(range(len(rows))) or [row[3] for row in rows] != sorted(row[3] for row in rows):
                    raise ValueError('Compact descriptor offsets were rewritten or duplicated')
                if len({tuple(row[10:15]) for row in rows}) != 1:
                    raise ValueError('Compact table immutable ownership differs across slots')
            expected_match = bool(own_inputs) and not command[23] and all(row[13] == 1 for row in own_inputs)
            if command[25]:
                if not isinstance(t5_report, dict) or t5_report.get('verified') is not True or not 0 < command[28] <= len(t5_report['snapshots']):
                    raise ValueError('Multilayer command lacks its independently checked t5 receipt')
                receipt = t5_report['snapshots'][command[28] - 1]
                view_unchanged = ordinal in t5_by_ordinal and t5_by_ordinal[ordinal][29] == 1
                if receipt[:2] != list(command[:2]) or command[26] != (receipt[2] == 1) or command[27] != (view_unchanged and (receipt[28] == 1 or receipt[2] == 2)):
                    raise ValueError('Multilayer command t5 copy/match status differs')
                expected_match = expected_match and bool(command[27])
            elif any(command[26:30]):
                raise ValueError('Non-multilayer command acquired t5 markers')
            if command[11] != expected_match:
                raise ValueError('Command GPU-match status differs from its exact inputs')
        elif own_slots or command[11]:
            raise ValueError('Uncaptured command acquired compact descriptors or GPU proof')
        statuses[command[2]] += 1
    seen = set()
    for row in t5:
        ordinal = row[0]
        if ordinal >= len(sources) or row[1] != sources[ordinal][1] or ordinal in seen or row[2] not in (0, 1) or row[27] != sources[ordinal][7] or any(row[n] for n in (20, 30, 31)):
            raise ValueError('Multilayer t5 source identity or copy permission differs')
        seen.add(ordinal)
        if row[2] and (row[4:6] != (23, 5) or row[8] != 2 or row[13] != 128 or not sources[ordinal][13] & (1 << 23) or not all(row[n] for n in (6, 7, 8, 9, 10, 12))):
            raise ValueError('Current t5 structured view metadata is incomplete')
        if row[19] and (row[18] != row[17].bit_length() or row[14] != (row[11] + row[16]) * 128 or row[15] != row[18] * 128 or row[16] + row[18] > row[12]):
            raise ValueError('Multilayer bounded record span differs')
        if row[28] and (not row[19] or not grouped_inputs.get(ordinal) or grouped_inputs[ordinal][0][13] != 1):
            raise ValueError('Multilayer record bound lacks its actual GPU-matched b4')
    expected = [statuses[1], statuses[2], sum(statuses[n] for n in range(3, 7)), sum(row[8] for row in commands),
                sum(bool(row[11]) for row in commands), sum(row[7] for row in inputs), sum(bool(row[13]) for row in inputs)]
    if stats[6:13] != expected:
        raise ValueError('Selector aggregate counters differ from exact source receipts')
    return dict(verified=True, stats=stats, commands=[list(row) for row in commands], inputs=[list(row) for row in inputs],
                slots=[list(row) for row in slots], t5=[list(row) for row in t5], blobBytes=len(intent_blob),
                capturedCommands=statuses[1], deferredMultilayerCommands=statuses[2], declinedCommands=expected[2],
                gpuMatchedCommands=expected[4], compactDescriptors=len(slots),
                gpuMismatchedInputs=sum(row[13] == 2 for row in inputs), currentT5Views=sum(row[2] for row in t5),
                gpuMatchedT5Bounds=sum(row[28] for row in t5), registeredLedgerCertified=False,
                multilayerCapturedCommands=sum(row[25] and row[2] == 1 for row in commands),
                multilayerGpuMatchedCommands=sum(row[25] and row[11] == 1 for row in commands),
                shaderDrawConsumptionProved=False, allSourceInputsComplete=False, privateTablesConsumed=False,
                **_no_permission())
