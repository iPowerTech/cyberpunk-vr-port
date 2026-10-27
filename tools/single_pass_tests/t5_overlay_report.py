"""Validate private t5 overlay receipts and bytes, never native execution."""

import math
import struct

from source_cbv_report import MAX_SOURCES, UINT64_MAX, _no_permission, _source_rows, _words

ROW_WORDS = 32
MAX_BYTES = 8 * 1024 * 1024
DOMAIN_BYTES = 8192 * 128


def _binary(value, name):
    if not isinstance(value, (bytes, bytearray, memoryview)) or (
            isinstance(value, memoryview) and (
                not value.c_contiguous or value.ndim != 1 or value.itemsize != 1)):
        raise ValueError(f'{name} is not a contiguous binary ABI buffer')
    return value


def _report_rows(report, key, words, limit, name):
    if not isinstance(report, dict) or report.get('verified') is not True:
        raise ValueError(f'Missing verified {name} report')
    values = report.get(key)
    if not isinstance(values, (list, tuple)) or len(values) > limit:
        raise ValueError(f'Invalid {name} report extent')
    for field, expected in _no_permission().items():
        if field in report and report[field] != expected:
            raise ValueError(f'{name} report acquired unrelated execution or copy permission')
    for field in ('privateCbvsConsumed', 'privateRangesConsumed', 'privateTablesConsumed'):
        if field in report and report[field] is not False:
            raise ValueError(f'{name} report acquired private input consumption')
    return [_words(value, words, name + ' row') for value in values]


def _payloads(rows, blob, status_field, bytes_field, offset_field, limit, name):
    blob = _binary(blob, name + ' blob')
    if len(blob) > limit:
        raise ValueError(f'{name} blob exceeds its bounded snapshot storage')
    result, cursor = {}, 0
    for index, row in enumerate(rows):
        if row[status_field] != 1:
            if row[offset_field]:
                raise ValueError(f'{name} noncopy acquired payload bytes')
            continue
        count = row[bytes_field]
        if not count or row[offset_field] != cursor + 1 or count > len(blob) - cursor:
            raise ValueError(f'{name} blob has a hole, overlap or truncation')
        result[index] = bytes(blob[cursor:cursor + count])
        cursor += count
    if cursor != len(blob):
        raise ValueError(f'{name} blob has unrelated trailing bytes')
    return result


def _b4_span(payload):
    if len(payload) != 228:
        raise ValueError('Overlay B4 is not the exact copied 228-byte logical view')
    base = struct.unpack_from('<f', payload, 128)[0]
    mask = struct.unpack_from('<I', payload, 224)[0]
    if not math.isfinite(base) or not 0 <= base < 2**32 or not mask:
        raise ValueError('Overlay B4 has no supported nonempty bounded read plan')
    records = [math.trunc(base) + bit for bit in range(32) if mask & (1 << bit)]
    if records[-1] >= 8192:
        raise ValueError('Overlay B4 records exceed the original structured domain')
    return records[0] * 128, (records[-1] - records[0] + 1) * 128


def analyze(stats, raw32Qrows, blob, source_report, cbv_snapshot_report,
            cbv_blob, t5_snapshot_report, t5_blob, source_assembly_report,
            cbv_copy_owners_raw):
    """Join already verified current snapshots; metadata never grants ownership."""
    stats = _words(stats, 16, 't5 overlay stats')
    native = stats[13] == 1
    raw = _binary(raw32Qrows, 't5 overlay rows')
    blob = _binary(blob, 't5 overlay blob')
    if len(raw) % (ROW_WORDS * 8) or len(raw) // (ROW_WORDS * 8) > MAX_SOURCES or len(blob) > MAX_BYTES:
        raise ValueError('Invalid bounded t5 overlay extent')
    rows = list(struct.iter_unpack('<32Q', raw))
    source_stats, sources = _source_rows(source_report)
    cbvs = _report_rows(cbv_snapshot_report, 'rows', 32, MAX_SOURCES * 28, 'source-CBV snapshot')
    cbv_commands = _report_rows(cbv_snapshot_report, 'commands', 24, MAX_SOURCES, 'source-CBV snapshot command')
    snapshots = _report_rows(t5_snapshot_report, 'snapshots', 32, MAX_SOURCES, 't5 snapshot')
    commands = _report_rows(source_assembly_report, 'commands', 32, MAX_SOURCES, 'source assembly')
    tables = _report_rows(source_assembly_report, 'tables', 16, 65536, 'source assembly table')
    if cbv_snapshot_report.get('metadataOnly') is not False:
        raise ValueError('Overlay requires actual captured B4 GPU bytes')
    cbv_stats = _words(cbv_snapshot_report.get('stats'), 16, 'source-CBV snapshot stats')
    assembly_stats = _words(source_assembly_report.get('stats'), 16, 'source assembly stats')
    if cbv_stats[:4] != [3, source_stats[1], len(sources), len(cbvs)] or any(cbv_stats[13:]) or (
            assembly_stats[:3] != [3, source_stats[1], len(sources)] or any(assembly_stats[12:])):
        raise ValueError('Overlay input reports differ from the current source frame')
    owner_raw = _binary(cbv_copy_owners_raw, 'source-CBV copy owners')
    if len(commands) != len(sources) or len(cbv_commands) != len(sources) or len(owner_raw) != len(sources) * 8 * 8:
        raise ValueError('Overlay assembly lost its exact source occurrence set')
    copy_owners = list(struct.iter_unpack('<8Q', owner_raw))
    copied_by_source = [[] for _ in sources]
    cbv_keys = set()
    for cbv in cbvs:
        key = tuple(cbv[:6])
        if key in cbv_keys or cbv[0] >= len(sources) or cbv[1] != sources[cbv[0]][1] or cbv[25:27] != [sources[cbv[0]][7], sources[cbv[0]][8]]:
            raise ValueError('Overlay CBV input belongs to another source occurrence')
        cbv_keys.add(key)
        if cbv[8] == 1:
            copied_by_source[cbv[0]].append(cbv)
    table_cursor = 0
    source_tables = []
    for ordinal, (command, source) in enumerate(zip(commands, sources)):
        if command[:2] != [ordinal, source[1]] or command[11:15] != [source[6], source[7], ordinal + 1, source[11]] or any(command[23:26]):
            raise ValueError('Overlay assembly scope differs from its original source')
        if command[27] != table_cursor or command[28] > len(tables) - table_cursor:
            raise ValueError('Overlay assembly table ranges have gaps or truncation')
        selected = tables[table_cursor:table_cursor + command[28]]
        if any(table[:2] != [ordinal, source[1]] for table in selected):
            raise ValueError('Overlay assembly table belongs to another source occurrence')
        source_tables.append({table[2]: table for table in selected})
        if len(source_tables[-1]) != len(selected):
            raise ValueError('Overlay assembly duplicated an original table domain')
        table_cursor += command[28]
        copied_cbvs = copied_by_source[ordinal]
        cbv_command, owner = cbv_commands[ordinal], copy_owners[ordinal]
        if cbv_command[:2] != [ordinal, source[1]] or cbv_command[11:15] != [source[6], source[7], source[8], ordinal + 1] or (
                owner[:5] != (ordinal, source[1], source[6], source[7], ordinal + 1) or
                owner[6:] != (cbv_command[7], cbv_command[8]) or
                owner[6:] != (len(copied_cbvs), sum(row[20] for row in copied_cbvs))):
            raise ValueError('Overlay B4 copy owner borrowed another source recording or aggregate')
        if copied_cbvs:
            if cbv_command[2:4] != [1, 0] or not owner[5] or cbv_command[15:20] != [1] * 5:
                raise ValueError('Overlay B4 copy owner lacks its finalized current source submission')
        elif cbv_command[2:4] != [2, 0] or any(owner[5:]):
            raise ValueError('Deferred CBV command acquired a copy owner')
    if table_cursor != len(tables):
        raise ValueError('Overlay assembly has unrelated trailing table rows')
    cbv_payloads = _payloads(cbvs, cbv_blob, 8, 20, 31, MAX_BYTES * 2, 'source-CBV snapshot')
    t5_payloads = _payloads(snapshots, t5_blob, 2, 18, 27, MAX_BYTES, 't5 snapshot')
    by_source = {}
    for index, snapshot in enumerate(snapshots):
        ordinal = snapshot[0]
        if ordinal >= len(sources) or ordinal in by_source or snapshot[1] != sources[ordinal][1]:
            raise ValueError('Overlay t5 snapshot occurrence is foreign or duplicated')
        by_source[ordinal] = (index, snapshot)

    if stats[0] == 0:
        if stats[1] not in (0, source_stats[1]) or any(stats[k] for k in range(16) if k != 1) or rows or blob:
            raise ValueError('Absent overlay acquired versions, storage or completion')
    elif stats[0] != 2:
        raise RuntimeError('Private t5 overlay has not reached destination GPU completion')
    elif (stats[1] != source_stats[1] or not rows or stats[2] != len(rows) or stats[11] != len(rows) or
          not stats[3] or not stats[4] or stats[3] > MAX_BYTES * (2 if native else 1) or stats[4] > MAX_BYTES or
          stats[13] not in (0, 1) or any(stats[14:]) or
          (native and any(stats[5:11])) or
          (not native and (not all(stats[k] for k in (5, 6, 7, 8, 9)) or stats[10] == UINT64_MAX or stats[10] < stats[9]))):
        raise ValueError('Overlay completion, bounds or nonexecution markers are invalid')

    seen, resource_owner, cursor, required, copied, attached = set(), None, 0, 0, 0, 0
    native_backings = {}
    entries = []
    for row in rows:
        ordinal, eye = row[:2]
        if ordinal >= len(sources) or ordinal in seen or ordinal not in by_source:
            raise ValueError('Overlay lost exact source membership or duplicated an occurrence')
        seen.add(ordinal)
        source, command = sources[ordinal], commands[ordinal]
        if row[:4] != (ordinal, source[1], 1, 0) or row[4:8] != (source[11], source[6], source[7], ordinal + 1):
            raise ValueError('Overlay borrowed another eye, root, list, generation or occurrence')
        if not all(row[k] for k in (8, 9, 10, 11, 14, 19, 20, 21, 22, 24, 25)) or row[17:19] != (8192, 128) or (
                row[24] != 228 or row[26:28] != (1, 1) or row[28] not in (0, 1) or row[31] != 1 or
                row[29:31] != (tuple(source[6:8]) if native else tuple(stats[5:7]))):
            raise ValueError('Overlay owners, structured domain or actual closure markers differ')
        if row[8] == row[19] or (row[8] == row[10]) != native or row[9] in (row[8], row[10], row[11], row[19], row[20], row[21]) or (
                row[11] in (row[10], row[19]) or row[20] in (row[19], row[8], row[10]) or row[21] in (row[19], row[8], row[10])):
            raise ValueError('Overlay aliases its original dense or B4 storage owners')
        pair = (row[8], row[9])
        if native:
            backing = native_backings.setdefault(row[8], dict(owner=row[11], scope=tuple(source[6:8]) + (eye,), end=0, width=0))
            if backing['owner'] != row[11] or backing['scope'] != tuple(source[6:8]) + (eye,):
                raise ValueError('Native private backing borrowed another source recording')
            if resource_owner is not None and resource_owner != row[9]:
                raise ValueError('Native views lost their shared adoption owner')
            resource_owner = row[9]
        elif resource_owner is None:
            resource_owner = pair
        elif resource_owner != pair:
            raise ValueError('One overlay acquired unrelated private backing versions')
        index, snapshot = by_source[ordinal]
        if snapshot[2:4] != [1, 0] or row[8] == snapshot[4] or snapshot[14:16] != [source[8], source[7]] or snapshot[20] != ordinal + 1 or (
                snapshot[21:25] != [1, 1, 1, 1] or snapshot[29] != 1 or
                row[10:15] != (snapshot[16], snapshot[19], snapshot[17], snapshot[10], snapshot[18])):
            raise ValueError('Overlay dense receipt differs from the finalized current t5 copy')
        if row[12] % 128 or row[13] % 128 or row[14] > 4096 or row[14] % 128 or row[13] > DOMAIN_BYTES or row[14] > DOMAIN_BYTES - row[13] or (
                row[12] > (MAX_BYTES if native else DOMAIN_BYTES) or row[14] > (MAX_BYTES if native else DOMAIN_BYTES) - row[12]):
            raise ValueError('Overlay selected span exceeds the original structured domain')
        b4_index = row[25] - 1
        if b4_index >= len(cbvs) or b4_index not in cbv_payloads:
            raise ValueError('Overlay B4 snapshot index is missing or deferred')
        b4 = cbvs[b4_index]
        if b4[:8] != [ordinal, eye, 5, 4, 19, 0, 228, 256] or b4[8:10] != [1, 0] or b4[25:27] != [source[7], source[8]] or (
                b4[29:31] != [1, 1] or (row[19], row[20], row[22], row[23], row[24]) !=
                (b4[17], b4[22], b4[18], b4[19], b4[20]) or row[21] != copy_owners[ordinal][5]):
            raise ValueError('Overlay B4 physical view, owner or original scope differs')
        if b4[18] % 256 or b4[19] % 256 or b4[18] <= b4[19] or b4[18] > UINT64_MAX - 256 or b4[19] > MAX_BYTES - 256 or (
                b4[21] != 256 or _b4_span(cbv_payloads[b4_index]) != (row[13], row[14])):
            raise ValueError('Overlay B4 GPU payload does not select its exact current t5 span')
        target = max(backing['end'] if native else cursor, row[13])
        first = (target - row[13]) // 128
        if row[15:17] != (target, first) or (row[16] + row[13] // 128) * 128 != row[15] or (
                row[16] * 128 > stats[3] or DOMAIN_BYTES > stats[3] - row[16] * 128):
            raise ValueError('Overlay mapping rebased indices or escaped its full structured view')
        cursor = target + row[14]
        if native:
            if row[12] != target or row[16] * 128 + DOMAIN_BYTES > MAX_BYTES:
                raise ValueError('Native view does not reuse the actual original GPU copy')
            backing['end'] = cursor
            backing['width'] = max(backing['width'], row[16] * 128 + DOMAIN_BYTES)
            required = sum(value['width'] for value in native_backings.values())
        else:
            required = max(required, row[16] * 128 + DOMAIN_BYTES)
        if cursor > stats[3] or row[14] > len(blob) - copied or bytes(blob[copied:copied + row[14]]) != t5_payloads.get(index):
            raise ValueError('Overlay actual GPU bytes differ from its exact current t5 snapshot')
        copied += row[14]
        attached += row[28]
        if row[28] and (command[2] != 1 or command[4] != 1 or command[21] != 1 or command[26]):
            raise ValueError('Overlay attachment did not reach successful CPU assembly')
        if row[28] and any(parameter not in source_tables[ordinal] or
                              source_tables[ordinal][parameter][3] <= offset or
                              not source_tables[ordinal][parameter][12]
                              for parameter, offset in ((19, 0), (23, 5))):
            raise ValueError('Overlay attachment lost its original B4 or t5 assembled table domain')
        entries.append(dict(sourceOrdinal=ordinal, side=eye, sourceOffset=row[13], copiedBytes=row[14],
                            targetOffset=row[15], privateFirstElement=row[16], overrideAttached=bool(row[28]),
                            gpuByteMatchVerified=True, privateT5Consumed=False, **_no_permission()))
    if (required, copied, len(rows), attached) != (stats[3], stats[4], stats[11], stats[12]) or copied != len(blob):
        raise ValueError('Overlay aggregates or dense packed payload extent differ from its rows')
    return dict(verified=True, stats=stats, rows=[list(row) for row in rows],
                cbvCopyOwners=[list(row) for row in copy_owners], versions=len(rows),
                requiredBytes=required, copiedBytes=copied, blobBytes=len(blob), attached=attached,
                destinationFenceComplete=stats[0] == 2 and not native, sourceCopiesReused=native,
                additionalGpuCopies=0 if native else len(rows), gpuBytesMatched=len(rows), overlays=entries,
                privateT5Consumed=False, wholeInputEquivalenceProved=False, **_no_permission())
