"""Describe paired native target lifetimes and submission order, without authorizing reuse."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import struct

TARGET_BYTES=464
VIEW_FIELDS=('identity','resource','width','height','format','viewFormat','resourceFlags','viewFlags','reserved')


def decode(draws,targets):
    if len(draws)%1096 or len(targets)!=len(draws)//1096*TARGET_BYTES:raise ValueError('Pass-target ABI/count mismatch')
    result=[]
    for index in range(len(draws)//1096):
        offset=index*1096;sequence,qpc,command,pipeline,root=struct.unpack_from('<5Q',draws,offset)
        frame,node,side=struct.unpack_from('<3I',draws,offset+40)
        seq,scope,stage,pass_frame,status,colors=struct.unpack_from('<2Q4I',targets,index*TARGET_BYTES)
        if seq!=sequence or colors>8 or status not in (1,2,3,4):raise ValueError('Incomplete target publication')
        views=[dict(zip(VIEW_FIELDS,struct.unpack_from('<3Q6I',targets,index*TARGET_BYTES+32+i*48))) for i in range(9)]
        if any(v['reserved'] for v in views):raise ValueError('Unknown target metadata version')
        result.append(dict(sequence=sequence,qpc=qpc,command=command,pipeline=pipeline,root=root,frame=pass_frame,
                           nativeFrame=frame,node=node,side=side,scope=scope,stage=stage,status=status,colors=colors,views=views))
    return result


def relation(producer,consumer):
    a,b=producer.get('submission'),consumer.get('submission')
    if not a or not b:return 'unsubmitted-or-ambiguous'
    if a['queue']!=b['queue']:return 'cross-queue-unproved'
    if a['batch']!=b['batch']:
        if not a['serialized'] or not b['serialized']:return 'unserialized-observation'
        return 'earlier-submission' if a['batch']<b['batch'] else 'later-submission'
    if producer['list']==consumer['list'] and a['generation']==b['generation']:
        return 'earlier-in-list-needs-barrier' if producer['lastQpc']<consumer['firstQpc'] else 'later-or-overlapping-in-list'
    return 'same-submit-may-overlap'


def analyze(preparation,draws,targets):
    rows=decode(draws,targets);events=preparation['records'];begins={};ends={};resets=defaultdict(list);submits=defaultdict(list)
    for event in events:
        key=(event['scope'],event['output'])
        if event['kind'] in (13,14):
            container=begins if event['kind']==13 else ends
            if key in container:raise ValueError('Duplicate draw scope boundary')
            container[key]=event
        elif event['kind']==16:resets[event['context']].append(event)
        elif event['kind']==15:submits[event['context']].append(event)
    grouped=defaultdict(list);unscoped=[];identities={}
    for row in rows:
        if row['node']!=0x23a938 or row['side'] not in (0,1):raise ValueError('Unexpected native draw node/side')
        if not row['scope']:unscoped.append(row['sequence']);continue
        grouped[(row['scope'],row['command'])].append(row)
        for view in row['views']:
            if not view['identity']:continue
            description={k:v for k,v in view.items() if k not in ('viewFormat','viewFlags','reserved')}
            old=identities.setdefault(view['identity'],description)
            if old!=description:raise ValueError('A resource identity changed description or address')
    groups=[]
    for key,values in sorted(grouped.items()):
        start,end=begins.get(key),ends.get(key)
        if not start or not end:raise ValueError('Missing native pass boundary')
        values.sort(key=lambda row:row['qpc']);first,last=values[0],values[-1]
        if (end['qpc']<last['qpc'] or start['qpc']<first['qpc'] or end['flags0']<len(values) or
                any((r['frame'],r['side'],r['stage'])!=(start['frame'],start['side'],start['stage']) for r in values)):
            raise ValueError('Draws do not belong to their native pass')
        signatures={json.dumps(dict(colors=r['colors'],views=r['views']),sort_keys=True) for r in values if r['status']==1}
        complete=(len(values)==end['flags0'] and all(r['status']==1 for r in values) and len(signatures)==1)
        group=dict(scope=key[0],list=key[1],frame=first['frame'],side=first['side'],stage=first['stage'],
                   firstQpc=first['qpc'],lastQpc=end['qpc'],draws=len(values),nativeDraws=end['flags0'],
                   knownTargets=sum(r['status']==1 for r in values),targetVariants=len(signatures),complete=complete,
                   colors=first['colors'],views=first['views'] if complete else None,submission=None)
        reset_before=[e for e in resets[key[1]] if e['qpc']<=first['qpc']]
        reset_after=[e for e in resets[key[1]] if e['qpc']>first['qpc']]
        boundary=min((e['qpc'] for e in reset_after),default=2**64-1)
        if boundary<=end['qpc']:raise ValueError('Native list reset inside a captured pass')
        candidates=[e for e in submits[key[1]] if end['qpc']<=e['qpc']<boundary]
        if len(candidates)==1:
            submit=candidates[0]
            group['submission']=dict(queue=submit['input'],batch=submit['scope'],position=submit['flags0'],
                serialized=submit['reserved']==1,generation=max((e['sequence'] for e in reset_before),default=0))
        groups.append(group)
    depth=[g for g in groups if g['stage']==3 and g['complete'] and not g['colors'] and g['views'][8]['identity']]
    gbuffer=[g for g in groups if g['complete'] and g['colors'] in (3,4) and
             [g['views'][i]['viewFormat'] for i in range(3)]==[24,24,28] and
             (g['colors']==3 or g['views'][3]['viewFormat']==34) and g['views'][8]['identity']]
    consumers=[]
    for color in gbuffer:
        peer=[g for g in depth if g['frame']==color['frame'] and g['side']!=color['side']]
        relations=[dict(scope=g['scope'],depth=g['views'][8]['identity'],order=relation(g,color)) for g in peer]
        consumers.append(dict(scope=color['scope'],frame=color['frame'],side=color['side'],stage=color['stage'],colors=color['colors'],
            depth=color['views'][8]['identity'],oppositeDepth=relations,
            oppositeDepthEarlier=bool(relations) and all(r['order']=='earlier-submission' for r in relations)))
    reuse=defaultdict(list)
    for group in groups:
        if group['complete']:
            for slot,view in enumerate(group['views']):
                if view['identity']:reuse[view['identity']].append(dict(scope=group['scope'],side=group['side'],stage=group['stage'],slot=slot))
    captured_frames={row['frame'] for row in rows}
    missing=[dict(scope=scope,list=command,stage=row['stage'],side=row['side']) for (scope,command),row in begins.items()
             if row['frame'] in captured_frames and (scope,command) not in grouped]
    return dict(draws=len(rows),unscopedDraws=unscoped,uncapturedScopes=missing,groups=groups,resources=list(identities.values()),gbufferConsumers=consumers,
                sharedAcrossEyes={str(key):value for key,value in reuse.items() if len({r['side'] for r in value})>1},
                depthScopes=len(depth),gbufferScopes=len(gbuffer),
                limitation='Bound-target identities and observed native submission order only. CPU recording order, same-batch list order and COM ownership do not prove GPU completion, resource contents or lack of aliasing. No game work was skipped.')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    preparation=json.loads(a.capture.read_text());stem=a.capture.with_suffix('')
    result=analyze(preparation,Path(str(stem)+'-early-draws.bin').read_bytes(),Path(str(stem)+'-pass-targets.bin').read_bytes())
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('groups','resources','sharedAcrossEyes')},indent=2))


if __name__=='__main__':main()
