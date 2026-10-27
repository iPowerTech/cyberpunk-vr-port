"""Fence-completed diagnostic GPU reads of exact original instance intervals."""
import struct
from source_cbv_report import _source_rows, _no_permission


def analyze(raw, blob, cpu_blob, writers, source_report):
    _,sources=_source_rows(source_report)
    if writers.get('verified') is not True or len(writers.get('rows',[]))!=len(sources):
        raise ValueError('Instance GPU reads require exact CPU writer occurrences')
    if not isinstance(raw,(bytes,bytearray)) or len(raw)!=len(sources)*192 or not isinstance(blob,(bytes,bytearray)) or len(blob)>16*1024*1024:
        raise ValueError('Instance GPU read ABI or payload exceeds bounds')
    rows=list(struct.iter_unpack('<24Q',raw));ranges=[];eye_end=[0,0]
    for ordinal,(row,source,cpu) in enumerate(zip(rows,sources,writers['rows'])):
        if row[:2]!=(ordinal,source[1]) or any(row[k] not in (0,1) for k in (2,3,4,21)) or row[5] not in (0,1,2) or any(row[22:]):
            raise ValueError('Instance GPU read scope/flags differ')
        if not row[2]:
            if any(row[3:]):raise ValueError('Unrecorded instance read acquired GPU facts')
            continue
        if not cpu[4] or source[2] or not row[6] or not row[7] or row[8]!=cpu[10] or row[9]!=cpu[12]-cpu[11]+source[24]*cpu[14] or (
                row[10]!=eye_end[source[1]] or row[11]!=cpu[7] or row[12]!=ordinal*8 or row[13:16]!=(source[6],source[7],ordinal+1) or
                row[16]!=cpu[5] or row[17]!=cpu[6]):
            raise ValueError('Instance GPU read borrowed another writer/range/recording')
        eye_end[source[1]]+=row[11]
        if eye_end[source[1]]>16*1024*1024 or (row[3] and not cpu[15]) or row[4]!=row[3]:
            raise ValueError('Instance GPU read lacks original submission permission')
        if not row[4]:
            if row[5] or any(row[18:22]):raise ValueError('Predicated-off read acquired completed bytes')
            continue
        begin=row[18]-1
        if not row[19] or not row[20] or not row[21] or begin<0 or begin>len(blob) or row[11]>len(blob)-begin or (
                not row[17] or row[17]-1>len(cpu_blob) or row[11]>len(cpu_blob)-(row[17]-1)):
            raise ValueError('Instance GPU read has no exact fence-completed payload')
        expected=cpu_blob[row[17]-1:row[17]-1+row[11]]
        if row[5]!=(1 if blob[begin:begin+row[11]]==expected else 2):raise ValueError('Instance GPU byte-match claim differs from payloads')
        ranges.append((begin,begin+row[11]))
    cursor=0
    for begin,end in sorted(ranges):
        if begin!=cursor:raise ValueError('Instance GPU payloads overlap or leave an unowned gap')
        cursor=end
    if cursor!=len(blob):raise ValueError('Instance GPU blob contains unowned bytes')
    return dict(verified=True,rows=[list(row) for row in rows],recorded=sum(r[2] for r in rows),
                submitted=sum(r[4] for r in rows),matched=sum(r[5]==1 for r in rows),mismatched=sum(r[5]==2 for r in rows),
                gpuBytes=cursor,privateIaConsumed=False,wholeInputEquivalenceProved=False,**_no_permission())
