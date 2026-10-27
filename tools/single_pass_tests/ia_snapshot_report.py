"""Validate exact source IA copy receipts, never bytes or draw permission."""
import struct
from source_cbv_report import _source_rows, _no_permission


def _rows(raw, words, limit):
    if not isinstance(raw, (bytes, bytearray)) or len(raw) % (words * 8) or len(raw) // (words * 8) > limit:
        raise ValueError('Invalid native IA ABI extent')
    return list(struct.iter_unpack(f'<{words}Q', raw))


def analyze_reuse(raw, transfer_raw, report, source_report):
    if report.get('verified') is not True:raise ValueError('IA reuse lacks verified source copies')
    _,sources=_source_rows(source_report)
    rows=_rows(raw,8,2048*17);transfers=_rows(transfer_raw,5,2048)
    views=report['views'];observations=report['observations'];commands=report['commands']
    if len(rows)!=len(views) or len(transfers)!=len(commands):raise ValueError('IA reuse row count differs')
    original={(r[0],r[3],r[4]):r[3:] for r in observations}
    by_source={};covered={};arena=[0,0]
    for reuse,view in zip(rows,views):
        source,eye,role,slot,origin,used,offset,size=reuse
        if tuple(view[:4])!=(source,eye,role,slot) or reuse[6:]!=(view[7],view[8]) or used not in (0,1) or not 0<origin<=source+1 or bool(used)!=(origin!=source+1):
            raise ValueError('IA reuse view or origin differs')
        now=original[(source,role,slot)]
        if used:
            if sources[origin-1][1]!=eye or sources[origin-1][6:9]!=sources[source][6:9]:raise ValueError('IA reused a foreign recording/eye')
            spans=[]
            for prior in by_source.get(origin-1,[]):
                old=original[(prior[0],prior[2],prior[3])]
                if old[3]==now[3] and prior[4:6]==view[4:6] and prior[7]-prior[6]==view[7]-view[6]:spans.append((prior[6],prior[6]+prior[8]))
            end=view[6]
            for first,last in sorted(spans):
                if first<=end:end=max(end,last)
            if end<view[6]+size:raise ValueError('IA reuse has no earlier covering source copy')
        by_source.setdefault(source,[]).append(view)
        covered.setdefault((source,used),[]).append((view[4],offset,offset+size))
    def union(spans):
        merged=[]
        for resource,first,last in sorted(spans):
            if merged and merged[-1][0]==resource and first<=merged[-1][2]:merged[-1][2]=max(merged[-1][2],last)
            else:merged.append([resource,first,last])
        return sum(last-first for _,first,last in merged)
    for row,command in zip(transfers,commands):
        source,eye,copied,reused,total=row
        if row[:2]!=tuple(command[:2]) or copied!=union(covered.get((source,0),[])) or reused!=union(covered.get((source,1),[])) or copied+reused!=command[9]:
            raise ValueError('IA actual/reused byte accounting differs')
        if total<arena[eye] or total>64*1024*1024 or total%16 or copied>total-arena[eye]:raise ValueError('IA arena bound or allocation growth differs')
        arena[eye]=total
    return dict(verified=True,rows=[list(r) for r in rows],transfers=[list(r) for r in transfers],
                transferredBytes=sum(r[2] for r in transfers),reusedBytes=sum(r[3] for r in transfers),arenaBytes=arena,
                reusedViews=sum(r[5] for r in rows),**_no_permission())


def analyze(command_raw, observation_raw, view_raw, source_report, selection_raw=None):
    _, sources = _source_rows(source_report)
    commands = _rows(command_raw, 16, 2048)
    observations = _rows(observation_raw, 67, 2048 * 17)
    views = _rows(view_raw, 16, 2048 * 17)
    selections=(_rows(selection_raw,5,2048) if selection_raw is not None else
                [(index,source[1],0,source[19],source[19]) for index,source in enumerate(sources)])
    if len(selections)!=len(sources):raise ValueError('IA selection lost source occurrences')
    for index,(selected,source) in enumerate(zip(selections,sources)):
        if selected[:2]!=(index,source[1]) or selected[2] not in (0,128) or selected[3]!=source[19] or selected[4]!=source[19]&~selected[2] or (
                selected[2] and (source[2] or not source[19]&128)):
            raise ValueError('IA selection changed original slots or omitted an unqualified domain')
    if len(commands) != len(sources):
        raise ValueError('Native IA lost source occurrences')
    grouped = [[] for _ in sources]
    for row in observations:
        if row[0] >= len(sources) or row[1] != sources[row[0]][1] or row[2] != len(grouped[row[0]]):
            raise ValueError('Native IA observation borrowed another source/view')
        grouped[row[0]].append(row[3:])
    copied = [[] for _ in sources]
    for row in views:
        if row[0] >= len(sources) or row[1] != sources[row[0]][1]:
            raise ValueError('Private IA view borrowed another source')
        copied[row[0]].append(row)
    for ordinal, (source, command, observed, private) in enumerate(zip(sources, commands, grouped, copied)):
        if command[:2] != (ordinal, source[1]) or command[10:13] != (source[6], source[7], ordinal + 1):
            raise ValueError('Native IA command scope differs')
        if any(command[k] not in (0, 1) for k in (2, 5, 6, 14, 15)) or command[7] > 32 or command[3] != len(observed):
            raise ValueError('Native IA command flags/reasons/count differ')
        expected = ([(0, 2**32 - 1)] if source[2] != 2 else []) + [(1, slot) for slot in range(16) if selections[ordinal][4] & (1 << slot)]
        if [(row[0], row[1]) for row in observed] != expected:
            raise ValueError('Native IA did not observe the complete consumed view set')
        for row in observed:
            pooled = bool(row[43] & 4096)
            placed = bool(row[43] & 8192)
            if pooled and placed:
                raise ValueError('Native IA cannot be both pooled and placed')
            if row[2] > 32 or row[40:43] != (source[6], source[7], ordinal + 1) or (row[46] and not (pooled or placed)):
                raise ValueError('Native IA observation has another recording or unknown fields')
            if placed and (row[22] != 2 or not row[23] or not row[25] or row[24] > 2**64-1-row[25] or
                           row[24] % 65536 or row[46] != row[24] or row[30] != 1 or row[32] != row[23] or
                           row[33] != row[25] or row[33] < row[5] or row[49] or row[47] or row[48]):
                raise ValueError('Placed IA lacks the exact native getter and retained heap span')
            if pooled and (row[16] != 1 or row[18] != 5 or row[46] > 2**32-1 or row[46] & 7 >= 5 or
                           not row[49] or not 0 < row[51] <= 98304 or row[50] != row[46] >> 16 or
                           not row[5] or row[5] % row[51] or row[5] // row[51] > 1024 or row[50] >= row[5] // row[51] or
                           row[49] != row[29] or row[54] != 1 or row[11] != row[4] + row[50] * row[51] or
                           not 0 < row[12] <= row[51] or row[6] < row[11] or row[7] > row[12] - (row[6] - row[11])):
                raise ValueError('Geometry pool observation has no exact occupied logical backing')
            if row[6] < row[4] or row[9] != row[6] - row[4] or not row[7] or row[9] > row[5] or row[7] > row[5] - row[9]:
                raise ValueError('Native IA original view exceeds its held buffer')
            if row[0] == 0 and row[6:9] != (source[25], source[26], source[27]):
                raise ValueError('Native IA index view differs from original operands')
            if row[2] == 0:
                required_flags = 1023 if pooled else 2047
                if row[43] & required_flags != required_flags or row[19] != 1 or row[22] != (2 if placed else 1) or not row[28] or (
                        not pooled and (row[49] or row[50])) or row[62] or row[63] != 1:
                    raise ValueError('Admitted IA has no current committed/unprotected backing proof')
                if row[10] != row[3] or (not pooled and row[11:13] != (row[4], row[5])) or not row[13] or not row[14] or not row[15] or row[15] > 1048576:
                    raise ValueError('Admitted IA native buffer differs from held full backing')
                if not row[29] or row[30] != (1 if placed else 0) or row[31] != row[3] or (not placed and row[32]) or row[33] < row[5] or not row[34] or not row[35] or row[36] != row[34]:
                    raise ValueError('Admitted IA has no matching native allocation/allocator')
                if row[56] != 1 or row[58:61] != (1, 1, 1) or row[57] not in (0, 65536) or row[61] >> 32 > 1 or row[61] & 0xffffffff > 1:
                    raise ValueError('Admitted IA resource shape or nodes differ')
                required = 2 if row[0] == 0 else 1
                if row[16] == 1:
                    if row[21] or not row[26] or row[27] not in (0, 2755) or row[37:40] != (2755, 8, 1):
                        raise ValueError('Admitted fixed IA has no verified fixed-read creation')
                elif row[16] not in (0, 3) or row[21] & ~4 or row[38] != 2 or row[39] != row[16] or row[37] & required != required or row[37] & ~2755:
                    raise ValueError('Admitted tracked IA is not in its original read state')
        admitted = sum(row[2] == 0 for row in observed)
        unions=[]
        for view in sorted(private,key=lambda r:(r[4],r[7])):
            resource,begin,end=view[4],view[7],view[7]+view[8]
            if unions and unions[-1][0]==resource and begin<=unions[-1][2]:unions[-1][2]=max(unions[-1][2],end)
            else:unions.append([resource,begin,end])
        if command[4] != admitted or command[8] != len(private) or command[9] != sum(end-begin for _,begin,end in unions):
            raise ValueError('Native IA copy/admission aggregates differ')
        if command[6]:
            if not command[2] or not command[5] or command[7] or not command[13] or not command[14] or not command[15] or len(private) != len(observed) or admitted != len(observed):
                raise ValueError('Recorded native IA lacks whole-set ownership/finalization/submission')
        elif private or command[5] or command[8] or command[9] or command[13] or command[14] or command[15]:
            raise ValueError('Deferred native IA acquired private copies')
        for original, row in zip(observed, private):
            if row[2:4] != original[:2] or not row[4] or not row[5] or row[4] == original[3] or row[6] != original[9] or row[8] != original[7] or row[9] < row[8]:
                raise ValueError('Private IA changed its original role/range/backing')
            if not row[10] or row[11:13] != original[7:9] or row[13:16] != (1, 1, 0) or row[7] + row[9] > 64 * 1024 * 1024:
                raise ValueError('Private IA domain/finalization/budget differs')
        for index,view in enumerate(private):
            for other_index,other in enumerate(private[:index]):
                if view[4]==other[4] and view[7]<other[7]+other[8] and other[7]<view[7]+view[8] and (
                        observed[index][3]!=observed[other_index][3] or view[7]-view[6]!=other[7]-other[6]):
                    raise ValueError('Coalesced IA views overlap different original bytes')
    return dict(verified=True, commands=[list(r) for r in commands], observations=[list(r) for r in observations],
                selections=[list(r) for r in selections],completeIaCommands=sum(r[6] and not s[2] for r,s in zip(commands,selections)),
                views=[list(r) for r in views], sourceCommands=len(commands), observedViews=len(observations),
                copiedCommands=sum(r[6] for r in commands), copiedViews=len(views), copiedBytes=sum(r[9] for r in commands),
                deferredCommands=sum(not r[6] for r in commands), gpuBytesMatched=False, privateIaConsumed=False,
                wholeInputEquivalenceProved=False, **_no_permission())
