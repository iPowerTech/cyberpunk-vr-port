"""Read emitter-instance preparation state without inferring simulation work from call counts."""
import math
import struct

RECORD=struct.Struct('<6Q8Ii3IfI2f')


def decode(data,lost=0):
    if lost or len(data)%RECORD.size or not data or len(data)>4096*RECORD.size:
        raise ValueError('Incomplete or overflowing particle trace')
    rows=[]
    for index,r in enumerate(RECORD.iter_unpack(data)):
        if r[0]!=index+1 or r[6] not in (0,1) or r[16]!=1 or r[17]>128 or r[13]>=r[17] or not math.isfinite(r[18]):
            raise ValueError('Invalid particle record sequence, layout or instance state')
        if r[6]==0 and r[15] not in (0,1):
            raise ValueError('Missing native onscreen argument')
        rows.append(dict(sequence=r[0],qpc=r[1],id=hex(r[2]),instance=hex(r[3]),state=hex(r[4]),view=hex(r[5]),
            kind='before' if r[6]==0 else 'bounds',thread=r[7],caller=hex(r[8]),node=hex(r[9]),epoch=r[10],stamp=r[11],
            sceneCounter=r[12],instanceIndex=r[13],side=r[14],onscreen=r[15] if r[6]==0 else None,
            instances=r[17],accumulated=r[18],boundsAnchor=r[19],systemDelta=r[20] if r[6]==0 else None,parameterDelta=r[21]))
    starts=[r for r in rows if r['kind']=='before']
    groups={}
    for row in starts:groups.setdefault(row['instance'],[]).append(row)
    repeated=[dict(instance=key,id=values[0]['id'],entries=values) for key,values in groups.items() if len(values)>1]
    return dict(summary=dict(records=len(rows),entrySamples=len(starts),boundsSamples=len(rows)-len(starts),
        instances=len(groups),repeatedInstances=len(repeated),epochs=sorted({r['epoch'] for r in rows}),
        sceneCounters=sorted({r['sceneCounter'] for r in rows}),
        entryAlreadyStamped=sum(r['stamp']==r['epoch'] for r in starts),
        entryAccumulatedPositive=sum(r['accumulated']>0 for r in starts),
        note='Call-entry and pre-publication observations, not counts of executed simulation steps'),repeated=repeated,records=rows)
