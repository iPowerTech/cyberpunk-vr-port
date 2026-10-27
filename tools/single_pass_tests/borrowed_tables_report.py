"""Descriptor no-overwrite observation, never material-read or GPU permission."""
import struct

PREFIX = 'CyberpunkVR_Retained'
MAX_U64 = 2**64-1
MAX_SLOTS = 4*1024*1024
STATS = ('schema', 'requested', 'frame', 'sources', 'captured', 'ranges', 'descriptors',
         'beforePrefixValidated', 'afterPrefixValidated', 'captureRejected', 'beforeRejected',
         'afterRejected', 'firstFailureOrdinalPlus1', 'failureCode', 'binds', 'nativeSuppressed')
PAGEABLE_STATS = ('schema', 'requested', 'frame', 'entries', 'bound', 'afterValid', 'bindRejected',
                  'afterRejected', 'resourcePageables', 'heapPageables', 'capacity', 'firstStage',
                  'firstDetail', 'firstOrdinalPlus1', 'permission', 'nativeSuppressed')
ARRAYS = (('rows', 'BorrowedTables', 16, 512), ('ranges', 'BorrowedRange', 12, 16384),
          ('inventories', 'BorrowedInventory', 24, 256))


def _words(value, count):
    if not isinstance(value, (list, tuple)) or len(value) != count or any(
            type(v) is not int or not 0 <= v <= MAX_U64 for v in value):
        raise ValueError('Borrowed-table ABI requires bounded uint64 fields')
    return list(value)


def _rows(value, words, limit):
    if not isinstance(value, (list, tuple)) or len(value) > limit:
        raise ValueError('Borrowed-table row extent exceeds bounds')
    return [_words(row, words) for row in value]


def decode(stats, rows, ranges, inventories, failure, pageable=None):
    stats = _words(stats, 16)
    if stats[0] != 1:raise ValueError('Borrowed-table schema differs')
    result = dict(stats=stats, summary=dict(zip(STATS, stats)), rows=_rows(rows, 16, 512),
                  ranges=_rows(ranges, 12, 16384), inventories=_rows(inventories, 24, 256),
                  failure=_words(failure, 8), verified=False, classification='OBSERVATION',
                  executionPermission=False, resourceReadPermission=False, nativeSuppressionPermission=False,
                  fullDynamicBankValid=False, inventoryResourceOwnershipProved=False)
    if pageable is not None:
        values = _words(pageable.get('stats'), 16)
        if values[0] != 1:raise ValueError('Pageable-use schema differs')
        result['pageableUse'] = dict(stats=values, summary=dict(zip(PAGEABLE_STATS, values)),
            rows=_rows(pageable.get('rows'), 16, 2048), failure=_words(pageable.get('failure'), 24),
            classification='OBSERVATION', allQualified=False, fullDynamicBankValid=False, resourceReadPermission=False)
        if 'search' in pageable:
            result['pageableUse']['search'] = _words(pageable['search'], 20)
    return result


def read_report(read_export, read_memory, *, pageable_available=False, search_available=False, attempts=4):
    def snapshot():
        result = {'stats': list(struct.unpack('<16Q', read_export(PREFIX+'BorrowedTablesStats', 128))),
                  'failure': list(struct.unpack('<8Q', read_export(PREFIX+'BorrowedFailure', 64)))}
        definitions = list(ARRAYS)
        if pageable_available:
            result['pageableStats'] = list(struct.unpack('<16Q', read_export(PREFIX+'PageableUseStats', 128)))
            result['pageableFailure'] = list(struct.unpack('<24Q', read_export(PREFIX+'PageableUseFailure', 192)))
            if search_available:result['pageableSearch'] = list(struct.unpack('<20Q', read_export(PREFIX+'PageableSearch', 160)))
            definitions.append(('pageableRows', 'PageableUse', 16, 2048))
        identities = []
        for name, suffix, words, limit in definitions:
            count, = struct.unpack('<I', read_export(PREFIX+suffix+'Count', 4))
            size, = struct.unpack('<I', read_export(PREFIX+suffix+'RowBytes', 4))
            pointer, = struct.unpack('<Q', read_export(PREFIX+suffix+'Rows', 8))
            if size != words*8 or count > limit or (count and (not pointer or pointer > MAX_U64-count*size)):
                raise ValueError('Borrowed-table payload pointer or ABI exceeds bounds')
            raw = read_memory(pointer, count*size) if count else b''
            if len(raw) != count*size:raise ValueError('Borrowed-table payload is truncated')
            result[name] = [list(row) for row in struct.iter_unpack(f'<{words}Q', raw)]
            identities.append((count, size, pointer))
        return result, identities
    for _ in range(attempts):
        first = snapshot()
        if first != snapshot():continue
        value = first[0]
        pageable = dict(stats=value['pageableStats'], rows=value['pageableRows'], failure=value['pageableFailure']) if pageable_available else None
        if pageable is not None and search_available:pageable['search'] = value['pageableSearch']
        return decode(value['stats'], value['rows'], value['ranges'], value['inventories'], value['failure'], pageable)
    raise ValueError('Borrowed-table capture changed during the bounded double read')


def _tail(source, tail):
    from source_cbv_report import _source_rows
    stats, rows = _source_rows(source)
    selected = []
    for side in (0, 1):
        eye = [row for row in rows if row[1] == side]
        first = len(eye)
        while first and eye[first-1][4] == 9:first -= 1
        if first == len(eye) or any(row[2] for row in eye[first:]):
            raise ValueError('Borrowed tables lack a complete direct final tail')
        selected.extend([row[0], side] for row in eye[first:])
    selected.sort()
    if (not isinstance(tail, dict) or tail.get('frame') != stats[1] or tail.get('nativeSources') != selected or
            tail.get('sources') != [[ordinal, 1-side] for ordinal, side in selected] or tail.get('sourceCount') != len(selected)):
        raise ValueError('Borrowed tables differ from the exact native tail')
    return stats[1], rows, selected


def _pageables(result, requested, frame, source, selected):
    if 'pageableUse' not in result:return
    report = result['pageableUse'];stats = report['stats'];rows = report['rows']
    search = report.get('search', [0]*20)
    if search[0] not in (0, 1) or search[1] not in (0, 1) or search[19] or (not search[0] and any(search)):
        raise ValueError('Pageable search schema or permission differs')
    if stats[1] != int(requested) or any(stats[14:]) or any(row[15] for row in rows):
        raise ValueError('Pageable observation request or permission differs')
    if not requested:
        if any(stats[1:]) or rows or any(report['failure']) or any(search):raise ValueError('Disabled pageable observation performed work')
        return
    if stats[2] != frame or stats[3] != len(rows) or stats[10] not in (0, 1):
        raise ValueError('Pageable observation frame or count differs')
    if search[0] and (stats[11:13] != [11, 409] or not stats[6] or not search[3] or
            not any(row[0]+1 == stats[13] and row[5] == search[4] and row[13:15] == [11, 409] for row in rows)):
        raise ValueError('Pageable search is not joined to the first missing-member refusal')
    seen = set();bound = after = bind_rejected = after_rejected = resources = heaps = 0
    for row in rows:
        ordinal, side = row[:2]
        if [ordinal, side] not in selected or row[2] != frame or row[5] != source[ordinal][6] or not row[4] or not 0 < row[3] <= 32768:
            raise ValueError('Pageable observation lost its source identity')
        key = (side, row[3], row[4])
        if key in seen:raise ValueError('Pageable observation duplicated a native member')
        seen.add(key)
        if row[11] not in (0, 1) or row[12] not in (0, 1) or row[12] > row[11]:
            raise ValueError('Pageable observation has invalid validation flags')
        bound += row[11];after += row[12];bind_rejected += not row[11];after_rejected += row[11] and not row[12]
        if row[11]:
            if not row[6] or not row[7] or not row[9] or row[10] not in (1, 2):
                raise ValueError('Bound pageable observation omitted its typed member')
            if row[10] == 1 and row[6] != row[4]:raise ValueError('Resource pageable differs from its source resource')
            resources += row[10] == 1;heaps += row[10] == 2
            if row[12] and (row[13] or row[14]):raise ValueError('Qualified pageable observation retained a refusal')
        if not row[12] and not row[13]:raise ValueError('Unqualified pageable observation omitted its refusal')
    if stats[4:10] != [bound, after, bind_rejected, after_rejected, resources, heaps]:
        raise ValueError('Pageable observation counters differ from rows')
    if bind_rejected or after_rejected:
        if not stats[11] or not stats[13] or stats[13]-1 not in [row[0] for row in rows if row[13]]:
            raise ValueError('Pageable observation lost its first refusal')
        # Several assets can belong to one draw. A lower-index asset may fail
        # only after the prefix, later than the recorded first bind refusal.
        if not any(row[0] == stats[13]-1 and row[13:15] == stats[11:13] for row in rows) or report['failure'][7:9] != stats[11:13]:
            raise ValueError('Pageable observation refusal diagnostics disagree')
    elif any(stats[11:14]) or any(report['failure']):raise ValueError('Pageable observation reports an unexplained refusal')
    report['allQualified'] = bool(rows) and not stats[10] and bound == after == len(rows)


def validate(report, *, requested, source=None, tail=None):
    if type(requested) is not bool:raise ValueError('Borrowed tables need an explicit boolean request')
    result = decode(report.get('stats'), report.get('rows'), report.get('ranges'), report.get('inventories'),
                    report.get('failure'), report.get('pageableUse'))
    stats, rows, ranges, inventories = (result[name] for name in ('stats', 'rows', 'ranges', 'inventories'))
    if stats[1] != int(requested):raise ValueError('Borrowed-table request differs')
    if not requested:
        if any(stats[1:]) or rows or ranges or inventories or any(result['failure']):
            raise ValueError('Disabled borrowed-table observer performed work')
        _pageables(result, False, 0, [], [])
        result['verified'] = True
        return result
    frame, originals, selected = _tail(source, tail)
    count = len(selected)
    if (not 0 < count <= 512 or [[row[0], row[1]] for row in rows] != selected or stats[2] != frame or
            stats[3:5] != [count, count] or stats[7:9] != [count, count] or any(stats[9:]) or any(result['failure'])):
        raise ValueError('Borrowed-table capture or before/after-prefix qualification failed')
    cursor = descriptors = 0;families = {};dynamic = {}
    for row in rows:
        original = originals[row[0]]
        if row[2:7] != [frame, original[6], original[7], original[11], original[10]] or not row[7]:
            raise ValueError('Borrowed-table root, PSO or recording join differs')
        if row[6] in families and families[row[6]] != row[7]:raise ValueError('Borrowed-table PSO changed shader-family identity')
        families[row[6]] = row[7]
        if row[8] != cursor or not 0 < row[9] <= 256 or row[9] > len(ranges)-cursor or row[11:] != [1, 1, 1, 0, 0]:
            raise ValueError('Borrowed-table range ownership or qualification differs')
        row_count = 0
        for index in range(cursor, cursor+row[9]):
            r = ranges[index]
            if (r[0] != row[0] or r[1] >= 64 or r[2] > 3 or r[3] >= 2**32 or not 0 < r[4] <= MAX_SLOTS or
                    not 0 < r[5] < 2**32 or not 0 < r[6] < 2**32 or not r[7] or not r[8] or not r[9] or not r[10] or
                    r[11] not in (0, 1) or r[7]+r[3]*r[5] != r[8] or r[8] > MAX_U64-r[4]*r[5]):
                raise ValueError('Borrowed descriptor range is malformed or belongs to another source')
            row_count += r[4]
            if r[11]:dynamic.setdefault((r[9], r[8], r[4], r[10]), index)
        if row[10] != row_count:raise ValueError('Borrowed descriptor accounting differs')
        cursor += row[9];descriptors += row_count
    if cursor != len(ranges) or stats[5:7] != [cursor, descriptors]:raise ValueError('Borrowed-table range totals differ')
    seen = set()
    for row in inventories:
        if row[3] >= len(ranges):raise ValueError('Borrowed inventory references a missing range')
        r = ranges[row[3]];key = (row[4], row[5], row[6], row[7])
        if key in seen or key not in dynamic or dynamic[key] != row[3] or row[:3] != [r[0], originals[r[0]][1], frame]:
            raise ValueError('Borrowed inventory is not the first exact unchanged range observation')
        seen.add(key)
        if (key != (r[9], r[8], r[4], r[10]) or row[8] != r[4] or sum(row[9:13]) != row[8] or
                row[13] > row[11] or sum(row[14:16]) != row[13] or sum(row[16:18]) > row[11] or
                row[18] > row[11] or row[19] > row[11] or any(row[20:])):
            raise ValueError('Borrowed metadata inventory counts or permission fields differ')
    if seen != set(dynamic):raise ValueError('Borrowed metadata inventory omitted a dynamic range')
    _pageables(result, True, frame, originals, selected)
    result['verified'] = True
    return result


def validate_capture(capture, requested):
    if capture.get('borrowedTablesRequested', False) is not requested:
        raise ValueError('Borrowed-table capture changed its explicit request')
    if not (requested or capture.get('borrowedTablesAvailable') or 'borrowedTables' in capture):return None
    raw = capture.get('borrowedTables', {})
    checked = validate(raw, requested=requested, source=capture.get('sourceStream'), tail=capture.get('preparedTail'))
    if raw != checked:raise ValueError('Borrowed-table capture omitted its validated observation report')
    return checked
