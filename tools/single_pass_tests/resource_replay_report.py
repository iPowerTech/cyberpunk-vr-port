"""Validate selected owned-reference resource grammar/cursor closure, not access or alias permission."""
import struct


def require(condition,message):
    if not condition:raise ValueError(message)


def validate(stats,proof,raw,execution):
    require(len(stats)==16 and len(proof)==8 and len(execution)==24 and
            all(type(v) is int and 0<=v<1<<64 for v in (*stats,*proof,*execution)),'Invalid resource replay ABI')
    require(stats[0]==3 and stats[12:]==[0,0,0,0],'Native resource grammar/cursor replay did not complete: '+str(stats))
    require(stats[1:4]==execution[1:4] and stats[5:7]==[execution[5],execution[4]],'Resource replay selected a different native consumer/graph')
    require(stats[4]>0 and stats[7]>0 and stats[7]==stats[8],'Resource declarations were not completely replayed')
    require(proof[:3]==[stats[1],1,1] and proof[4:7]==[*stats[2:4],stats[4]] and proof[7]==0,'Resource rows lack compiler-return and real terminal stability')
    require(isinstance(raw,bytes) and len(raw)%32==0 and len(raw)//32==proof[3]<=220,'Invalid resource replay node-row ABI/count')
    rows=list(struct.iter_unpack('<8I',raw));handles=set();slots=set();ordinals=set();total=entered=finished=0
    for handle,ordinal,slot,view,declared,cursor,starts,ends in rows:
        require(handle<65536 and handle not in handles and ordinal<len(rows) and ordinal not in ordinals and
                slot<220 and slot not in slots and view<=1 and declared<=500 and cursor==declared and starts==ends,
                'Resource replay has stale node identity or incomplete per-slot cursor')
        handles.add(handle);ordinals.add(ordinal);slots.add(slot);total+=declared;entered+=starts>0;finished+=ends>0
    require(total==stats[7] and [entered,finished]==stats[10:12],'Resource replay aggregate differs from actual node counters')
    return dict(stats=stats,proof=proof,nodes=len(rows),commands=total,requestDecisionDifferences=stats[9],
                nativeCallsPreserved=True,accessDirectionsProved=False,externalAliasingProved=False,executionPermission=False)
