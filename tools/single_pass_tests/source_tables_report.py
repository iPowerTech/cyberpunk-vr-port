"""Validate owned finite-source-table receipts, never whole-input admission.

Stats are 16Q; source-command receipts are 24Q; owned-table receipts are 16Q.
The ABI contains totals, not reflection bytecodes or a per-slot use bitmap.
"""

import struct

from source_cbv_report import (
    MAX_SOURCES, UINT32_MAX, UINT64_MAX, _no_permission, _source_rows, _words,
    analyze as analyze_cbvs,
)

COMMAND_WORDS = 24
TABLE_WORDS = 16
COMMAND_BYTES = COMMAND_WORDS * 8
TABLE_BYTES = TABLE_WORDS * 8
MAX_TABLES = MAX_SOURCES * 64
RESOURCE_LIMIT = 1_000_000
SAMPLER_LIMIT = 2048
CBV_EXTENTS = {0: 4, 1: 1, 2: 2, 3: 1, 4: 6,
               18: 4, 19: 1, 20: 2, 21: 1, 22: 6}


def _raw_rows(raw, words, count, limit, name):
    if not isinstance(raw, (bytes, bytearray, memoryview)) or (
            isinstance(raw, memoryview) and (
                not raw.c_contiguous or raw.itemsize != 1 or raw.ndim != 1)):
        raise ValueError(f'{name} is not a contiguous binary ABI buffer')
    size = words * 8
    if count > limit or len(raw) % size or len(raw) // size != count:
        raise ValueError(f'Invalid {name} extent')
    try:
        return list(struct.iter_unpack(f'<{words}Q', raw))
    except (TypeError, BufferError, struct.error) as error:
        raise ValueError(f'Invalid {name} binary ABI') from error


def _cbv_rows(report, source_report):
    if not isinstance(report, dict) or report.get('verified') is not True:
        raise ValueError('Missing verified source-CBV report')
    stats = _words(report.get('stats'), 16, 'source-CBV stats')
    rows = report.get('rows')
    if not isinstance(rows, (list, tuple)) or len(rows) != stats[3] or len(rows) > MAX_SOURCES * 28:
        raise ValueError('Invalid source-CBV report rows')
    rows = [_words(row, 24, 'source-CBV row') for row in rows]
    raw = b''.join(struct.pack('<24Q', *row) for row in rows)
    checked = analyze_cbvs(stats, raw, source_report)
    for name in ('sourceCommands', 'cbvCount', 'ownedCount'):
        if type(report.get(name)) is not int or report[name] != checked[name]:
            raise RuntimeError(f'Source-CBV {name} differs from its independently decoded rows')
    return rows


def _cbv_slot(stage, register):
    if stage not in (1, 5) or not 0 <= register <= 13:
        raise ValueError('Invalid native CBV stage/register')
    if register < 4:
        parameter, offset = 0, register
    elif register == 4:
        parameter, offset = 1, 0
    elif register < 7:
        parameter, offset = 2, register - 5
    elif register == 7:
        parameter, offset = 3, 0
    else:
        parameter, offset = 4, register - 8
    return parameter + (18 if stage == 5 else 0), offset


def analyze(stats, raw_command_rows, raw_table_rows, source_report, cbv_report):
    """Raise on incomplete/torn ownership; retain raw diagnostics before calling."""
    stats = _words(stats, 16, 'source-table stats')
    if stats[0] not in (3, 4) or not stats[1] or any(stats[13:]):
        raise ValueError('Invalid source-table completion/frame/reserved header')
    source_stats, sources = _source_rows(source_report)
    count = len(sources)
    if stats[1] != source_stats[1] or stats[2] != count or stats[3] != count:
        raise RuntimeError('Source tables did not cover every original source occurrence/frame')
    commands = _raw_rows(raw_command_rows, COMMAND_WORDS, stats[3], MAX_SOURCES, 'source-table commands')
    tables = _raw_rows(raw_table_rows, TABLE_WORDS, stats[6], MAX_TABLES, 'source-table rows')
    cbvs = _cbv_rows(cbv_report, source_report)
    per_source_cbvs = [[] for _ in sources]
    for row in cbvs:
        expected = _cbv_slot(row[2], row[3])
        if tuple(row[4:6]) != expected:
            raise ValueError('Native CBV register-to-original-root-slot mapping changed')
        per_source_cbvs[row[0]].append(row)

    eye_recordings = {}
    for source in sources:
        recording = tuple(source[6:10])
        if source[1] in eye_recordings and eye_recordings[source[1]] != recording:
            raise ValueError('One eye contains multiple source recording identities')
        eye_recordings[source[1]] = recording

    by_source = [dict() for _ in sources]
    identities, heaps, owners = {}, {}, {}
    unique = {}
    prior = (-1, -1)
    type_totals = [0] * 4
    for row in tables:
        ordinal, side, parameter, extent = row[:4]
        if ordinal >= count or side != sources[ordinal][1] or parameter >= 64 or (ordinal, parameter) <= prior:
            raise ValueError('Source-table ordinal/order/eye/duplicate/root parameter changed')
        prior = ordinal, parameter
        if row[15] or not all(row[index] for index in range(3, 11)) or extent > RESOURCE_LIMIT:
            raise ValueError('Source-table heap/owner/identity/version/source range or extent is incomplete')
        if not sources[ordinal][13] & (1 << parameter):
            raise ValueError('Owned table was not initialized on its exact source occurrence')
        subtotals = tuple(row[11:15])
        if sum(subtotals) != row[10] or row[10] > extent or (row[14] and any(row[11:14])):
            raise ValueError('Source-table used-slot/type subtotals or heap kind are inconsistent')
        if row[14] and extent > SAMPLER_LIMIT:
            raise ValueError('Source-table sampler extent exceeds its heap limit')
        if row[11] and (parameter not in CBV_EXTENTS or extent != CBV_EXTENTS[parameter]):
            raise ValueError('Native CBV table did not retain its full original declared extent')
        key = row[6], row[7]
        coherent = (row[7], row[4], row[5], extent, row[10], subtotals)
        if row[6] in identities and identities[row[6]] != coherent:
            raise ValueError('Immutable source-table identity/version/heap/owner/count changed')
        identities[row[6]] = coherent
        for mapping, pointer, name in ((heaps, row[4], 'CPU heap'), (owners, row[5], 'owner')):
            if pointer in mapping and mapping[pointer] != key:
                raise ValueError(f'One retained source-table {name} represents conflicting identities')
            mapping[pointer] = key
        unique[key] = row
        by_source[ordinal][parameter] = row
        for index, subtotal in enumerate(subtotals):
            type_totals[index] += subtotal

    used, deferred, held_cbvs, stride = 0, 0, 0, None
    cbv_signatures = {}
    for ordinal, command in enumerate(commands):
        source, owned, cbv_rows = sources[ordinal], by_source[ordinal], per_source_cbvs[ordinal]
        if command[0] != ordinal or command[1] != source[1]:
            raise ValueError('Source-table command order/eye differs from the original occurrence')
        if command[2] != 1 or command[3] or tuple(command[4:6]) != (UINT32_MAX, UINT32_MAX):
            raise RuntimeError(f'Source-table command {ordinal} failed: reason={command[3]} slot={command[4:6]}')
        if any(command[16:]) or tuple(command[11:15]) != (source[11], source[6], source[7], source[8]):
            raise ValueError('Source-table command root/list/generation/context/reserved identity changed')
        if not 0 < command[6] <= 64 or command[6] != command[7] or command[7] != len(owned):
            raise RuntimeError('Source-table command is missing requested finite tables')
        if command[8] != sum(row[3] for row in owned.values()) or command[9] != sum(row[10] for row in owned.values()):
            raise RuntimeError('Source-table command declared/used extents differ from owned rows')
        if not 0 < command[10] <= 1024:
            raise RuntimeError('Original-family dynamic uses were not explicitly deferred')
        if command[15] != len(cbv_rows) or sum(row[11] for row in owned.values()) != len(cbv_rows):
            raise RuntimeError('Finite-table CBV slot totals differ from held source-CBV receipts')
        camera = next(row for row in cbv_rows if (row[2], row[3]) == (1, 1))
        camera_table = owned.get(0)
        if not camera_table:
            raise RuntimeError('Mandatory camera CBV table0 is missing')
        delta = camera[8] - camera_table[8]
        if not 0 < delta <= UINT32_MAX or (stride is not None and stride != delta):
            raise ValueError('Mandatory camera table-relative descriptor stride is inconsistent')
        stride = delta
        cbvs_by_table = {}
        slots = set()
        for cbv in cbv_rows:
            parameter, offset = cbv[4:6]
            table = owned.get(parameter)
            if not table or not table[11] or offset >= table[3] or (parameter, offset) in slots:
                raise RuntimeError('Required source-CBV slot is missing/duplicated/outside its owned table')
            slots.add((parameter, offset))
            if table[9] != cbv[9] or table[8] > UINT64_MAX - offset * stride or table[8] + offset * stride != cbv[8]:
                raise ValueError('Source-CBV GPU slot/heap cookie differs from its exact table source range')
            cbvs_by_table.setdefault(parameter, []).append((offset, cbv[10], cbv[11], cbv[17], cbv[18]))
        for parameter, table in owned.items():
            signature = tuple(sorted(cbvs_by_table.get(parameter, [])))
            if len(signature) != table[11]:
                raise RuntimeError('Owned table CBV subtotal differs from its independent slot receipts')
            key = table[6], table[7]
            if key in cbv_signatures and cbv_signatures[key] != signature:
                raise ValueError('One immutable CPU table identity contains conflicting CBV descriptions')
            cbv_signatures[key] = signature
        used += command[9]
        deferred += command[10]
        held_cbvs += command[15]

    unique_resources = sum(row[3] for row in unique.values() if not row[14])
    unique_samplers = sum(row[3] for row in unique.values() if row[14])
    expected = [count, 0, len(tables), sum(row[3] for row in tables),
                len(unique), unique_resources + unique_samplers, used, deferred, 0]
    if stats[4:13] != expected or used != sum(type_totals) or held_cbvs != len(cbvs):
        raise RuntimeError('Source-table aggregate/cache/type counters differ from exact occurrences')
    if unique_resources > RESOURCE_LIMIT or unique_samplers > SAMPLER_LIMIT:
        raise ValueError('Source-table cache exceeds its resource/sampler budget')
    if stats[0] != 3:
        raise RuntimeError('Source-table capture reported failure')
    return dict(
        stats=stats, commands=[list(row) for row in commands], rows=[list(row) for row in tables],
        verified=True, sourceCommands=count, ownedCommands=count, finiteTableCount=len(tables),
        declaredDescriptorCount=stats[7], usedSlotCount=used, cbvSlotCount=type_totals[0],
        srvSlotCount=type_totals[1], uavSlotCount=type_totals[2], samplerSlotCount=type_totals[3],
        uniqueTableCount=len(unique), uniqueDescriptorCount=stats[9], deferredUseCount=deferred,
        resourceDescriptorStrideFromCamera=stride, originalParameters=sorted({row[2] for row in tables}),
        allSourceOccurrencesCovered=True, reportedFiniteTableOwnershipComplete=True,
        sourceCbvSlotMappingsCrossChecked=True, completeAllShaderTables=False,
        dynamicUsesDeferred=True, dynamicSlotsOwned=False, dynamicUseParametersIndependentlyEnumerated=False,
        allReflectedFiniteUsesIndependentlyEnumerated=False, finiteDeclaredLayoutsIndependentlyCompared=False,
        currentBackingOwnershipOnly=True, **_no_permission(),
    )
