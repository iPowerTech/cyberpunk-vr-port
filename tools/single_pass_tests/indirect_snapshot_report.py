"""Verify private indirect arguments used by the unchanged-native reference."""
import struct
from retained_geometry_report import validate as validate_retained

ROW_BYTES = 24 * 8


def analyze(stats, raw, source, bindings, retained):
    if len(stats) != 16 or any(type(v) is not int or not 0 <= v < 2**64 for v in stats):
        raise ValueError('Invalid indirect snapshot stats ABI')
    if not isinstance(raw, (bytes, bytearray)) or len(raw) % ROW_BYTES or len(raw) // ROW_BYTES != stats[3]:
        raise ValueError('Invalid indirect snapshot row extent')
    if not bindings.get('verified') or not 0 < stats[3] <= 2048:
        raise ValueError('Indirect snapshots lack verified original bindings')
    originals = [row for row in source['rows'] if row[2]]
    if stats[1] != source['stats'][1] or stats[2] != len(originals) or stats[3] != len(originals):
        raise RuntimeError('Indirect snapshots do not cover the exact original occurrences')
    rows = list(struct.iter_unpack('<24Q', raw))
    known = {row[0]: row for row in bindings['rows']}
    totals = [0] * 5
    for row, original in zip(rows, originals):
        if row[2] not in (0, 1) or row[3] not in (0, 1) or row[20] not in (0, 1) or row[21] not in (0, 1) or row[23]:
            raise ValueError('Indirect snapshot flags or reserved field changed')
        totals[0 if row[2] else 1] += 1
        totals[2] += row[13]
        totals[3] += row[21]
        totals[4] += row[20]
        if (row[0] != original[0] or row[1] != original[1] or row[6] != original[6] or
                row[7] != original[7] or row[8] != original[0] + 1 or row[9] != original[32] or
                row[10] != original[33] or row[19] != original[29] or row[22] != original[8]):
            raise ValueError('Indirect snapshot lost its exact original recording position')
        if not row[2] or not row[3] or row[4] or row[5] or not row[20] or not row[21]:
            raise RuntimeError(f'Indirect snapshot {row[0]} failed: prepare={row[3]}, HRESULT={row[4]:#x}, native={row[5]}, submitted={row[20]}, used={row[21]}')
        native = known[row[0]]
        if native[2] != 0x623CC1 or native[8] != 2 or native[6] not in (0, 3) or not native[26]:
            raise ValueError('Indirect snapshot was admitted outside the verified native contract')
        if (not row[11] or row[11] == row[9] or row[12] or row[13] != 20 or
                row[14] != (native[7] | 0x800) or row[15] != row[9] or not row[16] or row[16] == row[9] or row[17] or row[18]):
            raise ValueError('Indirect snapshot private storage, native read state or typed owner is invalid')
    if list(stats[4:9]) != totals or stats[0] != 3 or stats[5] or stats[9] or stats[10] != 2 or stats[11] != source['commands'] or any(stats[12:]):
        raise RuntimeError('Indirect snapshot closure counters disagree with exact captured commands')
    validate_retained(retained, 'reference', 7)
    return dict(stats=list(stats), rows=[list(row) for row in rows], verified=True,
                frozenIndirectArguments=stats[4], copiedBytes=stats[6], privateReferenceUses=stats[7],
                sourceSubmissionsValidated=stats[8], inputContentsFrozen=False,
                nativeCommandsSuppressed=0, nativeExecutionPermitted=False, performanceGainProved=False)
