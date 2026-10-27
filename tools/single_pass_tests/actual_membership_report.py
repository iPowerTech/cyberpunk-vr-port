"""Actual private B4/full-T5 membership and the AND of its GPU predicates."""
import struct

PREFIX = 'CyberpunkVR_PreparedActualMembership'
U64 = 2**64-1
U32 = 2**32-1
MAX_SOURCES = 512
FIELDS = ('schema', 'requested', 'sourceFrame', 'sourceCount', 'bankCount', 'coveredSlots', 'omittedSlots',
          'dispatches', 'readbackBytes', 'completed', 'enabledSources', 'acceptedSources', 'rejectedSources',
          'checkedRecords', 'checkedSelectors', 'error', 'firstFailureOrdinalPlus1', 'builderFailure',
          'faultRequested', 'referenceB4Overrides', 'pairedBindingChecks', 'CPUwaits', 'nativeSuppressed', 'reserved')
POLICY = 'Actual private B4 and full T5 select only owned bitmap entries; no expected shader-byte comparison'
AND_POLICY = 'GPU AND of per-source acceptance QWORDs against literal one QWORDs; uploaded bytes are predicate constants'


def _words(values, count):
    if not isinstance(values, (list, tuple)) or len(values) != count or any(
            type(v) is not int or not 0 <= v <= U64 for v in values):
        raise ValueError('Actual membership ABI requires bounded uint64 words')
    return list(values)


def decode(stats, rows):
    stats = _words(stats, 24)
    if stats[0] not in (1, 2) or not isinstance(rows, (list, tuple)) or len(rows) > MAX_SOURCES:
        raise ValueError('Actual membership schema or row count differs')
    return dict(stats=stats, summary=dict(zip(FIELDS, stats)), rows=[_words(row, 32) for row in rows],
                inputPolicy=POLICY, aggregateGatePolicy=AND_POLICY, verified=False)


def read_report(read_export, attempts=4):
    def snapshot():
        stats = struct.unpack('<24Q', read_export(PREFIX+'Stats', 192))
        count, = struct.unpack('<I', read_export(PREFIX+'Count', 4))
        row_bytes, = struct.unpack('<I', read_export(PREFIX+'RowBytes', 4))
        if count > MAX_SOURCES or row_bytes != 256:raise ValueError('Actual membership rows exceed fixed ABI storage')
        raw = read_export(PREFIX+'Rows', count*256) if count else b''
        if len(raw) != count*256:raise ValueError('Actual membership rows were truncated')
        return stats, count, row_bytes, raw
    for _ in range(attempts):
        value = snapshot()
        if value == snapshot():return decode(value[0], list(struct.iter_unpack('<32Q', value[3])))
    raise ValueError('Actual membership changed during bounded double read')


def validate_partial(report, *, requested, frame=None):
    if type(requested) is not bool:raise ValueError('Actual membership requires explicit requested mode')
    result = decode(report.get('stats'), report.get('rows'))
    stats = result['stats']
    if stats[1] != int(requested):raise ValueError('Actual membership request differs')
    if not requested:
        if any(stats[1:]) or result['rows']:raise ValueError('Disabled actual membership performed work')
        return result
    if frame is not None and (type(frame) is not int or not 0 < frame <= U64 or stats[2] != frame):
        raise ValueError('Actual membership belongs to a different source frame')
    if stats[3] > MAX_SOURCES or stats[4] > stats[3] or len(result['rows']) > stats[3] or stats[9] not in (0, 1) or stats[18] not in (0, 1):
        raise ValueError('Actual membership partial counts exceed source bounds')
    if stats[15] or stats[16] or stats[17] or any(stats[21:]):
        raise ValueError('Actual membership builder or CPU admission refused the capture')
    return result


def validate(report, *, requested, frame, sources, shared, inputs, rejected=False):
    if type(rejected) is not bool:raise ValueError('Actual membership requires explicit fault mode')
    result = validate_partial(report, requested=requested, frame=frame)
    if not requested:
        result['verified'] = True
        return result
    stats, rows = result['stats'], result['rows']
    if not isinstance(sources, (list, tuple)) or not 0 < len(sources) <= MAX_SOURCES or type(shared) is not int or not 0 <= shared <= 256:
        raise ValueError('Actual membership lacks bounded prepared source identities')
    seen = set()
    for source in sources:
        ordinal, eye = _words(source, 2)
        if eye > 1 or ordinal == U64 or ordinal in seen:raise ValueError('Actual membership source identity duplicates or differs')
        seen.add(ordinal)
    count = len(sources)
    dispatches = count if stats[0] == 1 else len({(row[3], row[5]) for row in rows})
    if (stats[3:5] != [count, count] or len(rows) != count or stats[7:11] != [dispatches, 48*count, 1, count] or
            stats[18:21] != [int(rejected), count, 2*shared]):
        raise ValueError('Actual membership did not finish every bank, dispatch, readback or binding check')
    accepted = refused = records = selectors = covered = omitted = 0
    for row, (ordinal, eye) in zip(rows, sources):
        if row[:3] != [ordinal, 1-eye, frame] or not row[3] or not row[5] or row[4] % 256 or row[6] % 128 or row[7] != 1024*1024:
            raise ValueError('Actual membership source or private B4/T5 identity differs')
        extent, materials, populated, immutable, nulls, skipped, maximum = row[8:15]
        if (not 0 < extent <= 1000000 or materials > 32768 or populated+skipped != materials or immutable+nulls != populated or
                populated > min(extent, 32768) or row[15] != 1 or
                (maximum != U32 if populated == 0 else not 0 <= maximum < min(extent, 32768))):
            raise ValueError('Actual membership bitmap/SourcePass storage accounting differs')
        enabled, error, base, mask, checked, visits, first_bit, first_index = row[16:24]
        accept, reject = row[24:26]
        if (any(v > U32 for v in row[16:24]) or enabled != 1 or any(row[26:]) or accept not in (0, 1) or reject not in (0, 1) or
                accept+reject != 1 or checked > 32 or checked != mask.bit_count() or visits != 5*checked or
                (checked and (base >= 8192 or base+mask.bit_length()-1 >= 8192))):
            raise ValueError('Actual membership GPU predicates, record bounds or selector counts differ')
        if error:
            if not rejected or error != 16 or accept or not reject or not checked or first_bit >= 32 or not mask & (1 << first_bit) or first_index >= 32768:
                raise ValueError('Actual membership fault was not solely a missing owned descriptor')
        elif not accept or reject or first_bit != U32 or first_index != U32:
            raise ValueError('Actual membership success retained a failure or rejected predicate')
        if rejected and bool(error) != bool(checked):
            raise ValueError('Cleared membership bitmap accepted a source with actual selector records')
        accepted += accept;refused += reject;records += checked;selectors += visits;covered += populated;omitted += skipped
    if stats[5:7] != [covered, omitted] or stats[11:15] != [accepted, refused, records, selectors]:
        raise ValueError('Actual membership totals differ from GPU rows')
    if (not rejected and (accepted != count or refused)) or (rejected and not refused):
        raise ValueError('Actual membership selected the wrong positive/fault outcome')
    values = _words(inputs, 8)
    if (values[:3] != [count, count, 8*count] or not 2 <= values[3] <= count+1 or values[4:] != [8*count, int(not rejected), int(rejected), 16]):
        raise ValueError('Actual membership aggregate predicate AND differs; these are not shader-byte expectations')
    result['verified'] = True
    result['faultPolicy'] = 'All membership bitmaps cleared; actual B4/T5 data unchanged' if rejected else 'Actual source inputs unchanged'
    return result


def validate_join(capture, comparison, *, requested, rejected=False):
    if capture.get('actualMembershipRequested', False) is not requested or comparison.get('actualMembershipRequested', False) is not requested:
        raise ValueError('Actual membership changed its requested capture mode')
    if not (requested or capture.get('actualMembershipAvailable') or comparison.get('actualMembershipAvailable') or 'actualMembership' in comparison):return None
    frame = capture.get('sourceStream', {}).get('stats', [0, 0])[1]
    partial = validate_partial(capture.get('actualMembership', {}), requested=requested, frame=frame)
    if capture.get('actualMembership') != partial:raise ValueError('Preparation lost actual membership raw observation')
    from read_prepared_pair import INPUT_FIELDS
    terminal = validate(comparison.get('actualMembership', {}), requested=requested, frame=frame,
        sources=comparison.get('sources'), shared=comparison.get('batch', {}).get('sharedDraws'),
        inputs=[comparison.get('gpuInputs', {}).get(name) for name in INPUT_FIELDS], rejected=rejected)
    if comparison.get('actualMembership') != terminal:raise ValueError('Terminal observer omitted strict actual membership validation')
    if requested:
        t5_rows = capture.get('fullT5', {}).get('rows', [])
        t5 = {row[0]:row for row in t5_rows}
        if len(t5) != len(t5_rows):raise ValueError('Actual membership has duplicate full T5 evidence')
        for row in terminal['rows']:
            full = t5.get(row[0])
            if not full or [row[1], row[2], row[5], row[6], row[7]] != [full[1], full[2], full[7], full[8], full[9]]:
                raise ValueError('Actual membership T5 input differs from the independently verified complete source copy')
    return terminal
