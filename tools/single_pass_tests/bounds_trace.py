"""Decode bounded native bounds writes and their relation to both input snapshots."""
import collections
import struct

RECORD=struct.Struct('<5Q4Ii3I6f14I')
KINDS=('begin','write_begin','write_end','vrcam_begin','vrcam_end','main_begin','main_end','end')


def decode(data,lost=0,changed_ids=()):
    if lost or len(data)%RECORD.size or not data or len(data)>4096*RECORD.size:
        raise ValueError('Incomplete or overflowing bounds trace')
    rows=[];calls={};markers={}
    for index,r in enumerate(RECORD.iter_unpack(data)):
        if r[0]!=index+1 or r[5]>=len(KINDS) or r[32]>12:
            raise ValueError('Invalid bounds trace sequence, kind or stack')
        row=dict(sequence=r[0],qpc=r[1],id=hex(r[2]),pool=hex(r[3]),span=r[4],kind=KINDS[r[5]],
                 thread=r[6],caller=hex(r[7]),node=hex(r[8]),side=r[9],slot=r[10],key=r[11],
                 valid=bool(r[12]),bounds=list(r[13:19]),stack=[hex(v) for v in r[19:19+r[32]]],result=r[31])
        rows.append(row)
        if r[5]==2:
            raise ValueError('Old completion-span trace cannot be read as an entry-only trace')
        if r[5]==1:
            if not r[4] or r[4] in calls:
                raise ValueError('Duplicate or missing write identity')
            calls[r[4]]=row
        else:
            if r[5] in markers:
                raise ValueError('Repeated bounds trace boundary')
            markers[r[5]]=row
    if set(markers)!={0,3,4,5,6,7} or not all(markers[a]['sequence']<markers[b]['sequence'] for a,b in ((0,3),(3,4),(4,5),(5,6),(6,7))):
        raise ValueError('Missing or unordered eye snapshots')
    between=[];overlapping=[]
    for span,row in calls.items():
        at=row['sequence']
        if markers[4]['sequence']<at<markers[5]['sequence']:
            between.append(row)
        if any(markers[start]['sequence']<at<markers[finish]['sequence'] for start,finish in ((3,4),(5,6))):
            overlapping.append(span)
    changed=set(changed_ids)
    traced={r['id'] for r in between if r['valid']}
    matched=[r for r in between if r['id'] in changed and r['valid']]
    summary=dict(records=len(rows),calls=len(calls),callsBetweenSnapshots=len(between),
                 callsDuringSnapshot=overlapping,changedIdsTraced=sorted(changed & traced),untracedChangedIds=sorted(changed-traced),
                 changedCallers=dict(collections.Counter(r['caller'] for r in matched)),
                 changedNodes=dict(collections.Counter(r['node'] for r in matched)),
                 changedThreads=sorted({r['thread'] for r in matched}),
                 nativeCounters=dict(vrcam=markers[3]['key'],main=markers[5]['key']),
                 note='Function-entry observations only; no completion-time or atomic-snapshot proof')
    return dict(summary=summary,records=rows)
