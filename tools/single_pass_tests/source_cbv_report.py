"""Validate current native source-CBV ownership diagnostics, never byte freezing.

The 24Q CBV ABI has no root/pipeline pointer. Those identities are source-stream
context only, not an independent equality check or additional execution proof.
"""

import struct

ROW_WORDS = 24
ROW_BYTES = ROW_WORDS * 8
MAX_SOURCES = 2048
MAX_ROWS = MAX_SOURCES * 28
UINT64_MAX = 2**64 - 1
UINT32_MAX = 2**32 - 1
CREATORS = {0x1F01A6, 0x1F0BF7, 0x1F2960}
ACCEPTED = {1, 2, 3}
STATUS_NAMES = (
    'Ignored', 'Registered', 'Transient', 'Created', 'UnsupportedExecutable',
    'MissingContext', 'InvalidBinding', 'MissingObservation', 'UnknownCreator',
    'UnknownOrigin', 'Changed', 'DescriptorMismatch', 'UnsupportedResource',
    'Capacity', 'Failure',
)
REQUIRED_CAMERA_USES = {(1, 1), (1, 5)}


def _words(values, count, name):
    if not isinstance(values, (list, tuple)) or len(values) != count or any(
            type(value) is not int or not 0 <= value <= UINT64_MAX for value in values):
        raise ValueError(f'Invalid {name} ABI')
    return list(values)


def _source_rows(report):
    if not isinstance(report, dict):
        raise ValueError('Missing source-stream report')
    stats = _words(report.get('stats'), 16, 'source-stream stats')
    values = report.get('rows')
    if not isinstance(values, (list, tuple)) or not 0 < len(values) <= MAX_SOURCES or len(values) != stats[2]:
        raise ValueError('Invalid source-stream occurrence extent')
    if stats[0] != 3 or not stats[1] or any(stats[12:]):
        raise RuntimeError('Source stream did not close exact source ownership')
    rows = [_words(value, 48, 'source-stream row') for value in values]
    counts = [0] * 8
    for ordinal, row in enumerate(rows):
        if row[0] != ordinal or row[1] not in (0, 1) or row[2] not in (0, 1, 2) or row[3] != stats[1]:
            raise ValueError('Source-stream ordinal, eye, command kind or frame changed')
        if row[43] != 31 or row[44] or row[47] or not all(row[field] for field in (6, 7, 8, 9, 10, 11, 12)):
            raise RuntimeError('Source-stream recording, shader, root or pipeline ownership is incomplete')
        counts[int(bool(row[2]))] += 1
        counts[2 + row[1]] += 1
        for bit in range(4):
            counts[4 + bit] += bool(row[43] & (1 << bit))
    if stats[3:11] != counts or stats[11] != len(rows):
        raise RuntimeError('Source-stream aggregate counters differ from occurrences')
    for key, expected in (('commands', len(rows)), ('direct', counts[0]), ('indirect', counts[1])):
        if type(report.get(key)) is not int or report[key] != expected:
            raise RuntimeError(f'Source-stream {key} count differs from occurrences')
    return stats, rows


def _no_permission():
    return dict(inputContentsFrozen=False, nativeExecutionPermission=False,
                nativeExecutionPermitted=False, nativeSuppression=0,
                nativeCommandsSuppressed=0, suppressed=0, performanceGain=False,
                performanceGainProved=False, copyPermission=False)


def _owned(row, source):
    status = row[7]
    if row[23] != 1 or status not in ACCEPTED:
        raise RuntimeError(f'Source CBV {row[0]} stage={row[2]} b{row[3]} did not close ownership: {STATUS_NAMES[status]}')
    if (row[15], row[16]) != (source[8], source[7]):
        raise ValueError('Source-CBV context/generation differs from its exact source occurrence')
    if not source[13] & (1 << row[4]):
        raise ValueError('Source-CBV root parameter was not initialized on its source occurrence')
    if not all(row[field] for field in (8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20)):
        raise ValueError('Source-CBV descriptor, heap, backing, cookie or current CPU identity is incomplete')
    reflected, address, descriptor_bytes, base, width = row[6], row[10], row[11], row[13], row[14]
    if not 0 < reflected <= 65536 or not 0 < descriptor_bytes <= 65536 or address % 256 or descriptor_bytes % 256:
        raise ValueError('Source-CBV logical extent or 256-byte descriptor alignment is invalid')
    if descriptor_bytes < reflected:
        raise ValueError('Source-CBV padded descriptor does not cover its reflected logical bytes')
    if base > UINT64_MAX - width or address > UINT64_MAX - descriptor_bytes or address < base or (
            address - base > width or descriptor_bytes > width - (address - base)):
        raise ValueError('Source-CBV descriptor is outside its current physical COM buffer')
    if row[19] and row[19] not in CREATORS:
        raise ValueError('Source-CBV creator return RVA is unsupported')
    # Registered setters can attach a held origin without a CreateCBV return.
    # A creator still requires an origin; created/transient paths require both.
    if row[19] and not row[18]:
        raise ValueError('Source-CBV held origin and creator metadata are torn')
    if row[21] > 0x100000 or row[22] > UINT32_MAX:
        raise ValueError('Source-CBV native handle/logical-byte ABI is invalid')
    if status in (1, 2) and (not row[21] or not row[22]):
        raise ValueError('Registered/transient source-CBV lacks its native handle/logical extent')
    if status in (2, 3) and (not row[18] or not row[19]):
        raise ValueError('Transient/created source-CBV lacks its held creation-origin identity')
    if status == 3:
        if row[21] and not row[22]:
            raise ValueError('Created source-CBV native handle/logical metadata are torn')
        if not row[21] and (row[2], row[3], row[19]) != (1, 5, 0x1F2960):
            raise ValueError('Unobserved created source-CBV is not the audited fresh VS b5 path')
    # Native B0 logical bytes may describe a larger allocation than this CBV
    # view. Do not invent logicalBytes <= descriptorBytes or a start address.
    return dict(
        sourceOrdinal=row[0], side=row[1], visibility=row[2], shaderRegister=row[3],
        parameter=row[4], tableOffset=row[5], reflectedLogicalBytes=reflected,
        status=status, statusName=STATUS_NAMES[status], owned=True,
        originalGpuDescriptor=row[8], sourceHeapCookie=row[9], cbvGpuAddress=address,
        descriptorBytes=descriptor_bytes, resource=row[12], resourceGpuBase=base,
        resourceWidth=width, physicalOffset=address - base,
        recordingContext=row[15], recordingGeneration=row[16], resourceCookie=row[17],
        originId=row[18], creatorRva=row[19], currentCpuDescriptor=row[20],
        nativeHandle=row[21], logicalNativeBytes=row[22],
        nativeLogicalExtentInformationOnly=True,
        nativeLogicalBytesWithinBackingWidth=row[22] <= width if row[22] else None,
        sourceList=source[6], rootFromSource=source[11], pipelineFromSource=source[10],
        shaderSnapshotFromSource=source[12], sourceHeldFlags=source[43],
        rootPipelineIdentityIndependentlyCompared=False,
        **_no_permission(),
    )


def analyze(stats, raw, source_report):
    """Raise on any incomplete/torn receipt; callers retain raw diagnostics first."""
    stats = _words(stats, 16, 'source-CBV stats')
    if stats[0] not in (3, 4) or not stats[1] or any(stats[12:]) or stats[11] >= len(STATUS_NAMES):
        raise ValueError('Invalid source-CBV state/frame/first-failure/reserved header')
    if not isinstance(raw, (bytes, bytearray, memoryview)) or (
            isinstance(raw, memoryview) and (not raw.c_contiguous or raw.itemsize != 1)):
        raise ValueError('Source-CBV rows are not a contiguous binary ABI buffer')
    if len(raw) % ROW_BYTES or len(raw) // ROW_BYTES != stats[3] or stats[3] > MAX_ROWS:
        raise ValueError('Invalid source-CBV row extent')
    source_stats, sources = _source_rows(source_report)
    if stats[1] != source_stats[1] or stats[2] != len(sources):
        raise RuntimeError('Source-CBV frame/source-command count differs from source stream')
    if stats[3] > len(sources) * 28:
        raise ValueError('Source-CBV rows exceed the native 14-register/two-stage domain')
    try:
        rows = list(struct.iter_unpack('<24Q', raw))
    except (TypeError, BufferError, struct.error) as error:
        raise ValueError('Source-CBV rows are not a contiguous 24Q ABI buffer') from error
    uses = [set() for _ in sources]
    counts = [0] * 7
    first_failure = 0
    prior_ordinal = 0
    for row in rows:
        ordinal, side, stage, register, parameter, offset = row[:6]
        if ordinal >= len(sources) or ordinal < prior_ordinal or side != sources[ordinal][1]:
            raise ValueError('Source-CBV ordinal/order/eye differs from the exact source occurrence')
        prior_ordinal = ordinal
        if stage not in (1, 5) or register > 13 or parameter >= 64 or offset > UINT32_MAX:
            raise ValueError('Source-CBV visibility/register/root-slot ABI is unsupported')
        if row[7] >= len(STATUS_NAMES) or row[23] not in (0, 1):
            raise ValueError('Source-CBV status or owned boolean is invalid')
        if (stage, register) in uses[ordinal]:
            raise ValueError('Duplicate source-CBV (stage,register) in one source occurrence')
        uses[ordinal].add((stage, register))
        counts[0 if row[23] else 1] += 1
        counts[2 if stage == 1 else 3] += 1
        if row[23] and row[7] in ACCEPTED:
            counts[3 + row[7]] += 1
        elif not first_failure:
            # Matches the runtime publisher, including Ignored(0) placeholders.
            first_failure = row[7]
    if stats[4:11] != counts or stats[11] != first_failure:
        raise RuntimeError('Source-CBV aggregate/status counters differ from exact rows')
    for ordinal, actual in enumerate(uses):
        missing = REQUIRED_CAMERA_USES - actual
        if missing:
            raise RuntimeError(f'Source occurrence {ordinal} lacks mandatory VS camera CBVs: {sorted(missing)}')
    if stats[0] != 3:
        raise RuntimeError(f'Source-CBV capture reported failure: {STATUS_NAMES[stats[11]]}')
    entries = [_owned(row, sources[row[0]]) for row in rows]
    if stats[4] != stats[3] or stats[5] or sum(stats[8:11]) != stats[3] or stats[11]:
        raise RuntimeError('Source-CBV capture did not close every accepted owned row')
    return dict(
        stats=stats, rows=[list(row) for row in rows], cbvs=entries,
        verified=True, sourceCommands=len(sources), cbvCount=len(rows),
        ownedCount=stats[4], registeredCount=stats[8], transientCount=stats[9], createdCount=stats[10],
        vertexCount=stats[6], pixelCount=stats[7], requiredVsB1B5Covered=True,
        reflectedBytes=sum(row[6] for row in rows), descriptorBytes=sum(row[11] for row in rows),
        currentBackingOwnershipOnly=True, allReflectedCbvUsesIndependentlyEnumerated=False,
        rootPipelineIdentityIndependentlyCompared=False,
        creators=sorted({row[19] for row in rows if row[19]}),
        **_no_permission(),
    )
