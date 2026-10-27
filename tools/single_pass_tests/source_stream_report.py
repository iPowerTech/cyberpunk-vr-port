"""Decode exact retained source occurrences, not frozen-input admission."""
import struct

ROW_BYTES = 48 * 8
MAX_SOURCES = 2048


def analyze(stats, raw, retained):
    if len(stats) != 16 or any(type(v) is not int or not 0 <= v < 2**64 for v in stats):
        raise ValueError('Invalid source-stream stats ABI')
    if len(raw) % ROW_BYTES or len(raw) // ROW_BYTES != stats[2] or not 0 < stats[2] <= MAX_SOURCES:
        raise ValueError('Invalid source-stream occurrence extent')
    rows = list(struct.iter_unpack('<48Q', raw))
    report = dict(stats=list(stats), rows=[list(row) for row in rows],
                  inputContentsFrozen=False, nativeCommandsSuppressed=0,
                  nativeExecutionPermitted=False, performanceGainProved=False)
    if stats[0] != 3 or stats[12] or any(stats[13:]):
        raise RuntimeError(f'Source stream did not close exact owned geometry: {stats}')
    counts = [0] * 8
    for index, row in enumerate(rows):
        if row[0] != index or row[1] not in (0, 1) or row[2] not in (0, 1, 2) or row[3] != stats[1]:
            raise ValueError('Source-stream order, eye, kind or frame changed')
        if row[43] != 31 or row[44] or row[47] or not all(row[i] for i in (6, 7, 8, 9, 10, 11, 12, 15, 16, 36, 37, 42)):
            raise RuntimeError(f'Unowned source occurrence {index}: flags={row[43]}, failure={row[44]}')
        if row[40] != row[1] or row[41] >= 220 or row[38] >= 65536 or row[15] > 2:
            raise ValueError('Source occurrence native parent or graphics identity is invalid')
        if row[2] and (not row[29] or not row[30] or not row[32] or row[33] % 4 or row[35] % 4):
            raise ValueError('Indirect source operands or padded signature stride are missing')
        counts[0 if row[2] == 0 else 1] += 1
        counts[2 + row[1]] += 1
        for bit in range(4):
            counts[4 + bit] += bool(row[43] & (1 << bit))
    if list(stats[3:11]) != counts or stats[11] != stats[2]:
        raise RuntimeError('Source-stream aggregate counters differ from owned occurrences')
    expected = retained['stats']
    if list(stats[5:7]) != [expected['mainDraws'], expected['peerDraws']]:
        raise RuntimeError('Source stream did not represent every native retained occurrence')
    if stats[4] != sum(retained['indirect']):
        raise RuntimeError('Source stream lost an indirect occurrence')
    report.update(commands=stats[2], direct=stats[3], indirect=stats[4],
                  graphicsTableMasks=sorted({row[13] for row in rows}),
                  indirectCallers=sorted({row[45] for row in rows if row[2]}))
    return report
