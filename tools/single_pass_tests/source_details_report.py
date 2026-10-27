"""Current-source resource observations; never a copy or execution certificate."""
from collections import Counter
import struct

from source_cbv_report import analyze as analyze_cbvs, _words

ROW_BYTES = 1024


def analyze(stats, raw, source_report, cbv_report):
    stats = _words(stats, 16, 'source detail stats')
    if not isinstance(cbv_report, dict) or not isinstance(cbv_report.get('rows'), (list, tuple)):
        raise ValueError('Missing owned source CBV rows')
    cbvs = [_words(row, 24, 'source CBV row') for row in cbv_report['rows']]
    checked = analyze_cbvs(cbv_report['stats'], b''.join(struct.pack('<24Q', *row) for row in cbvs), source_report)
    if not isinstance(raw, (bytes, bytearray, memoryview)) or (isinstance(raw, memoryview) and
            (not raw.c_contiguous or raw.itemsize != 1)) or len(raw) != len(cbvs) * ROW_BYTES:
        raise ValueError('Source detail binary extent/type differs from current CBV rows')
    if stats[0] != 3 or stats[1:4] != [checked['stats'][1], checked['stats'][2], len(cbvs)] or any(stats[14:]):
        raise ValueError('Source detail capture did not close the matching occurrence set')
    rows = list(struct.iter_unpack('<128Q', raw))
    counts = [0] * 8
    observations = []
    for index, (row, cbv) in enumerate(zip(rows, cbvs)):
        source = source_report['rows'][cbv[0]]
        if row[:7] != (index, *cbv[:6]) or row[7] != 1 or row[39] or any(row[62:64]) or any(row[91:96]) or row[127]:
            raise ValueError('Source detail association, success or reserved fields differ')
        if row[8] not in (1, 2, 3, 4, 5) or row[11:14] != (cbv[14], cbv[13], cbv[10] - cbv[13]) or (
                row[14:18] != (cbv[16], cbv[15], source[11], source[6])) or row[33:39] != (
                row[30], cbv[21], cbv[12], cbv[10], cbv[11], cbv[6]):
            raise ValueError('Source detail resource/context/view identity differs')
        if row[18] not in (0, 1, 2) or row[19] not in (0, 1) or (not row[19] and row[20]) or (
                row[18] == 0 and any(row[19:24])) or (row[18] == 1 and any(row[21:24])):
            raise ValueError('Source resource creation receipt is inconsistent')
        if row[18] == 2 and (not row[21] or not row[23] or row[22] > 2**64 - 1 - row[23]):
            raise ValueError('Source placement receipt lacks a finite owned API heap span')
        if row[24] != 1 or row[25] not in (1, 2) or row[27] or row[28] not in range(1, 10):
            raise ValueError('Source recording context/state observation is invalid')
        if row[28] not in (1, 2, 8, 9) and row[29]:
            raise ValueError('Unknown source state was represented as a numeric state')
        if row[28] == 1 and (row[8] != 2 or row[29] != 0xAC3):
            raise ValueError('Fixed UPLOAD state is inconsistent with the actual heap')
        if row[31] not in (0, 1) or bool(row[31]) != bool(cbv[21]) or bool(row[32]) != bool(row[31]):
            raise ValueError('Native source B0 observation presence differs')
        native = struct.pack('<22Q', *row[40:62])
        if row[31]:
            if (native[6] & 15) != 6 or native[6] >> 4 != row[33] or struct.unpack_from('<I', native)[0] != cbv[22]:
                raise ValueError('Native B0 type/logical metadata differs from the held source')
            if row[33] != 4 and struct.unpack_from('<2Q', native, 8) != (cbv[10], cbv[12]):
                raise ValueError('Registered B0 resource/address differs from the held source')
        elif any(row[40:62]):
            raise ValueError('Unobserved creator was assigned a fabricated native B0 record')
        for known, address, start, end in ((64, 65, 66, 79), (79, 80, 81, 84), (96, 97, 99, 106)):
            if row[known] not in (0, 1) or bool(row[known]) != bool(row[address]) or (not row[known] and any(row[start:end])):
                raise ValueError('Borrowed native header presence is inconsistent')
        if row[84] not in (0, 1) or (not row[84] and any(row[85:91])) or (
                row[84] and (row[33] != 1 or not row[85] or not row[86] or row[89] != 512)):
            raise ValueError('Native pool observation is inconsistent or belongs to another class')
        if row[33] == 4 and (row[64] or row[79] or row[84] or row[96]):
            raise ValueError('Transient B0 fields were misrepresented as arena allocation owners')
        if row[96] == 0 and row[98]:
            raise ValueError('Unobserved alias control was supplied')
        if any(value > 8 for value in row[106:110]) or row[110] > 4 or any(value not in (0, 1) for value in row[111:118]):
            raise ValueError('Invalid borrowed-header observation/association flags')
        if row[24] and (not row[123] or row[26] > 2**32 - 1):
            raise ValueError('Current command context has an invalid state/pending extent')
        for captured, observation in ((64, 106), (79, 107), (96, 109)):
            if bool(row[captured]) != (row[observation] in (1, 7, 8)):
                raise ValueError('Borrowed-header bytes and observation status disagree')
        if bool(row[84]) != (row[108] == 1):
            raise ValueError('Pool snapshot and observation status disagree')
        counts[0] += 1
        if row[8] in (1, 2, 5):counts[{1: 2, 2: 3, 5: 4}[row[8]]] += 1
        counts[5] += bool(row[18]);counts[6] += bool(row[31]);counts[7] += bool(row[64])
        observations.append(dict(index=index, sourceOrdinal=cbv[0], side=cbv[1], visibility=cbv[2], shaderRegister=cbv[3],
            heapType=row[8], heapFlags=row[9], resourceFlags=row[10], creationKind=row[18], initialStateKnown=bool(row[19]),
            initialState=row[20] if row[19] else None, nativeKind=row[33], stateStatus=row[28], stateValue=row[29],
            numericStateKnown=row[28] in (1, 2), allocationHeaderCaptured=bool(row[64]), allocationObservation=row[106],
            allocatorObservation=row[107], allocationPath=row[110], poolKnown=bool(row[84]),
            allocationResourceMatches=bool(row[112]), allocationExtentCovers=bool(row[113]), allocatorPimplMatches=bool(row[117]),
            borrowedHeaderOnly=True, allocationPinned=False, inputContentsFrozen=False, copyPermission=False))
    expected = [3, stats[1], stats[2], len(rows), counts[0], 0, counts[2], counts[3], counts[4], counts[5], counts[6],
                counts[7], sum(bool(row[84]) for row in rows), 0, 0, 0]
    if stats != expected:raise ValueError('Source detail aggregate differs from current rows')
    return dict(stats=stats, rows=[list(row) for row in rows], observations=observations, verified=True,
        heapTypes=dict(Counter(row[8] for row in rows)), nativeKinds=dict(Counter(row[33] for row in rows)),
        stateStatuses=dict(Counter(row[28] for row in rows)), creationKinds=dict(Counter(row[18] for row in rows)),
        inputContentsFrozen=False, nativeExecutionPermission=False, copyPermission=False, allocationPinned=False,
        nativeCommandsSuppressed=0, performanceGainProved=False)
