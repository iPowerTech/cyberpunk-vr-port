"""Validate early prepared-source packets without granting GPU execution permission."""
import struct
from source_cbv_report import MAX_SOURCES, UINT32_MAX, _no_permission, _source_rows, _words

ROW_BYTES = 64
MAX_MATERIALS = 65536


def analyze_baseline(values, source):
    values = _words(values, 16, 'retained full-baseline stats')
    source_stats, _ = _source_rows(source)
    eyes = []
    for eye in range(2):
        row = values[eye*8:(eye+1)*8]
        if (row[0] != source_stats[1] or row[1] not in (3, 4) or not 0 < row[2] <= 64*1024*1024 or
                row[3:] != [1, 0, 1, 0, 0]):
            raise ValueError('Retained baseline lacks exact frame, bounded storage or writable-target capture')
        eyes.append(dict(eye=eye, frame=row[0], colors=row[1], privateAllocationBytes=row[2],
                         captured=True, writableTargetsValidated=True))
    return dict(verified=True, stats=values, frame=source_stats[1], eyes=eyes,
                fullPreIntervalBaseline=True, nativePassEquivalenceProved=False, **_no_permission())


def analyze(stats, raw, source, guarded=None, *, complete=False):
    stats = _words(stats, 12, 'staged source stats')
    source_stats, original = _source_rows(source)
    if not isinstance(raw, bytes) or len(raw) % ROW_BYTES or len(raw) > MAX_SOURCES * ROW_BYTES:
        raise ValueError('Staged source row ABI or capacity differs')
    rows = list(struct.iter_unpack('<8Q', raw))
    if (stats[:3] != [2, source_stats[1], len(original)] or len(rows) != len(original) or
            stats[3] + stats[4] != len(rows) or stats[5] > MAX_MATERIALS):
        raise ValueError('Staged source construction or aggregate bounds differ')
    if (any(stats[7:10]) or not stats[3] <= stats[6] <= 2 * stats[3] or
            stats[10] > stats[3] or stats[11] > stats[3]):
        raise ValueError('Staged source checks or pre-readback lifecycle differs')
    ready = skipped = materials = 0
    eyes = [0, 0]
    for ordinal, (row, saved) in enumerate(zip(rows, original)):
        if (row[:2] != (ordinal, saved[1]) or row[4:7] != (saved[6], saved[7], ordinal + 1) or
                row[2] not in (0, 1) or row[3] > UINT32_MAX or row[7] > MAX_MATERIALS):
            raise ValueError('Staged source scope, status or bounds differ')
        if row[2]:
            if row[3]:
                raise ValueError('Ready staged source retains a skip reason')
            ready += 1
            eyes[row[1]] += 1
            materials += row[7]
        else:
            if not row[3] or row[7]:
                raise ValueError('Skipped staged source lacks a reason or retained materials')
            skipped += 1
    if stats[3:6] != [ready, skipped, materials] or not all(eyes):
        raise ValueError('Staged source coverage differs or an eye has no ready packet')
    if guarded is not None:
        if (not isinstance(guarded, dict) or guarded.get('verified') is not True or
                not isinstance(guarded.get('rows'), (list, tuple)) or len(guarded['rows']) != len(rows)):
            raise ValueError('Staged sources lack guarded table coverage')
        checks = 0
        for ordinal, (row, saved, guard) in enumerate(zip(rows, original, guarded['rows'])):
            guard = _words(guard, 16, 'guarded source row')
            if (guard[:8] != [ordinal, saved[1], saved[6], saved[7], ordinal + 1, saved[11], 1, 0] or
                    not 1 <= guard[9] <= 2):
                raise ValueError('Staged source guarded checks belong to another occurrence')
            if row[2]:
                checks += guard[9]
        if stats[6] != checks:
            raise ValueError('Staged source input-check total differs from guarded tables')
    if complete and stats[10:] != [ready, ready]:
        raise ValueError('Completed capture did not consume and verify every staged source')
    return dict(verified=True, stats=stats, rows=[list(row) for row in rows], sources=len(rows),
                ready=ready, skipped=skipped, materials=materials, checks=stats[6], readyPerEye=eyes,
                consumedLater=stats[10], verifiedLater=stats[11],
                constructedBeforeCpuReadback=True, laterVerificationComplete=stats[10:] == [ready, ready],
                gpuChecksRequired=True, sourceContentsCpuVerified=False, **_no_permission())
