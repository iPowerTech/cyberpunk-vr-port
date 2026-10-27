"""Validate source-CBV owning copy receipts and exact readback bytes.

These snapshots are not yet consumed by the private CBV bank. A metadata-only
check (blob=None) verifies receipts but cannot certify captured GPU payload.
"""

import struct

from source_cbv_report import (
    MAX_SOURCES, UINT64_MAX, _no_permission, _source_rows, _words,
    analyze as analyze_cbvs,
)
from source_details_report import analyze as analyze_details
from source_cbv_producer_report import analyze as analyze_producers

ROW_WORDS = 32
ROW_BYTES = ROW_WORDS * 8
COMMAND_WORDS = 24
COMMAND_BYTES = COMMAND_WORDS * 8
MAX_ROWS = MAX_SOURCES * 28
ARENA_BYTES = 8 * 1024 * 1024
GENERIC_READ = 0xAC3


def _binary(raw, name):
    if not isinstance(raw, (bytes, bytearray, memoryview)) or (
            isinstance(raw, memoryview) and (
                not raw.c_contiguous or raw.itemsize != 1 or raw.ndim != 1)):
        raise ValueError(f'{name} is not a contiguous binary ABI buffer')
    return raw


def _raw_rows(raw, words, count, limit, name):
    raw = _binary(raw, name)
    if count > limit or len(raw) != count * words * 8:
        raise ValueError(f'Invalid {name} extent')
    try:
        return list(struct.iter_unpack(f'<{words}Q', raw))
    except (TypeError, BufferError, struct.error) as error:
        raise ValueError(f'Invalid {name} binary ABI') from error


def _report_rows(report, words, count, name):
    if not isinstance(report, dict) or report.get('verified') is not True:
        raise ValueError(f'Missing verified {name} report')
    values = report.get('rows')
    if not isinstance(values, (list, tuple)) or len(values) != count or count > MAX_ROWS:
        raise ValueError(f'Invalid {name} report extent')
    return [_words(row, words, f'{name} row') for row in values]


def _source_reports(source_report, cbv_report, details_report):
    source_stats, sources = _source_rows(source_report)
    if not isinstance(cbv_report, dict):
        raise ValueError('Missing source-CBV report')
    cbv_stats = _words(cbv_report.get('stats'), 16, 'source-CBV stats')
    cbvs = _report_rows(cbv_report, 24, cbv_stats[3], 'source-CBV')
    checked = analyze_cbvs(cbv_stats, b''.join(struct.pack('<24Q', *row) for row in cbvs), source_report)
    for key in ('sourceCommands', 'cbvCount', 'ownedCount'):
        if type(cbv_report.get(key)) is not int or cbv_report[key] != checked[key]:
            raise ValueError(f'Source-CBV {key} differs from its rows')
    details = _report_rows(details_report, 128, len(cbvs), 'source-detail')
    detail_stats = _words(details_report.get('stats'), 16, 'source-detail stats')
    analyze_details(detail_stats, b''.join(struct.pack('<128Q', *row) for row in details), source_report, cbv_report)
    return source_stats, sources, cbvs, details


def analyze(stats, raw32Qrows, raw24Qcommands, source_report, cbv_report,
            details_report, blob=b'', producer_report=None):
    """Raise on torn or incomplete receipts; blob=None checks metadata only."""
    stats = _words(stats, 16, 'source-CBV snapshot stats')
    if stats[0] != 3 or not stats[1] or any(stats[13:]):
        raise ValueError('Source-CBV snapshots did not reach GPU completion')
    source_stats, sources, cbvs, details = _source_reports(source_report, cbv_report, details_report)
    producers = {}
    if producer_report is not None:
        producer_rows = _report_rows(producer_report, 40, len(cbvs), 'source-CBV producer')
        verified_producers = analyze_producers(b''.join(struct.pack('<40Q', *row) for row in producer_rows), source_report, cbv_report)
        for field in ('sourceCommands', 'cbvCount', 'directEligibleCount'):
            if type(producer_report.get(field)) is not int or producer_report[field] != verified_producers[field]:
                raise ValueError('Source-CBV producer aggregate differs: ' + field)
        producers = {tuple(row[:6]): row for row in producer_rows}
    if stats[1:4] != [source_stats[1], len(sources), len(cbvs)]:
        raise ValueError('Source-CBV snapshots differ from the exact source occurrence set')
    rows = _raw_rows(raw32Qrows, ROW_WORDS, stats[3], MAX_ROWS, 'source-CBV snapshot rows')
    commands = _raw_rows(raw24Qcommands, COMMAND_WORDS, stats[2], MAX_SOURCES, 'source-CBV snapshot commands')
    if blob is not None:
        blob = _binary(blob, 'source-CBV snapshot blob')

    known = {tuple(cbv[:6]): (cbv, detail) for cbv, detail in zip(cbvs, details)}
    seen = set()
    grouped = [[] for _ in sources]
    arenas = {}
    totals = [0, 0, 0, 0, 0]
    blob_cursor = 0
    prior_ordinal = 0
    entries = []
    copied_default = copied_upload = 0
    for row in rows:
        ordinal, side = row[:2]
        key = tuple(row[:6])
        if ordinal >= len(sources) or ordinal < prior_ordinal or key not in known or key in seen:
            raise ValueError('Source-CBV snapshot association/order/uniqueness differs')
        seen.add(key)
        prior_ordinal = ordinal
        source = sources[ordinal]
        cbv, detail = known[key]
        if row[:8] != (*cbv[:7], cbv[11]) or row[10:17] != (
                detail[8], detail[18], cbv[12], cbv[10], cbv[10] - cbv[13], detail[33], cbv[21]) or (
                row[23:29] != (cbv[8], cbv[9], cbv[16], cbv[15], cbv[17], cbv[18])):
            raise ValueError('Source-CBV snapshot backing, view or recording identity differs')
        if side != source[1] or row[8] not in (1, 2, 3) or row[29] not in (0, 1) or row[30] not in (0, 1):
            raise ValueError('Source-CBV snapshot eye/status/completion flags are invalid')
        if row[8] == 3 or row[9]:
            raise RuntimeError(f'Source-CBV snapshot {ordinal} failed: {row[9]}')
        totals[row[8] - 1] += 1
        totals[3] += row[20]
        totals[4] += row[21]
        grouped[ordinal].append(row)

        if row[8] == 2:
            if row[10] not in (2, 5) or any(row[17:23]) or any(row[29:]):
                raise ValueError('Deferred UPLOAD CBV acquired private storage, bytes or completion markers')
        else:
            if row[10] == 2:
                producer = producers.get(key)
                if not producer or not (producer[22] == 1 or producer[32] == 1) or detail[18:21] != [1, 1, GENERIC_READ] or detail[26] or detail[27] or (
                        detail[28:30] != [1, GENERIC_READ]):
                    raise ValueError('Copied UPLOAD CBV lacks its completed producer, committed backing or fixed original read state')
                copied_upload += 1
            else:
                if row[10] != 1 or row[11] not in (1, 2) or row[15] not in (1, 3) or not detail[19] or detail[26] or detail[27]:
                    raise ValueError('Copied DEFAULT CBV lacks matching resource creation/read context')
                read_only = (row[15] == 1 and detail[30] == 1 and detail[28] in (8, 9) and detail[29] == GENERIC_READ) or (
                    detail[28] == 2 and detail[30] == 3 and detail[29] & 1 and not detail[29] & ~GENERIC_READ)
                if not read_only:
                    raise ValueError('Copied DEFAULT CBV lacks its original read-only state observation')
                copied_default += 1
            resource, address, offset, copied, padded, owner = row[17:23]
            if (not resource or not address or not owner or resource == row[12] or owner in (resource, row[12]) or
                    address % 256 or offset % 256 or address <= offset or copied != row[6] or padded != row[7] or
                    offset > ARENA_BYTES or padded > ARENA_BYTES - offset or address > UINT64_MAX - padded or
                    row[29:31] != (1, 1)):
                raise ValueError('Private CBV storage, logical bytes or closure is invalid')
            arena = arenas.setdefault(side, [resource, owner, address - offset, 0, tuple(source[6:9])])
            if arena[:3] != [resource, owner, address - offset] or offset != arena[3] or arena[4] != tuple(source[6:9]):
                raise ValueError('Private CBV arena identity or contiguous padded offsets differ')
            arena[3] += padded
            if row[31] != blob_cursor + 1:
                raise ValueError('Source-CBV snapshot blob offsets have a gap or overlap')
            blob_cursor += copied
            if blob is not None and blob_cursor > len(blob):
                raise ValueError('Source-CBV snapshot blob is truncated')
        entries.append(dict(sourceOrdinal=ordinal, side=side, visibility=row[2], shaderRegister=row[3],
            parameter=row[4], tableOffset=row[5], reflectedLogicalBytes=row[6], descriptorBytes=row[7],
            status=row[8], statusName=('CopiedUpload' if row[10] == 2 else 'CopiedDefault') if row[8] == 1 else 'DeferredUpload', heapType=row[10],
            sourceResource=row[12], sourceGpuAddress=row[13], sourceOffset=row[14], privateResource=row[17],
            privateGpuAddress=row[18], privateOffset=row[19], copiedBytes=row[20], paddedBytes=row[21],
            privateOwner=row[22], arenaFinalized=bool(row[29]), sourceSubmissionValidated=bool(row[30]),
            blobOffset=row[31] - 1 if row[31] else None,
            gpuPayloadCaptured=blob is not None and row[8] == 1,
            privateCbvsConsumed=False, **_no_permission()))
    if len(seen) != len(cbvs):
        raise ValueError('Source-CBV snapshots omitted a reflected source CBV')
    if len({arena[0] for arena in arenas.values()}) != len(arenas) or len({arena[1] for arena in arenas.values()}) != len(arenas):
        raise ValueError('Distinct eye recordings share a private CBV arena')
    if blob is not None and len(blob) != blob_cursor:
        raise ValueError('Source-CBV snapshot blob has trailing or missing bytes')

    submitted = prepared = 0
    for ordinal, (command, source, source_rows) in enumerate(zip(commands, sources, grouped)):
        if command[:2] != (ordinal, source[1]) or command[11:15] != (source[6], source[7], source[8], ordinal + 1):
            raise ValueError('Source-CBV snapshot command lost its exact recording occurrence')
        if command[2] not in (1, 2, 3) or any(value not in (0, 1) for value in command[15:20]) or any(command[20:]):
            raise ValueError('Source-CBV snapshot command status/markers/reserved fields are invalid')
        if command[2] == 3 or command[3]:
            raise RuntimeError(f'Source-CBV snapshot command {ordinal} failed: {command[3]}')
        copied_rows = [row for row in source_rows if row[8] == 1]
        deferred_rows = [row for row in source_rows if row[8] == 2]
        expected = [len(copied_rows), len(deferred_rows), len(source_rows), len(copied_rows),
                    sum(row[20] for row in copied_rows), sum(row[21] for row in copied_rows),
                    copied_rows[-1][17] if copied_rows else 0]
        if list(command[4:11]) != expected:
            raise ValueError('Source-CBV snapshot command aggregates differ from its exact rows')
        if copied_rows:
            if command[2] != 1 or command[15:20] != (1, 1, 1, 1, 1):
                raise RuntimeError('Snapshot command did not close prepare/record/finalize/submission/scope')
            if len({(row[4], row[5]) for row in copied_rows}) != len(copied_rows):
                raise ValueError('Snapshot command duplicated a private CBV slot')
        elif command[2] != 2 or any(command[15:20]):
            raise ValueError('Empty snapshot command claimed recorded copies')
        submitted += command[18]
        prepared += command[19]
    expected_stats = [3, source_stats[1], len(sources), len(rows), *totals,
                      len(arenas), submitted, 0, prepared, 0, 0, 0]
    if stats != expected_stats:
        raise ValueError('Source-CBV snapshot aggregates differ from its exact receipts')
    return dict(stats=stats, rows=[list(row) for row in rows],
        commands=[list(command) for command in commands], cbvs=entries, verified=True,
        sourceCommands=len(sources), cbvCount=len(rows), copiedCount=totals[0],
        copiedDefaultCount=copied_default, copiedUploadCount=copied_upload,
        deferredUploadCount=totals[1], failedCount=totals[2], copiedBytes=totals[3], paddedBytes=totals[4],
        finalizedArenas=len(arenas), sourceSubmissionsValidated=submitted, preparedCommands=prepared,
        blobBytes=blob_cursor, metadataOnly=blob is None,
        gpuPayloadCaptured=blob is not None and bool(totals[0]),
        defaultCopyReceiptsVerified=True, uploadContentsFrozen=False,
        sourceSnapshotInputsComplete=False, privateCbvsConsumed=False,
        gpuInputEquivalenceProved=False, **_no_permission())
