"""Bounded exact-span cache history metadata; never a reusable byte snapshot."""
import struct
from source_cbv_report import _no_permission, _source_rows


def analyze(raw, source_report, cbv_report, selector_report):
    _, sources = _source_rows(source_report)
    if cbv_report.get('verified') is not True or selector_report.get('verified') is not True:
        raise ValueError('Cache events lack verified source/input reports')
    inputs = selector_report['inputs']
    if not isinstance(raw, (bytes, bytearray)) or len(inputs) > 4096 or len(raw) != len(inputs) * 288:
        raise ValueError('Selector cache-event ABI/count differs')
    rows = list(struct.iter_unpack('<36Q', raw))
    cbvs = {tuple(row[:6]): row for row in cbv_report['rows']}
    evictions = {8, 9, 10, 11, 12}
    for index, (row, input_) in enumerate(zip(rows, inputs)):
        if row[:3] != (index, *input_[:2]) or row[1] >= len(sources) or row[3] not in (0, 1):
            raise ValueError('Cache events borrowed another input occurrence')
        d = row[4:]
        if not row[3]:
            if any(d):
                raise ValueError('Failed cache-event query acquired history')
            continue
        cbv = cbvs.get(tuple(input_[:6]))
        if cbv is None or d[0] != 0x4345565430303031 or d[1] != cbv[12] or not d[2] or d[3] not in (0, 1):
            raise ValueError('Cache-event current held resource/version differs')
        if (d[4], d[5]) != (cbv[10] - cbv[13], input_[6]) or not 0 < d[5] <= 65536 or d[6] > 64:
            raise ValueError('Cache-event exact span/history bound differs')
        receipts = [d[start:start + 8] for start in (8, 16, 24)]
        if (not d[3] and any(d[6:])) or (not d[6] and any(d[8:])):
            raise ValueError('Absent event storage acquired historical facts')
        for kind, event in enumerate(receipts):
            if not any(event):
                continue
            generation, _, _, offset, size, sequence, reason, event_sequence = event
            if not 0 < generation <= d[2] or not size or not event_sequence:
                raise ValueError('Cache-event receipt has no bounded generation/span/sequence')
            if offset + size <= 2**64 - 1 and not (offset < d[4] + d[5] and d[4] < offset + size):
                raise ValueError('Cache-event receipt does not intersect requested span')
            if kind == 0:
                if reason != 1 or not sequence:
                    raise ValueError('Published cache event has no actual copy sequence')
            elif reason not in set(range(2, 14)) | set(range(32, 48)):
                raise ValueError('Invalidation cache event has unknown reason')
            if kind == 2 and (reason not in evictions or not sequence):
                raise ValueError('Eviction cache event has no actual removed-copy receipt')
        invalidated, evicted = receipts[1:]
        if any(evicted) and (not any(invalidated) or evicted[7] > invalidated[7]):
            raise ValueError('Eviction is newer than latest intersecting invalidation')
    return dict(verified=True, rows=[list(r) for r in rows], queries=sum(r[3] for r in rows),
                published=sum(bool(r[3] and r[19]) for r in rows),
                invalidated=sum(bool(r[3] and r[27]) for r in rows),
                evicted=sum(bool(r[3] and r[35]) for r in rows), **_no_permission())
