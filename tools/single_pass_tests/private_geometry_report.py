"""Join original-position IA copies and completed compact instance views."""
import struct
from source_cbv_report import _source_rows,_no_permission


def analyze(raw,source_report,ia,gpu,overlay):
    _,sources=_source_rows(source_report)
    if any(value.get('verified') is not True for value in (ia,gpu,overlay)) or not isinstance(raw,(bytes,bytearray)) or len(raw)!=len(sources)*128:
        raise ValueError('Private geometry has no complete owned input reports')
    rows=list(struct.iter_unpack('<16Q',raw));versions={r[0]:r for r in overlay['rows']}
    if len(ia['commands'])!=len(sources) or len(ia['selections'])!=len(sources) or len(gpu['rows'])!=len(sources):
        raise ValueError('Private geometry source occurrence sets differ')
    for ordinal,(row,source,copy,selected,probe) in enumerate(zip(rows,sources,ia['commands'],ia['selections'],gpu['rows'])):
        if row[:2]!=(ordinal,source[1]) or row[2] not in (0,1) or row[3:7]!=(source[19],source[6],source[7],ordinal+1) or any(row[14:]):
            raise ValueError('Private geometry scope/mask differs or acquired execution')
        if selected[2] and not probe[2]:raise ValueError('Separate slot7 has no original-position GPU probe')
        version=versions.get(ordinal)
        eligible=bool(copy[6] and copy[14] and copy[15] and not copy[7] and (not selected[2] or version is not None))
        if bool(row[2])!=eligible:raise ValueError('Private geometry completeness differs from owned domains')
        if not row[2]:
            if any(row[7:14]):raise ValueError('Incomplete private geometry acquired owners')
            continue
        if row[7]!=copy[13] or not row[7] or row[8]!=copy[8] or row[12:14]!=(source[24],source[21]):
            raise ValueError('Private geometry copy receipt/draw coordinates differ')
        if selected[2]:
            if not version or probe[5]!=1 or row[9:12]!=tuple(version[12:15]) or version[18:21]!=[1,1,1]:
                raise ValueError('Private geometry borrowed an unfinished compact instance view')
        elif any(row[9:12]):raise ValueError('Whole IA copy acquired an unrelated compact view')
    return dict(verified=True,rows=[list(r) for r in rows],ready=sum(r[2] for r in rows),sources=len(rows),
                compactInstanceJoins=sum(r[2] and bool(r[9]) for r in rows),nativeDrawsRecorded=0,
                wholeInputEquivalenceProved=False,**_no_permission())
