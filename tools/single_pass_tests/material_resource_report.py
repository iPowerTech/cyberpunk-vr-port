"""Check held material dependencies; never grant a future draw/read epoch."""
from collections import Counter
import struct
from source_cbv_report import _source_rows, _no_permission


def analyze_buffers(raw, source_report, material_raw):
    _, sources=_source_rows(source_report)
    if not isinstance(material_raw,(bytes,bytearray)) or len(material_raw)%192 or len(material_raw)>65536*192:
        raise ValueError('Material buffer dependencies have invalid bounds')
    if not isinstance(raw,(bytes,bytearray)) or len(raw)%320 or len(raw)>4096*320:
        raise ValueError('Material buffer nomination ABI or bound differs')
    material={}
    for row in struct.iter_unpack('<24Q',material_raw):
        if row[4]!=0 or row[19]!=1 or row[2]!=23 or row[3] not in (4,6):continue
        key=(row[0],row[2],row[3])
        if key in material or row[0]>=len(sources) or row[1]!=sources[row[0]][1] or row[14:17]!=(sources[row[0]][6],sources[row[0]][7],row[0]+1):
            raise ValueError('Material buffer dependency scope differs')
        material[key]=row
    rows=list(struct.iter_unpack('<40Q',raw))
    seen=set()
    for row in rows:
        ordinal,eye,parameter,register,status=row[:5]
        key=(ordinal,parameter,register)
        if key not in material or key in seen or ordinal>=len(sources) or eye!=sources[ordinal][1] or not 0<=status<=10:
            raise ValueError('Material buffer nomination belongs to another source/register')
        seen.add(key)
        if row[5]!=material[key][5] or not row[6] or not row[7] or row[33:36]!=(sources[ordinal][6],sources[ordinal][7],ordinal+1) or any(row[38:]):
            raise ValueError('Material buffer scope differs or acquired execution permission')
        if status==1:
            stride=4 if register==4 else 64
            if (not 0<row[12]<=0x100000 or not 0<=row[13]<16 or row[13]==4 or not row[14] or not row[36] or not row[37] or row[10]!=1 or
                    row[6]+row[7]>=2**64 or row[9]>row[14] or row[15]>15 or row[8]!=row[29]*stride or row[9]!=row[30]*stride or
                    not row[9] or row[8]>row[7] or row[9]>row[7]-row[8]):
                raise ValueError('Captured material buffer has invalid native/view bounds')
            if register==4 and (row[28],row[31],row[32])!=(39,0,1) or register==6 and (row[28],row[31],row[32])!=(0,64,0):
                raise ValueError('Material buffer raw/structured contract differs')
    if seen!=set(material):
        raise ValueError('A material buffer source nomination was omitted')
    return dict(verified=True,rows=[list(row) for row in rows],statuses=dict(Counter(row[4] for row in rows)),
                captured=sum(row[4]==1 for row in rows),**_no_permission())


def analyze_buffer_copies(raw, buffer_report):
    if buffer_report.get('verified') is not True or not isinstance(raw,(bytes,bytearray)) or len(raw)%128 or len(raw)>4096*128:
        raise ValueError('Material buffer copies lack valid nominations or bounds')
    metadata={(row[0],row[3]):row for row in buffer_report['rows']}
    rows=list(struct.iter_unpack('<16Q',raw))
    seen=set();owners=set();ranges={}
    for row in rows:
        key=(row[0],row[2])
        if key in seen or key not in metadata:
            raise ValueError('Material buffer copy has an unrelated or duplicate register')
        seen.add(key)
        source=metadata[key]
        if row[1]!=source[1] or row[3]!=source[4] or row[13:]!=tuple(source[33:36]) or any(row[i] not in (0,1) for i in (10,11,12)) or not row[12]<=row[11]<=row[10]:
            raise ValueError('Material buffer copy scope or lifecycle differs')
        if row[10]:
            if row[3]!=1 or row[4] or not row[5] or not row[6] or row[5]==row[6] or row[6]==source[5] or (row[7],row[9])!=(source[8],source[9]) or row[8]%128 or row[8]>8*1024*1024 or row[9]>8*1024*1024-row[8]:
                raise ValueError('Material buffer copy changed its exact original view')
            if row[5] in owners:raise ValueError('Material buffer copies reused a source occurrence owner')
            owners.add(row[5])
            spans=ranges.setdefault(row[6],[])
            if any(row[8]<end and begin<row[8]+row[9] for begin,end in spans):
                raise ValueError('Material buffer copies overlap independent source versions')
            spans.append((row[8],row[8]+row[9]))
        elif not row[4] or any(row[5:13]):
            raise ValueError('Unrecorded material buffer acquired private storage')
    if seen!=set(metadata):raise ValueError('Material buffer copy receipt omitted a nomination')
    return dict(verified=True,rows=[list(row) for row in rows],recorded=sum(row[10] for row in rows),complete=sum(row[12] for row in rows),
                bytesCopied=sum(row[9] for row in rows),**_no_permission())


def analyze(raw, source_raw, source_report, assembly, overlay, buffer_copies=None):
    _, sources = _source_rows(source_report)
    if assembly.get('verified') is not True or overlay.get('verified') is not True:
        raise ValueError('Material dependencies lack verified source assembly/overlay')
    if not isinstance(raw, (bytes, bytearray)) or len(raw) % 192 or len(raw) > 65536*192:
        raise ValueError('Material dependency ABI or bound differs')
    if not isinstance(source_raw, (bytes, bytearray)) or len(source_raw) != len(sources)*40:
        raise ValueError('Material source occurrence set differs')
    rows = list(struct.iter_unpack('<24Q', raw))
    summaries = list(struct.iter_unpack('<5Q', source_raw))
    grouped = [[] for _ in sources]
    overlays = {row[0]: row for row in overlay['rows']}
    buffers={(row[0],row[2]):row for row in buffer_copies['rows']} if buffer_copies and buffer_copies.get('verified') is True else {}
    seen = set()
    for row in rows:
        ordinal, eye, parameter, offset, kind = row[:5]
        if ordinal >= len(sources) or eye != sources[ordinal][1] or parameter >= 64 or kind not in (0, 1):
            raise ValueError('Material dependency belongs to another source/type')
        if row[14:17] != (sources[ordinal][6], sources[ordinal][7], ordinal+1) or any(row[21:]):
            raise ValueError('Material scope differs or acquired execution')
        key = (ordinal, parameter, offset)
        if key in seen:
            raise ValueError('Material dependency duplicated a source slot')
        seen.add(key)
        status = row[8]
        if status not in range(1, 8) or any(row[n] not in (0, 1) for n in (17, 18)):
            raise ValueError('Unknown material status')
        if kind == 1 and status != 3 or kind == 0 and status == 3:
            raise ValueError('UAV was treated as an immutable SRV')
        if status == 1:
            if any(row[n] for n in (5, 6, 7, 9, 10, 11, 12, 13, 19, 20)):
                raise ValueError('Null material acquired a resource/owner')
            if row[18] != row[17]:
                raise ValueError('Null material source submission changed')
        elif status == 5:
            if not row[5] or row[6] or not row[7] or row[9] or not row[10] or not 0 < row[11] <= 32768 or not row[12] or not row[13]:
                raise ValueError('Immutable texture lacks its physical/content owners')
            if row[19] not in (2, 3, 4) or not row[20] or row[18] > row[17]:
                raise ValueError('Immutable texture shape or submission differs')
        elif status == 6:
            view = overlays.get(ordinal)
            if (parameter, offset, row[19]) != (23, 5, 1) or not view or row[10] != view[9] or view[27] != 1 or row[18] != 1:
                raise ValueError('Material T5 override lacks the completed private view')
            if any(row[11:14]) or row[17]:
                raise ValueError('Private T5 acquired a native immutable-texture receipt')
        elif status == 7:
            copied=buffers.get((ordinal,offset))
            if parameter!=23 or offset not in (4,6) or row[19]!=1 or not copied or not copied[12] or row[10]!=copied[5] or row[17:19]!=(1,1) or any(row[11:14]):
                raise ValueError('Material buffer override lacks its completed original copy')
        elif any(row[10:14]) or row[17] or row[18]:
            raise ValueError('Deferred material acquired physical ownership/readiness')
        grouped[ordinal].append(row)
    if len(assembly['commands']) != len(sources):
        raise ValueError('Material assembly count differs')
    for ordinal, (row, source, command) in enumerate(zip(summaries, sources, assembly['commands'])):
        if row[:3] != (ordinal, source[1], len(grouped[ordinal])) or row[3] not in (0, 1) or row[4] not in (0, 1):
            raise ValueError('Material source counts/truncation differ')
        expected = bool(command[4] and not row[4] and all(value[18] for value in grouped[ordinal]))
        if bool(row[3]) != expected:
            raise ValueError('Material readiness concealed a missing dependency')
    return dict(verified=True, rows=[list(r) for r in rows], sources=[list(r) for r in summaries],
                dependencies=len(rows), states=dict(Counter(row[8] for row in rows)),
                readySources=sum(row[3] for row in summaries),
                pinnedTextures=sum(row[8] == 5 for row in rows),
                validAfterSource=sum(row[8] == 5 and row[18] for row in rows),
                destinationReadEpochProved=False, **_no_permission())
