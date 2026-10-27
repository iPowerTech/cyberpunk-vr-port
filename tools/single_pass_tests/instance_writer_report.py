"""Actual completed CPU writer observations, never native GPU-copy permission."""
import struct
from source_cbv_report import _source_rows, _no_permission


def analyze(raw, blob, source_report):
    _, sources = _source_rows(source_report)
    if not isinstance(raw, (bytes, bytearray)) or len(raw) != len(sources)*384 or not isinstance(blob, (bytes, bytearray)) or len(blob)>16*1024*1024:
        raise ValueError('Native instance writer ABI/payload extent differs')
    rows=list(struct.iter_unpack('<48Q',raw))
    cursor=0
    for ordinal,(row,source) in enumerate(zip(rows,sources)):
        if row[:3]!=(ordinal,source[1],source[2]) or row[8:10]!=(source[10],source[12]) or any(row[k] not in (0,1) for k in (3,4,15)):
            raise ValueError('Instance writer borrowed another source or shader')
        d=row[16:]
        if not row[3]:
            if row[4] or any(row[5:8]) or row[15] or any(d):raise ValueError('Unqueried source acquired CPU writer facts')
            continue
        if source[2] or not source[19]&128 or d[0]>18 or d[1]&~63 or d[3:7]!=(source[6],source[7],ordinal+1,source[1]):
            raise ValueError('Instance query has an indirect/foreign scope or unknown facts')
        if not row[4]:
            if any(row[5:8]) or row[15] or d[1]&1:raise ValueError('Rejected query acquired owned CPU bytes')
            continue
        if d[0]!=1 or d[1]&31!=31 or not row[5] or row[6]!=cursor+1 or not 0<row[7]<=65536 or row[7]>len(blob)-cursor:
            raise ValueError('Completed instance writer lacks its genuine owner/payload')
        if d[9]!=row[10] or not d[9] or not d[7] or not d[8] or not 0<d[10]<=1048576 or d[11]!=5 or d[12]!=row[14] or d[13] not in (2*1024*1024,6*1024*1024):
            raise ValueError('Instance writer backing/stride/class differs')
        if (d[12],d[13]) not in ((48,6*1024*1024),(64,2*1024*1024)) or not row[11] or not row[12] or (
                row[11]>2**64-1-d[13] or row[12]>2**64-1-row[13]) or row[12]<row[11] or d[14]!=row[12]-row[11] or row[13]>d[13]-d[14]:
            raise ValueError('Instance original view escapes its held ring')
        first,count=source[24],source[21]
        if d[15:17]!=(first,count) or not count or d[25]!=row[7] or row[7]!=count*d[12] or (first+count)*d[12]>row[13]:
            raise ValueError('CPU bytes differ from the exact selected instance records')
        physical=d[14]//d[12]+first
        capacity=d[13]//d[12]
        reserved=d[20]-d[19]
        reserve_begin=d[21] if reserved<=capacity-d[21]%capacity else d[21]+capacity-d[21]%capacity
        if not 0<reserved<=capacity or reserve_begin!=d[19] or d[14]%d[12] or d[17]%capacity!=physical or not d[18] or d[19]>d[17] or d[17]+count>d[20] or d[22]>d[17]:
            raise ValueError('Instance writer has no exact live absolute reservation range')
        if not all(d[k] for k in (2,26,27,28,29,30)) or not row[7]<=d[31]<=16*1024*1024:
            raise ValueError('Instance producer recording/lifetime bounds differ')
        if bool(d[1]&32)!=bool(d[23]) or (not d[23] and d[24]) or (d[23] and d[24]<d[17]+count):
            raise ValueError('Instance publication labels exceed the completed range')
        cursor+=row[7]
    if cursor!=len(blob):raise ValueError('Instance payload contains unowned trailing bytes')
    return dict(verified=True,rows=[list(r) for r in rows],sources=len(rows),queried=sum(r[3] for r in rows),
                captured=sum(r[4] for r in rows),cpuBytes=cursor,validatedAtSourceSubmission=sum(r[15] for r in rows),
                sourceGpuBytesMatched=False,protectedOriginalEpochProved=False,privateIaConsumed=False,**_no_permission())
