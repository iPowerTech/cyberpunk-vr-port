"""Bounded fixed-asset bootstrap observations, never GPU access permission."""
import struct

PREFIX = 'CyberpunkVR_Retained'
MAX_U64 = 2**64-1
NO_ASSET = 2**32-1
STAT_FIELDS = ('schema', 'requested', 'frame', 'attemptedRanges', 'coherentRanges',
               'allSlotsQualifiedRanges', 'slots', 'assets', 'immutableSlots',
               'immutableAssets', 'refusedAssets', 'captureFailures', 'reserved0',
               'reserved1', 'permission', 'nativeSuppressed')
STATUS = ('Unknown', 'TypedNull', 'Immutable', 'UnsupportedDescriptor', 'UnsupportedView',
          'UnsupportedShape', 'MissingCandidate', 'PinRefused', 'CookieMismatch', 'ImmutableRefused')
ARRAYS = (('rows', 'FixedAssets', 24, 256), ('assets', 'FixedAsset', 16, 65536),
          ('slots', 'FixedSlot', 8, 262144))


def _words(value, count):
    if not isinstance(value, (list, tuple)) or len(value) != count or any(
            type(v) is not int or not 0 <= v <= MAX_U64 for v in value):
        raise ValueError('Fixed-assets ABI requires bounded uint64 fields')
    return list(value)


def _rows(value, words, limit):
    if not isinstance(value, (list, tuple)) or len(value) > limit:
        raise ValueError('Fixed-assets payload exceeds its bounded extent')
    return [_words(row, words) for row in value]


def decode(stats, counts, rows, assets, slots, samples=None):
    stats = _words(stats, 16)
    if stats[0] != 1:raise ValueError('Fixed-assets schema differs')
    result = dict(stats=stats, summary=dict(zip(STAT_FIELDS, stats)), counts=_words(counts, 10),
                statusCounts=dict(zip(STATUS, counts)), rows=_rows(rows, 24, 256),
                assets=_rows(assets, 16, 65536), slots=_rows(slots, 8, 262144),
                verified=False, bootstrapExercised=False, allSlotsQualified=False,
                classification='OBSERVATION', resourceReadPermission=False,
                executionPermission=False, residencyPermission=False,
                nativeSuppressionPermission=False)
    if samples is not None:result['samples'] = _rows(samples, 24, 160)
    return result


def read_report(read_export, read_memory, *, samples_available=False, attempts=4):
    if type(attempts) is not int or not 1 <= attempts <= 16:
        raise ValueError('Fixed-assets snapshot attempt bound differs')
    def snapshot():
        data = dict(stats=list(struct.unpack('<16Q', read_export(PREFIX+'FixedAssetsStats', 128))),
                    counts=list(struct.unpack('<10Q', read_export(PREFIX+'FixedAssetsCounts', 80))))
        identities = []
        definitions = ARRAYS + (('samples', 'FixedDescriptor', 24, 160),) if samples_available else ARRAYS
        for name, suffix, words, limit in definitions:
            count, = struct.unpack('<I', read_export(PREFIX+suffix+'Count', 4))
            size, = struct.unpack('<I', read_export(PREFIX+suffix+'RowBytes', 4))
            pointer, = struct.unpack('<Q', read_export(PREFIX+suffix+'Rows', 8))
            if size != words*8 or count > limit or (count and (not pointer or pointer > MAX_U64-count*size)):
                raise ValueError('Fixed-assets pointer or payload ABI exceeds bounds')
            raw = read_memory(pointer, count*size) if count else b''
            if len(raw) != count*size:raise ValueError('Fixed-assets payload is truncated')
            data[name] = [list(row) for row in struct.iter_unpack(f'<{words}Q', raw)]
            identities.append((count, size, pointer))
        return data, identities
    for _ in range(attempts):
        first = snapshot()
        if first == snapshot():
            value = first[0]
            return decode(value['stats'], value['counts'], value['rows'], value['assets'], value['slots'],value.get('samples'))
    raise ValueError('Fixed-assets capture changed during bounded double payload read')


def validate(report, *, requested, source=None, tail=None, borrowed=None):
    if type(requested) is not bool or not isinstance(report, dict):
        raise ValueError('Fixed-assets observation needs an explicit request and report')
    result = decode(*(report.get(name) for name in ('stats', 'counts', 'rows', 'assets', 'slots')),samples=report.get('samples'))
    stats, counts, rows, assets, slots = (result[name] for name in ('stats', 'counts', 'rows', 'assets', 'slots'))
    if stats[1] != int(requested) or any(stats[12:]):raise ValueError('Fixed-assets request or reserved/permission fields differ')
    if not requested:
        if any(stats[1:]) or any(counts) or rows or assets or slots or result.get('samples'):
            raise ValueError('Disabled fixed-assets observation performed work')
        result['verified'] = True
        return result
    from borrowed_tables_report import validate as validate_borrowed
    from source_cbv_report import _source_rows
    certified = validate_borrowed(borrowed, requested=True, source=source, tail=tail)
    source_stats, originals = _source_rows(source)
    frame = source_stats[1]
    if stats[2] != frame or not 0 < len(rows) <= 4 or stats[3:5] != [len(rows), len(rows)] or stats[11]:
        raise ValueError('Fixed-assets captures are missing, incoherent or from another frame')
    inventories = certified['inventories']
    if len(rows) != len(inventories):raise ValueError('Fixed-assets observation omitted a unique dynamic range')
    ranges = certified['ranges'];borrowed_sources = {row[0]: row for row in certified['rows']}
    asset_cursor = slot_cursor = qualified = immutable_slots = immutable_assets = refused_assets = 0
    totals = [0]*10;seen_ranges = set()
    for capture, row in enumerate(rows):
        ordinal, side, source_frame, range_index, heap, gpu, count, version = row[:8]
        if ordinal >= len(originals) or range_index >= len(ranges):raise ValueError('Fixed-assets source or range is absent')
        original = originals[ordinal];bound = borrowed_sources.get(ordinal);r = ranges[range_index]
        key = (heap, gpu, count, version)
        inventory = next((value for value in inventories if value[3] == range_index), None)
        if (key in seen_ranges or inventory is None or inventory[:8] != row[:8] or
                key != (r[9], r[8], r[4], r[10]) or r[0] != ordinal or r[2] != 0 or r[11] != 1 or
                not bound or side != original[1] or source_frame != frame or row[14:17] != [original[11], original[10], bound[7]] or
                bound[3:6] != [original[6], original[7], original[11]]):
            raise ValueError('Fixed-assets capture lost exact first-range/source/PSO provenance')
        seen_ranges.add(key)
        if (not 0 < count <= 65536 or row[8] != 1 or row[9] not in (0, 1) or any(row[17:]) or
                row[10] != asset_cursor or row[12] != slot_cursor or row[13] != count or
                row[11] > len(assets)-asset_cursor or row[13] > len(slots)-slot_cursor):
            raise ValueError('Fixed-assets capture extents or coherent flags differ')
        owned = assets[asset_cursor:asset_cursor+row[11]]
        used = set();dedup = {};local_counts = [0]*10
        for local, asset in enumerate(owned):
            if (asset[:2] != [capture, local] or not asset[2] or not asset[3] or
                    asset[5] not in (2, 7, 8, 9) or (asset[2], asset[3]) in dedup):
                raise ValueError('Fixed-asset deduplication, identity or status differs')
            dedup[(asset[2], asset[3])] = local
            status, stage, failure = asset[5:8]
            if status == 2:
                if (asset[4] != asset[2] or stage or failure or not all(asset[8:13]) or
                        asset[9] > 32768 or asset[10] != asset[3] or any(asset[13:])):
                    raise ValueError('Immutable asset lacks exact cookie or native allocation pins')
                immutable_assets += 1
            else:
                refused_assets += 1
                if not failure or (status == 7 and stage not in (1, 2)) or (status == 8 and stage != 3) or (status == 9 and stage not in (4, 5)):
                    raise ValueError('Refused fixed asset omitted its actual bootstrap stage')
                if status == 9 and asset[4] != asset[2]:raise ValueError('Immutable refusal did not first prove its cookie')
        for local, slot in enumerate(slots[slot_cursor:slot_cursor+count]):
            if slot[0] != capture or slot[1] != r[3]+local or slot[5] >= len(STATUS) or any(slot[6:]):
                raise ValueError('Fixed slot order, capture, status or reserved fields differ')
            status = slot[5];local_counts[status] += 1;totals[status] += 1
            if slot[2] == NO_ASSET:
                if status in (2, 7, 8, 9):raise ValueError('Owned/refused fixed slot has no asset row')
                if status == 1 and (slot[3] or slot[4]):raise ValueError('Typed-null slot contains a resource candidate')
                if status == 6 and (not slot[3] or slot[4]):raise ValueError('Missing-candidate slot is not missing its candidate')
            else:
                if slot[2] >= len(owned):raise ValueError('Fixed slot references a missing local asset')
                asset = owned[slot[2]];used.add(slot[2])
                view_refusal = status == 4 and (asset[5] == 2 or (asset[5] == 9 and asset[6] == 5))
                if slot[3:5] != asset[2:4] or (status != asset[5] and not view_refusal):
                    raise ValueError('Fixed slot cookie/candidate/status differs from its held asset')
            immutable_slots += status == 2
        if used != set(range(len(owned))):raise ValueError('Fixed asset was not used by any descriptor slot')
        # The fixed classifier rejects buffer/AS/implicit SRVs before decoding;
        # those UnsupportedView slots can be null, nonnull or malformed in the
        # broader inventory. Only compare categories with a definite mapping.
        if (local_counts[1] > inventory[10] or local_counts[0] > inventory[9] or
                sum(local_counts[i] for i in (2, 5, 6, 7, 8, 9)) > inventory[11]):
            raise ValueError('Fixed slot classification disagrees with its canonical descriptor inventory')
        complete = local_counts[1]+local_counts[2] == count
        if complete and (local_counts[1] != inventory[10] or local_counts[2] != inventory[11] or inventory[9] or inventory[12]):
            raise ValueError('Complete fixed range disagrees with its exact descriptor inventory')
        if row[9] != int(complete):raise ValueError('Fixed range falsely claims complete slot ownership')
        qualified += complete;asset_cursor += len(owned);slot_cursor += count
    if (asset_cursor != len(assets) or slot_cursor != len(slots) or counts != totals or
            stats[5:11] != [qualified, len(slots), len(assets), immutable_slots, immutable_assets, refused_assets]):
        raise ValueError('Fixed-assets totals differ from detailed classified rows')
    if not immutable_assets:raise ValueError('Requested fixed-assets test did not exercise a successful native bootstrap')
    seen_samples=set();sample_counts={}
    for sample in result.get('samples',[]):
        capture,offset,status,asset=sample[:4]
        if capture>=len(rows) or status>=len(STATUS) or (capture,offset) in seen_samples:
            raise ValueError('Fixed-assets descriptor samples are duplicated or unbound')
        row=rows[capture];first=ranges[row[3]][3]
        if offset<first or offset-first>=row[13]:raise ValueError('Fixed descriptor sample lies outside its range')
        slot=slots[row[12]+offset-first]
        if (slot[0],slot[1],slot[5],slot[2])!=(capture,offset,status,asset) or slot[3]!=sample[6]:
            raise ValueError('Fixed descriptor sample differs from its original slot')
        seen_samples.add((capture,offset));key=(capture,status);sample_counts[key]=sample_counts.get(key,0)+1
        if sample_counts[key]>4:raise ValueError('Fixed descriptor sample exceeds its status limit')
    result.update(verified=True, bootstrapExercised=True, allSlotsQualified=qualified == len(rows))
    return result


def validate_capture(capture, requested):
    if capture.get('fixedAssetsRequested', False) is not requested:
        raise ValueError('Fixed-assets capture changed its explicit request')
    if not (requested or capture.get('fixedAssetsAvailable') or 'fixedAssets' in capture):return None
    raw = capture.get('fixedAssets', {})
    checked = validate(raw, requested=requested, source=capture.get('sourceStream'),
                       tail=capture.get('preparedTail'), borrowed=capture.get('borrowedTables'))
    if raw != checked:raise ValueError('Fixed-assets capture omitted its validated diagnostic report')
    return checked
