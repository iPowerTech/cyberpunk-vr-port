"""Current exact-range cache diagnostics, with no historical candidate reuse."""
import struct
from source_cbv_report import _no_permission, _source_rows


def analyze(raw, source_report, cbv_report, selector_report):
    _,sources=_source_rows(source_report)
    if cbv_report.get('verified') is not True or selector_report.get('verified') is not True:
        raise ValueError('Coverage lacks verified current source/input reports')
    inputs=selector_report['inputs']
    if not isinstance(raw,(bytes,bytearray)) or len(inputs)>4096 or len(raw)!=len(inputs)*224:
        raise ValueError('Selector coverage ABI/count differs')
    rows=list(struct.iter_unpack('<28Q',raw))
    cbvs={tuple(row[:6]):row for row in cbv_report['rows']}
    for index,(row,input_) in enumerate(zip(rows,inputs)):
        if row[:3]!=(index,*input_[:2]) or row[1]>=len(sources) or row[3] not in (0,1):
            raise ValueError('Coverage borrowed another source/input occurrence')
        d=row[4:]
        if not row[3]:
            if any(d):raise ValueError('Failed query acquired cache facts')
            continue
        cbv=cbvs.get(tuple(input_[:6]))
        if cbv is None or d[0]!=0x47434F5630303031 or d[1]!=cbv[12] or not d[2] or d[3] not in (0,1):
            raise ValueError('Coverage current held resource/version differs')
        if (d[6],d[7])!=(cbv[10]-cbv[13],input_[6]) or not 0<d[7]<=65536 or d[4]>8704 or d[5]>2*1024*1024:
            raise ValueError('Coverage resource span/budget differs')
        if d[8]>d[4] or d[9]>d[8] or d[10]>d[7] or d[19]>8 or d[23]>d[2]:
            raise ValueError('Coverage counters/operation exceed current bounds')
        if d[10]==d[7]:
            if d[11]!=2**64-1:raise ValueError('Full coverage concealed a hole')
        elif not d[6]<=d[11]<d[6]+d[7]:
            raise ValueError('Unknown coverage has no exact first hole')
        if not d[3] and any(d[k] for k in (4,5,8,9,10,12,13,14,15,16,17,18,19,20,21,22,23)):
            raise ValueError('Missing cache owner acquired history')
        if bool(d[4])!=(bool(d[12]) and bool(d[13])) or d[12]>d[13]:
            raise ValueError('Present sequence bounds differ from current ranges')
        if input_[7] and not d[9]:raise ValueError('Captured candidate had no covering current copy')
    return dict(verified=True,rows=[list(r) for r in rows],queries=sum(r[3] for r in rows),
                singleCovered=sum(bool(r[3] and r[13]) for r in rows),
                uncovered=sum(bool(r[3] and not r[14]) for r in rows),**_no_permission())
