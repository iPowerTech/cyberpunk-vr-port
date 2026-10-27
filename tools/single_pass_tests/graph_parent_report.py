"""Join exact native work-context parent receipts to the immutable source graph."""
import struct
from collections import Counter

ROW_BYTES=80
FIELDS=('scope','context','manager','fingerprint','object','work','name','handle','ordinal','viewIndex','slot','status','stage')


def name_hash(value):
    result=14695981039346656037
    for byte in value.encode('ascii'):result=((result^byte)*1099511628211)&(2**64-1)
    return result


NAMES={name_hash(name):name for name in ('DepthPrepass','GBuffer_Solid','GBuffer_Discard','GBufferVelocity_Solid',
       'GBufferVelocity_Discard','DeferredDecals','Hair_Opaque','Lighting','PostGBufferOverrides')}


def fingerprint(nodes_raw,edges_raw,base):
    result=14695981039346656037
    def append(data):
        nonlocal result
        for byte in data:result=((result^byte)*1099511628211)&(2**64-1)
    def number(value):append(struct.pack('<Q',value))
    number(base);number(0);number(len(nodes_raw)//104);number(len(edges_raw)//32)
    for handle,order,raw,vtable,work,definition in struct.iter_unpack('<II48sQQ32s',nodes_raw):
        number(handle);number(order);append(raw);number(vtable);number(work);append(definition)
    for handle,order,raw in struct.iter_unpack('<II24s',edges_raw):number(handle);number(order);append(raw)
    return result


def analyze(raw,graph,events,expected_fingerprint):
    if len(raw)%ROW_BYTES or len(raw)//ROW_BYTES>8192:raise ValueError('Invalid graph-parent ABI/count')
    nodes={n['id']:n for n in graph['nodes']}
    scopes={}
    for event in events:
        if event['kind'] in (13,14):
            value=(event['frame'],event['side'],event['stage'],event['context'])
            if event['scope'] in scopes and scopes[event['scope']]!=value:raise ValueError('Scope parent context changed')
            scopes[event['scope']]=value
    rows=[];seen=set();unknown=[]
    for values in struct.iter_unpack('<7Q6I',raw):
        row=dict(zip(FIELDS,values))
        if not row['scope'] or row['scope'] in seen or row['status']>4:raise ValueError('Duplicate/invalid parent receipt')
        seen.add(row['scope'])
        if row['scope'] not in scopes:continue  # A recorded CPU scope may emit no draw.
        frame,side,stage,context=scopes[row['scope']]
        row.update(frame=frame,side=side,passName=NAMES.get(row['name'],hex(row['name'])))
        if row['context']!=context or row['stage']!=stage:raise ValueError('Receipt does not identify its actual draw scope')
        if row['status']:
            unknown.append(dict(scope=row['scope'],stage=stage,side=side,frame=frame,status=row['status']))
        else:
            node=nodes.get(row['handle']&0xDFFFFFFF)
            if (not node or row['fingerprint']!=expected_fingerprint or row['ordinal']!=node['ordinal'] or
                row['viewIndex']!=side or node['view']!=side or row['slot']!=node['phaseOrder'][1] or
                row['object']!=int(node['object'],16) or row['work']!=int(node['work'],16) or
                row['name']!=node['definitionWords'][6]|node['definitionWords'][7]<<32):
                raise ValueError('Native parent receipt differs from the source graph')
        rows.append(row)
    complete=all(any(r['frame']==1 and r['side']==side and r['stage']==stage and not r['status'] for r in rows)
                 for side in (0,1) for stage in (3,6,7,9))
    return dict(rows=rows,unknown=unknown,completeGeometryParents=complete,
                sourceFingerprint=hex(expected_fingerprint),statuses=dict(Counter(r['status'] for r in rows)),
                mappings=[dict(side=side,stage=stage,parents=sorted({r['handle']&0xDFFFFFFF for r in rows if r['frame']==1 and r['side']==side and r['stage']==stage and not r['status']}),
                               passes=sorted({r['passName'] for r in rows if r['frame']==1 and r['side']==side and r['stage']==stage and not r['status']}))
                          for side in (0,1) for stage in (3,6,7,9)],
                limitation='Source-bound graph parent identity only; no resource lifetime or scheduling mutation permission.')


def frontier(stats,roles,cuts,adds,expected_fingerprint):
    if tuple(map(len,(stats,roles,cuts,adds)))!=(16,22,6,6):raise ValueError('Frontier ABI mismatch')
    if stats[0]!=1 or stats[1] or stats[2] or stats[3]!=expected_fingerprint or stats[13:15]!=[3,3] or stats[15]!=255:
        raise ValueError('Native whole-pass topology candidate was not source-bound and non-executable')
    return dict(state='needs-cpu-and-resource-compile',executionReady=False,fingerprint=hex(stats[3]),
                counts=dict(nodes=stats[4],originalCpu=stats[5],originalOrder=stats[6],proposedCpu=stats[7],proposedOrder=stats[8],
                            cpuBlocker=stats[9],orderBlocker=stats[10],cpuClosure=stats[11],cpuIncoming=stats[12]),
                roles=[dict(zip(('collector','depth','solidReady','discard','postGBuffer','lighting','final2D'),roles[eye*11:eye*11+7]),solid=roles[eye*11+7:eye*11+11]) for eye in (0,1)],
                cuts=list(zip(cuts[::2],cuts[1::2])),adds=list(zip(adds[::2],adds[1::2])))
