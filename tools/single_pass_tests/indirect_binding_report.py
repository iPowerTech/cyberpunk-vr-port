"""Verify exact native indirect-binding observations, never input-copy permission."""
import struct

ROW_WORDS = 56
ROW_BYTES = ROW_WORDS * 8
MAX_SOURCES = 2048
UINT64_MAX = 2**64 - 1
UINT32_MAX = 2**32 - 1
RECORD_BYTES = 0xB0
RECORD_BASE = 0x5C0AF8
SITES = {0x623CC1: (1, 20), 0x624830: (1, 20), 0x62462F: (2, 16)}
ACCEPTED_STATES = {1, 2, 5, 8, 9}
GENERIC_READ = 0xAC3
INDIRECT_ARGUMENT = 0x200


def _words(values, count, name):
    if not isinstance(values, (list, tuple)) or len(values) != count or any(
            type(value) is not int or not 0 <= value <= UINT64_MAX for value in values):
        raise ValueError(f'Invalid {name} ABI')
    return list(values)


def _source_rows(source):
    if not isinstance(source, dict):
        raise ValueError('Missing source-stream report')
    stats = _words(source.get('stats'), 16, 'source-stream stats')
    raw_rows = source.get('rows')
    if not isinstance(raw_rows, (list, tuple)) or not 0 < len(raw_rows) <= MAX_SOURCES or len(raw_rows) != stats[2]:
        raise ValueError('Invalid source-stream row extent')
    if stats[0] != 3 or not stats[1] or stats[12] or any(stats[13:]):
        raise RuntimeError('Source-stream report has not closed verified source ownership')
    rows = [_words(row, 48, 'source-stream row') for row in raw_rows]
    indirect = []
    for index, row in enumerate(rows):
        if row[0] != index or row[1] not in (0, 1) or row[2] not in (0, 1, 2) or row[3] != stats[1]:
            raise ValueError('Source-stream order, eye, kind or frame changed')
        if row[43] != 31 or row[44] or row[47]:
            raise RuntimeError('Source-stream occurrence is not fully owned')
        if row[2]:
            if not all(row[field] for field in (6, 7, 8, 9, 29, 30, 32, 45)):
                raise ValueError('Indirect source identity is incomplete')
            indirect.append(row)
    reported = source.get('indirect')
    if type(reported) is not int or reported != len(indirect) or stats[4] != len(indirect) or stats[3] != len(rows) - len(indirect):
        raise RuntimeError('Source-stream indirect count differs from occurrences')
    if stats[11] != len(rows):
        raise RuntimeError('Source-stream owned command count changed')
    if stats[5:7] != [sum(row[1] == side for row in rows) for side in (0, 1)]:
        raise RuntimeError('Source-stream eye counts differ from occurrences')
    return stats, indirect


def _record(row):
    raw = struct.pack('<22Q', *row[32:54])
    length, = struct.unpack_from('<I', raw)
    gpu, resource = struct.unpack_from('<QQ', raw, 8)
    low, high = raw[6] & 15, raw[6] >> 4
    if low != 0xB or high != row[6] or length != row[11] or gpu != row[10] or resource != row[25]:
        raise ValueError('Native B0 record length, type, kind, GPU or resource differs from binding')
    return dict(address=row[4], length=length, typeByte=raw[6], lowType=low,
                highKind=high, logicalGPU=gpu, resource=resource,
                rawHex=raw.hex(), rawWords=list(row[32:54]),
                metadata={f'qword{offset:02x}': struct.unpack_from('<Q', raw, offset)[0]
                          for offset in (0x18, 0x20, 0x28, 0x40, 0x58)},
                dword68=struct.unpack_from('<I', raw, 0x68)[0])


def _range(row):
    logical, length, backing, width, offset, argument_bytes = row[10:16]
    if not all((logical, length, backing, width)) or length > UINT32_MAX or (
            logical > UINT64_MAX - length or backing > UINT64_MAX - width):
        raise ValueError('Invalid indirect logical/backing GPU extent')
    if logical < backing or logical - backing > width or length > width - (logical - backing):
        raise ValueError('Native logical allocation is outside the COM backing range')
    if offset % 4 or offset > width or argument_bytes > width - offset:
        raise ValueError('Indirect API argument extent is outside the COM buffer')
    address = backing + offset
    if address < logical or address - logical > length or argument_bytes > length - (address - logical):
        raise ValueError('Indirect API argument extent is outside the native logical allocation')
    if row[26] != int(logical == backing and length == width):
        raise ValueError('Full-backing observation differs from actual logical/backing extents')
    return address


def _state(row):
    value, status, kind = row[7:10]
    if value > UINT32_MAX or status not in ACCEPTED_STATES or kind != row[6]:
        raise ValueError('Accepted indirect state ABI or native kind changed')
    if row[16] != (2 if status == 1 else 1):
        raise ValueError('Indirect state observation disagrees with native heap type')
    if status == 5:
        if value:
            raise ValueError('Unknown native state carries an invented state value')
    elif not value & INDIRECT_ARGUMENT or value & ~GENERIC_READ:
        raise ValueError('Accepted native indirect read-state value is unsupported')
    if status in (1, 8, 9) and value != GENERIC_READ:
        raise ValueError('Fixed-read native state differs from GENERIC_READ')
    if status in (8, 9) and (kind != 1 or row[17] & 4):
        raise ValueError('Fixed-read class evidence does not match native kind/flags')
    return dict(value=value, status=status, kind=kind, known=status != 5,
                informationOnly=True, copyPermission=False)


def analyze(stats, raw, source_report):
    stats = _words(stats, 16, 'indirect-binding stats')
    if not isinstance(raw, (bytes, bytearray, memoryview)):
        raise ValueError('Indirect-binding rows are not a binary buffer')
    if isinstance(raw, memoryview) and (not raw.c_contiguous or raw.itemsize != 1):
        raise ValueError('Indirect-binding rows are not a contiguous byte ABI buffer')
    if len(raw) % ROW_BYTES or len(raw) // ROW_BYTES != stats[2] or not 0 < stats[2] <= MAX_SOURCES:
        raise ValueError('Invalid indirect-binding occurrence extent')
    if stats[0] not in (1, 2, 3, 4) or not stats[1] or any(stats[8:]):
        raise ValueError('Invalid indirect-binding state/frame/reserved header')
    source_stats, sources = _source_rows(source_report)
    if stats[1] != source_stats[1] or stats[2] != len(sources):
        raise RuntimeError('Indirect bindings do not cover the exact source frame/occurrences')
    try:
        rows = list(struct.iter_unpack('<56Q', raw))
    except (TypeError, BufferError, struct.error) as error:
        raise ValueError('Indirect-binding rows are not a contiguous ABI buffer') from error
    counts = [0] * 5
    bindings = []
    for index, (row, source) in enumerate(zip(rows, sources)):
        if row[3] not in (0, 1) or row[26] not in (0, 1) or row[27] not in (0, 1) or any(row[30:32]) or any(row[54:]) or row[28]>12 or row[29]>12:
            raise ValueError('Indirect-binding booleans or reserved row fields changed')
        if (row[3] and row[27] and (row[28] or row[29])) or (row[29] and row[28]!=6):
            raise ValueError('Indirect-binding rejection diagnostics disagree with the accepted receipt')
        counts[0 if row[3] else 1] += 1
        counts[2] += row[26]
        counts[3] += row[15] == 20
        counts[4] += row[8] == 2
        if (row[0] != source[0] or row[1] != source[1] or row[2] != source[45] or
                row[14] != source[33] or row[20] != source[31] or row[21] != source[7] or
                row[22] != source[8] or row[23] != source[6] or row[25] != source[32]):
            raise ValueError(f'Indirect binding {index} differs from its ordered source occurrence')
        if not row[3] or not row[27]:
            raise RuntimeError(f'Indirect binding {index} was not accepted and revalidated: stage={row[28]}, recording={row[29]}')
        if row[24] != source[9] or not all(row[field] for field in (4, 5, 10, 11, 12, 13, 21, 22, 23, 24, 25)):
            raise ValueError('Indirect native registry or recording identity is incomplete')
        if row[2] not in SITES or (row[18], row[19]) != SITES[row[2]] or row[18] != source[2] or row[19] != source[30]:
            raise ValueError('Indirect native site/signature kind or exact stride is unsupported')
        if row[20] != 1 or source[34] or source[35] or row[15] != row[20] * row[19]:
            raise ValueError('Indirect native argument/count operands are unsupported')
        if row[5] > 0x100000 or row[6] > 15 or row[17] > UINT32_MAX:
            raise ValueError('Indirect native handle/kind/resource flags are invalid')
        address = row[24] + RECORD_BASE + (row[5] - 1) * RECORD_BYTES
        if address > UINT64_MAX - RECORD_BYTES or row[4] != address:
            raise ValueError('Indirect B0 record address is not the exact registry/handle entry')
        record = _record(row)
        argument_gpu = _range(row)
        state = _state(row)
        bindings.append(dict(sourceOrdinal=row[0], side=row[1], actualCallerRva=row[2],
                             boundSucceeded=True, revalidated=True, handle=row[5],
                             logicalGPU=row[10], logicalBytes=row[11], backingGPU=row[12], backingWidth=row[13],
                             apiOffset=row[14], argumentBytes=row[15], argumentGPU=argument_gpu,
                             heapType=row[16], resourceFlags=row[17], signatureKind=row[18], stride=row[19], maxCount=row[20],
                             recordingGeneration=row[21], context=row[22], list=row[23], registry=row[24], argumentCOM=row[25],
                             fullBacking=bool(row[26]), state=state, rawRecord=record,
                             copyPermission=False, inputContentsFrozen=False, nativeSuppression=0))
    if list(stats[3:8]) != counts:
        raise RuntimeError('Indirect-binding aggregate counters differ from native occurrences')
    if stats[0] != 3 or stats[3] != stats[2] or stats[4]:
        raise RuntimeError('Indirect-binding capture did not close all accepted source occurrences')
    return dict(stats=stats, rows=[list(row) for row in rows], bindings=bindings,
                verified=True, sourceIndirectCount=len(sources), fullBackingCount=stats[5],
                twentyByteArgumentCount=stats[6], trackedStateCount=stats[7],
                unknownStateCount=sum(row[8] == 5 for row in rows),
                actualCallers=sorted({row[2] for row in rows}),
                stateInformationOnly=True, copyPermission=False, nativeSuppression=0,
                nativeCommandsSuppressed=0, inputContentsFrozen=False,
                nativeExecutionPermitted=False, performanceGainProved=False)
