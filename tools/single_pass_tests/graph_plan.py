"""Decode the owned native dependency graph without editing the game's graph."""
import struct
from collections import Counter,defaultdict,deque


def decode_graph(nodes_raw,edges_raw,base):
    if len(nodes_raw)%104 or len(edges_raw)%32:raise ValueError('Invalid native graph ABI')
    nodes=[];edges=[]
    for handle,order,raw,vtable,work,definition in struct.iter_unpack('<II48sQQ32s',nodes_raw):
        ordinal,unknown,kind,key,object_,view,order_word,*links=struct.unpack('<4IQ6I',raw)
        nodes.append(dict(id=handle&0xDFFFFFFF,handle=handle,order=order,ordinal=ordinal,kind=kind,key=key,
                          object=hex(object_),view=view,phaseOrder=[order_word&255,(order_word>>8)&255],orderWord=order_word,
                          links=links,vtable=hex(vtable),work=hex(work),
                          workRva=hex(work-base) if base<=work<base+0x10000000 else None,
                          definitionWords=list(struct.unpack('<8I',definition))))
    for handle,order,raw in struct.iter_unpack('<II24s',edges_raw):
        ordinal,phase,source,target,next_source,next_target=struct.unpack('<6I',raw)
        edges.append(dict(id=handle&0xBFFFFFFF,handle=handle,order=order,ordinal=ordinal,phase=phase,
                          source=source&0xDFFFFFFF,target=target&0xDFFFFFFF,sourceRaw=source,targetRaw=target,
                          next=[next_source,next_target]))
    if len({n['id'] for n in nodes})!=len(nodes) or len({e['id'] for e in edges})!=len(edges):
        raise ValueError('Duplicate graph handles')
    if any(n['order']!=i for i,n in enumerate(nodes)) or any(e['order']!=i for i,e in enumerate(edges)):
        raise ValueError('Invalid graph publication order')
    known={n['id'] for n in nodes}
    invalid=[e['id'] for e in edges if e['source'] not in known or e['target'] not in known]
    summary=dict(nodeKinds=dict(Counter(n['kind'] for n in nodes)),views=dict(Counter(n['view'] for n in nodes)),
                 edgePhases=dict(Counter(e['phase'] for e in edges)),invalidEndpointEdges=invalid,
                 culling=[n for n in nodes if n['workRva']=='0xb2befc'],
                 sceneDrivers=[n for n in nodes if n['workRva']=='0x1ec1d0'])
    return dict(nodes=nodes,edges=edges,summary=summary)


def dependency_path(graph,source,target,phase):
    following=defaultdict(list)
    for edge in graph['edges']:
        if edge['phase']==phase:following[edge['source']].append(edge['target'])
    previous={source:None};pending=deque([source])
    while pending:
        node=pending.popleft()
        if node==target:
            path=[]
            while node is not None:path.append(node);node=previous[node]
            return path[::-1]
        for child in following[node]:
            if child not in previous:previous[child]=node;pending.append(child)
    return []
