"""Validate pre-queue native definition provenance; no ownership admission."""
import struct
from collections import Counter
from graph_parent_report import fingerprint

MAX_OWNERS,MAX_ENTRIES,MAX_NODES=128,8192,8192


def validate(stats, owners_raw, mappings_raw, entries_raw, nodes_raw, edges_raw, image_base, jobs, epoch, ticket):
    if (len(stats)!=16 or any(type(v)is not int or not 0<=v<2**64 for v in stats) or stats[0]!=3 or stats[14:]!=[1,0]
            or stats[13]!=0x219e09 or stats[11]!=epoch or not epoch or not stats[12]):
        raise ValueError('Incomplete pre-queue definition source receipt')
    if not 0<stats[1]<=MAX_OWNERS or stats[2]>MAX_NODES or stats[3]>MAX_ENTRIES or stats[4]>stats[3]:
        raise ValueError('Definition source receipt exceeds its bounded ABI')
    if len(owners_raw)!=stats[1]*64 or len(mappings_raw)!=stats[2]*24 or len(entries_raw)!=stats[3]*8 or len(nodes_raw)%104:
        raise ValueError('Definition source binary count/ABI mismatch')
    if fingerprint(nodes_raw,edges_raw,image_base)!=stats[5] or len(jobs)!=8 or jobs[0]!=3 or jobs[2:4]!=stats[6:8] or jobs[7]!=stats[5]:
        raise ValueError('Definition source differs from completed graph jobs')
    if (len(ticket)!=8 or any(type(v)is not int or not 0<=v<2**64 for v in ticket) or not ticket[0] or not ticket[1]
            or ticket[2:6]!=[stats[6],stats[7],stats[11],stats[12]] or ticket[6:]!=[1,1]):
        raise ValueError('Native constructor/queue ticket is incomplete or changed')
    cache,slot=stats[8:10]
    if not cache or slot<cache or (slot-cache)%0x90 or (slot-cache)//0x90>=4 or stats[7]!=slot+0x10:
        raise ValueError('Definition source is not a native cache slot')
    entries=[v[0] for v in struct.iter_unpack('<Q',entries_raw)]
    if sum(v==0 for v in entries)!=stats[4]:raise ValueError('Synthetic definition slot count differs')
    owners,known,headers=[],{},set()
    next_entry=0
    for row in struct.iter_unpack('<6Q4I',owners_raw):
        graph,header,data,allocator,generation,start,capacity,count,in_flight,kind=row
        if not graph or header!=graph+0x70 or header in headers or start!=next_entry or not data or not allocator or count>capacity or capacity>MAX_ENTRIES or count>len(entries)-start or in_flight:
            raise ValueError('Invalid native definition owner row')
        definitions=entries[start:start+count]
        if any(definitions) and (not generation or kind!=1):raise ValueError('Native definition generation was not observed')
        if not capacity and (count or data!=allocator):raise ValueError('Invalid native allocator sentinel')
        headers.add(header);next_entry+=count
        owner=dict(graph=hex(graph),header=hex(header),data=hex(data),allocator=hex(allocator),epoch=generation,capacity=capacity,
                   count=count,definitions=definitions)
        for index,value in enumerate(definitions):
            if not value:continue
            if value&7 or value in known:raise ValueError('Invalid/duplicate unique definition pointer')
            known[value]=(len(owners),index)
        owners.append(owner)
    if next_entry!=len(entries):raise ValueError('Unclaimed definition owner entries')
    objects={}
    for handle,order,raw,vtable,work,definition in struct.iter_unpack('<II48sQQ32s',nodes_raw):
        obj=struct.unpack_from('<Q',raw,16)[0]
        if obj:objects[handle]=obj
    mapped,output={},[]
    for handle,owner,index,obj in struct.iter_unpack('<3I4xQ',mappings_raw):
        if handle in mapped or objects.get(handle)!=obj or known.get(obj)!=(owner,index):
            raise ValueError('Graph node definition has no exact native owner')
        mapped[handle]=obj;output.append(dict(nodeHandle=handle,owner=owner,index=index,object=hex(obj)))
    if mapped!=objects:raise ValueError('Native definition mapping is incomplete')
    counts=Counter(mapped.values())
    return dict(owners=owners,mappings=output,epoch=epoch,consumerId=ticket[0],rootGroup=hex(ticket[1]),fingerprint=hex(stats[5]),mappedNodes=len(mapped),uniqueMappedDefinitions=len(counts),
                sharedMappedDefinitions=sum(n>1 for n in counts.values()),definitionSlots=len(entries),syntheticNullSlots=stats[4],
                allPriorConsumersAccounted=False,definitionOwnershipProved=False,nativeExclusiveProved=False,executionPermitted=False)
