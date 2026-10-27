"""Validate native whole-discard sorting and CPU context-prefix readiness, not execution safety."""
import struct
from graph_parent_report import fingerprint
from resource_order_report import graph as decode_graph


def _require(condition, reason):
    if not condition:raise ValueError(reason)


def _prefix(nodes, cpu, slots, base):
    kinds={}
    for node in nodes:
        table=int(node['vtable'],16)-base if int(node['vtable'],16) else 0
        work=int(node['work'],16)-base if int(node['work'],16) else 0
        if table==0x2b3a008 and work==0x1ec1d0:kinds[node['id']]=2
        elif table==0x2b3a038 and work==0x58da9c:kinds[node['id']]=3
    following={n['id']:[] for n in nodes}
    for a,b in cpu:following[a].append(b)
    missing=[];required=0
    for producer,kind in kinds.items():
        ready={producer};pending=[producer]
        for node in pending:
            for child in following[node]:
                if child not in ready:ready.add(child);pending.append(child)
        for consumer,consumer_kind in kinds.items():
            if consumer_kind!=3 or slots[producer]>=slots[consumer]:continue
            required+=1
            if consumer not in ready:missing.append((producer,consumer,slots[producer],slots[consumer],kind))
    return sum(k==2 for k in kinds.values()),sum(k==3 for k in kinds.values()),required,missing


def shared_owners(stats, raw, source_nodes, source_edges, owned_nodes, owned_edges, base):
    _require(len(stats)==8 and all(type(v) is int and 0<=v<1<<64 for v in stats) and stats[0]==3 and stats[5]==1 and not stats[7],
             'Invalid shared-object CPU stats')
    _require(isinstance(raw,bytes) and len(raw)%32==0 and len(raw)//32==stats[6]<=32768,'Invalid shared-object CPU row ABI/count')
    source,_,_=decode_graph(source_nodes,source_edges,base);owned,_,_=decode_graph(owned_nodes,owned_edges,base)
    _require(stats[1]==fingerprint(source_nodes,source_edges,base),'Shared-object source fingerprint changed')
    relations=lambda parsed:{(e['source'],e['target']) for e in parsed['edges'] if not e['phase']}
    before,after=relations(source),relations(owned)
    rows=list(struct.iter_unpack('<4Q',raw));states={(a,b):kind for _,a,b,kind in rows}
    _require(len(states)==len(rows) and all(kind in (0,1,2) for kind in states.values()),'Invalid shared-object row classification')
    baseline=after-{(a,b) for _,a,b,kind in rows if kind==1}
    def reachable(edges,start,end):
        ready={start};pending=[start]
        for at in pending:
            if at==end:return True
            for a,b in edges:
                if a==at and b not in ready:ready.add(b);pending.append(b)
        return False
    expected=[];restored=reversed_=0
    nodes={n['id']:n for n in source['nodes']}
    for a in source['nodes']:
        object_=int(a['object'],16)
        if not object_:continue
        for b in source['nodes']:
            if a['id']==b['id'] or b['object']!=a['object'] or not reachable(before,a['id'],b['id']):continue
            _require((a['vtable'],a['work'])==(b['vtable'],b['work']),'Shared object has conflicting native types')
            if reachable(after,a['id'],b['id']):
                kind=states.get((a['id'],b['id']))
                _require(kind in (0,1) and (kind==0 or ((a['id'],b['id']) in after and not reachable(baseline,a['id'],b['id']))),
                         'Restored shared-object relation was not an actual missing CPU order')
                restored+=kind
            else:
                from graph_parent_report import name_hash
                _require(a['view']==0 and b['view']==1 and a['definitionWords'][6]|a['definitionWords'][7]<<32==name_hash('GBuffer_Discard') and
                         b['definitionWords'][6]|b['definitionWords'][7]<<32==name_hash('GBuffer_Discard') and reachable(after,b['id'],a['id']),
                         'Shared-object ordering was lost outside intentional serialized discard reversal')
                kind=2;reversed_+=1
            expected.append((object_,a['id'],b['id'],kind))
    _require(sorted(struct.iter_unpack('<4Q',raw))==sorted(expected) and stats[2:5]==[len(expected),restored,reversed_],
             'Missing or stale shared-object order proof')
    return dict(orderedPairs=len(expected),restored=restored,reversed=reversed_,rows=[list(row) for row in expected],
                descendantReadOnlyProved=False,executionPermitted=False)


def analyze(stats, context_stats, witnesses, source_nodes, source_edges, owned_nodes, owned_edges, base):
    _require(len(stats)==16 and len(context_stats)==24 and all(type(v) is int and 0<=v<1<<64 for v in (*stats,*context_stats)),
             'Invalid whole-pass stats ABI')
    _require(stats[0:2]==[3,1] and not stats[15], 'Whole-pass candidate failed or claimed execution permission')
    _require(isinstance(witnesses,bytes) and len(witnesses)%24==0 and len(witnesses)//24<=4096,'Invalid context witness ABI')
    original,_,_=decode_graph(source_nodes,source_edges,base)
    candidate,_,_=decode_graph(owned_nodes,owned_edges,base)
    _require(stats[2]==fingerprint(source_nodes,source_edges,base),'Whole-pass source fingerprint changed')
    _require(stats[3]==len(original['nodes'])==len(candidate['nodes']),'Whole-pass node count changed')
    left=list(struct.iter_unpack('<II48sQQ32s',source_nodes));right=list(struct.iter_unpack('<II48sQQ32s',owned_nodes))
    for a,b in zip(left,right):
        _require(a[:2]==b[:2] and a[2][:28]==b[2][:28] and a[2][30:32]==b[2][30:32] and a[3:]==b[3:],
                 'Whole-pass changed a native definition or node identity')
    edges=lambda graph,phase:{(e['source'],e['target']) for e in graph['edges'] if e['phase']==phase}
    source_cpu,source_order=edges(original,0),edges(original,1)
    owned_cpu,owned_order=edges(candidate,0),edges(candidate,1)
    _require(stats[4:8]==[len(source_cpu),len(source_order),len(owned_cpu),len(owned_order)],'Whole-pass edge counts changed')
    _require(stats[11:13]==[len(source_cpu-owned_cpu),len(owned_cpu-source_cpu)],'Whole-pass CPU delta changed')
    _require(len(source_order-owned_order)==3 and len(owned_order-source_order)==3,'Whole-pass ordered frontier not three exact changes')
    tail,continuation=stats[13:15]
    _require((tail,continuation) in source_order and (tail,continuation) not in owned_order,'Whole-pass tail was not moved')
    _require(tail in {n['id'] for n in candidate['nodes']} and continuation in {n['id'] for n in candidate['nodes']},'Unknown whole-pass frontier roles')
    scenarios=[(original,source_cpu),(candidate,source_cpu),(candidate,owned_cpu)]
    expected=[];reports=[]
    for i,(parsed,cpu) in enumerate(scenarios):
        slots={n['id']:n['phaseOrder'][1] for n in parsed['nodes']}
        publishers,syncs,required,missing=_prefix(parsed['nodes'],cpu,slots,base)
        row=context_stats[i*8:(i+1)*8]
        _require(row[0]==(6 if missing else 0) and row[2:7]==[publishers,syncs,required,required-len(missing),len(missing)] and row[7]==0,
                 'Native context-prefix report disagrees with CPU dependencies')
        if i==0:_require(row[1]==stats[2],'Source context-prefix fingerprint differs')
        if i==2:_require(row[1]==fingerprint(owned_nodes,owned_edges,base),'Candidate context-prefix fingerprint differs')
        expected.extend((i,*w) for w in missing)
        reports.append(dict(publishers=publishers,synchronizers=syncs,required=required,proved=required-len(missing),missing=len(missing)))
    _require(sorted(struct.iter_unpack('<6I',witnesses))==sorted(expected),'Missing/stale native context witnesses')
    _require(not reports[0]['missing'] and reports[1]['missing']>0 and not reports[2]['missing'],'Whole-pass coupled gate lacks its counterexample or readiness')
    return dict(boundary='GBuffer_Discard',stages=[7,9],stats=stats,contextStats=context_stats,source=reports[0],
                orderOnly=reports[1],coupled=reports[2],executionRedirected=False,resourceAliasingProved=False,
                sharedScenePreparationProved=False,performanceGainProved=False)
