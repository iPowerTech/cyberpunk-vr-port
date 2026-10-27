"""Validate complete per-occurrence T5 copies against native full readbacks."""
import struct

PREFIX = 'CyberpunkVR_RetainedFullT5'
BYTES = 1024 * 1024
MAX_ROWS = 2048
MAX_U64 = 2**64 - 1
STAT_FIELDS = ('schema', 'requested', 'frame', 'sources', 'copies', 'reuses', 'gpuCopiedBytes',
               'finalizedEyesMask', 'validatedSources', 'comparedSources', 'differentBytes', 'error',
               'arenaBytes0', 'arenaBytes1', 'referenceBytes', 'nativeSuppressed')
ROW_FIELDS = ('ordinal', 'side', 'frame', 'list', 'generation', 'occurrence', 'originalResource',
              'privateResource', 'privateOffset', 'bytes', 'reused', 'gpuCopiedBytes', 'referenceOffset',
              'viewFirstElement', 'verification', 'firstDifferent')


def _words(value, count):
    if not isinstance(value, (list, tuple)) or len(value) != count or any(
            type(word) is not int or not 0 <= word <= MAX_U64 for word in value):
        raise ValueError('Full T5 ABI requires bounded uint64 fields')
    return list(value)


def decode(stats, rows):
    stats = _words(stats, 16)
    if stats[0] != 1 or not isinstance(rows, (list, tuple)) or len(rows) > MAX_ROWS:
        raise ValueError('Full T5 schema or row extent differs')
    return dict(stats=stats, rows=[_words(row, 16) for row in rows], summary=dict(zip(STAT_FIELDS, stats)))


def read_report(read_export, read_memory, attempts=4):
    for _ in range(attempts):
        stats = struct.unpack('<16Q', read_export(PREFIX + 'Stats', 128))
        count, = struct.unpack('<I', read_export(PREFIX + 'Count', 4))
        row_bytes, = struct.unpack('<I', read_export(PREFIX + 'RowBytes', 4))
        pointer, = struct.unpack('<Q', read_export(PREFIX + 'Rows', 8))
        if row_bytes != 128 or count > MAX_ROWS or (count and (not pointer or pointer > MAX_U64-count*128)):
            raise ValueError('Full T5 row pointer or ABI exceeds bounds')
        raw = read_memory(pointer, count*128) if count else b''
        if len(raw) != count*128:
            raise ValueError('Full T5 row payload was truncated')
        if (stats == struct.unpack('<16Q', read_export(PREFIX + 'Stats', 128)) and
                count == struct.unpack('<I', read_export(PREFIX + 'Count', 4))[0] and
                pointer == struct.unpack('<Q', read_export(PREFIX + 'Rows', 8))[0]):
            return decode(stats, list(struct.iter_unpack('<16Q', raw)))
    raise ValueError('Full T5 capture changed while reading')


def validate(stats, rows, *, requested, source=None, tail=None):
    result = decode(stats, rows)
    values, rows = result['summary'], result['rows']
    if type(requested) is not bool or values['requested'] != int(requested):
        raise ValueError('Full T5 request differs from the explicit capture mode')
    if not requested:
        if any(result['stats'][1:]) or rows:
            raise ValueError('Disabled full T5 mode performed work')
        result['verified'] = True
        return result
    from source_cbv_report import _source_rows
    source_stats, source_rows = _source_rows(source)
    if values['frame'] != source_stats[1] or not isinstance(tail, dict) or tail.get('frame') != source_stats[1]:
        raise ValueError('Full T5 frame differs from its original source stream/tail')
    selected = []
    for side in (0, 1):
        eye_rows = [row for row in source_rows if row[1] == side]
        begin = len(eye_rows)
        while begin and eye_rows[begin-1][4] == 9:
            begin -= 1
        suffix = eye_rows[begin:]
        if not suffix or any(row[2] for row in suffix):
            raise ValueError('Full T5 lacks a complete direct final stage-9 interval')
        selected.extend((row[0], side) for row in suffix)
    selected.sort()
    if (tail.get('nativeSources') != [list(value) for value in selected] or
            tail.get('sources') != [[ordinal, 1-side] for ordinal, side in selected] or
            tail.get('sourceCount') != len(selected) or not 0 < len(selected) <= 512 or
            len(rows) != len(selected) or values['sources'] != len(rows)):
        raise ValueError('Full T5 rows do not cover exactly the final native tail')
    if [[row[0], row[1]] for row in rows] != [list(value) for value in selected]:
        raise ValueError('Full T5 source order, side or coverage differs')
    count = len(rows)
    if (values['copies']+values['reuses'] != count or values['gpuCopiedBytes'] != values['copies']*BYTES or
            values['finalizedEyesMask'] != 3 or values['validatedSources'] != count or values['comparedSources'] != count or
            values['referenceBytes'] != count*BYTES or any(values[name] for name in ('differentBytes', 'error', 'nativeSuppressed'))):
        raise ValueError('Full T5 verification, byte accounting or suppression counters differ')
    arenas = [values['arenaBytes0'], values['arenaBytes1']]
    if any(not BYTES <= size <= 8*BYTES or size % BYTES for size in arenas) or sum(arenas) != values['copies']*BYTES:
        raise ValueError('Full T5 full-version arenas exceed their unchanged bounds')
    copies = [0, 0]
    references = [0, 0]
    private_resources = [None, None]
    source_identity = [None, None]
    originals = set()
    for row in rows:
        item = dict(zip(ROW_FIELDS, row))
        ordinal, side = item['ordinal'], item['side']
        original = source_rows[ordinal]
        if (item['frame'] != source_stats[1] or item['list'] != original[6] or item['generation'] != original[7] or
                item['occurrence'] != ordinal+1 or not item['originalResource'] or not item['privateResource'] or
                item['originalResource'] == item['privateResource']):
            raise ValueError('Full T5 occurrence is not joined to its actual source identity')
        if (item['bytes'] != BYTES or item['reused'] not in (0, 1) or
                item['gpuCopiedBytes'] != (0 if item['reused'] else BYTES) or item['privateOffset'] % BYTES or
                item['privateOffset'] > arenas[side]-BYTES or item['viewFirstElement'] != item['privateOffset']//128 or
                item['referenceOffset'] != references[side]*BYTES or item['verification'] != 2 or item['firstDifferent'] != MAX_U64):
            raise ValueError('Full T5 range, original-index view or independent readback differs')
        identity = (item['list'], item['generation'])
        if source_identity[side] not in (None, identity) or private_resources[side] not in (None, item['privateResource']):
            raise ValueError('Full T5 eye changed its recording or private arena')
        source_identity[side] = identity
        private_resources[side] = item['privateResource']
        key = (side, *identity, item['originalResource'], item['privateResource'], item['privateOffset'])
        if item['reused']:
            if key not in originals:
                raise ValueError('Full T5 reuse has no earlier actual copy in the same eye/recording')
        else:
            if key in originals or item['privateOffset'] != copies[side]*BYTES:
                raise ValueError('Full T5 new versions overlap or omit arena storage')
            originals.add(key)
            copies[side] += 1
        references[side] += 1
    if private_resources[0] == private_resources[1] or source_identity[0][0] == source_identity[1][0]:
        raise ValueError('Full T5 eyes share a private arena or recording')
    if sum(copies) != values['copies'] or count-sum(copies) != values['reuses'] or [n*BYTES for n in copies] != arenas:
        raise ValueError('Full T5 row-derived transfer counters differ')
    result['verified'] = True
    return result


def validate_capture(capture, requested):
    if capture.get('fullT5Requested', False) is not requested:
        raise ValueError('Full T5 capture changed its explicit request')
    if not (requested or capture.get('fullT5Available') or 'fullT5' in capture):
        return None
    report = capture.get('fullT5', {})
    verified = validate(report.get('stats'), report.get('rows'), requested=requested,
                        source=capture.get('sourceStream'), tail=capture.get('preparedTail'))
    if report != verified:
        raise ValueError('Full T5 capture omitted its strict validated report')
    return verified
