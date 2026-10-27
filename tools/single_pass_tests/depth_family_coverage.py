"""Correlate actual native depth scopes, PSO families and selected MAIN draws."""
import argparse
from collections import Counter,defaultdict
import hashlib
import json
from pathlib import Path
import struct


def classify(family):
    if family is None:return 'unregistered'
    if family['nativePsBytes']:return 'pixel-shader'
    uses=family['uses']
    cbvs={(u['register'],u['bytes']) for u in uses}
    if (len(uses)==2 and cbvs=={(1,848),(5,112)} and
            all(u['type']==2 and u['visibility']==1 and u['space']==0 and u['count']==1 for u in uses)):
        return 'camera-material-cbv'
    return 'additional-vertex-inputs'


def analyze(preparation,registry,manifest,raw):
    if len(raw)%1096:raise ValueError('Invalid native draw ABI')
    families=manifest['families'];by_pipeline={}
    for record in registry['records']:
        asset=record['asset']
        if not 1<=asset<=len(families):raise ValueError('Invalid one-based native family asset')
        pipeline=int(record['original'],16)
        if pipeline in by_pipeline and by_pipeline[pipeline]!=asset:raise ValueError('Conflicting PSO family')
        by_pipeline[pipeline]=asset
    ends={}
    for row in preparation['records']:
        if row['kind']==14:
            key=(row['scope'],row['output'])
            if key in ends:raise ValueError('Repeated depth scope end')
            ends[key]=row
    scopes=[]
    for row in preparation['records']:
        if row['kind']!=13:continue
        end=ends.get((row['scope'],row['output']))
        if (end is None or end['qpc']<row['qpc'] or any(row[k]!=end[k] for k in ('frame','side','output'))):
            raise ValueError('Incomplete native depth scope')
        scopes.append((row,end))
    selected={source[0] for source in preparation.get('earlyDepthGpu',{}).get('sources',[])}
    draws=[]
    for offset in range(0,len(raw),1096):
        sequence,qpc,command,pipeline,root=struct.unpack_from('<5Q',raw,offset)
        frame,node,side,indices,instances=struct.unpack_from('<5I',raw,offset+40)
        draws.append(dict(sequence=sequence,qpc=qpc,command=command,pipeline=pipeline,node=node,side=side,indices=indices,instances=instances))
    counts=defaultdict(Counter);matched=set();assigned=set();uncaptured=[]
    for start,end in scopes:
        same=[d for d in draws if d['command']==start['output'] and d['side']==start['side'] and d['node']==0x23A938]
        inside=[d for d in same if start['qpc']<=d['qpc']<=end['qpc']]
        if not inside:
            uncaptured.append(dict(frame=start['frame'],side=start['side'],scope=start['scope']))
            continue
        # DrawBegin is emitted by DrawRecorded AFTER the first native draw hook.
        # Recover that one predecessor and require the exact DrawEnd draw count.
        before=[d for d in same if d['qpc']<start['qpc']]
        if not before or len(inside)+1!=end['flags0']:raise ValueError('Incomplete native depth draw capture')
        owned=[max(before,key=lambda d:d['qpc']),*inside]
        for d in owned:
            sequence=d['sequence'];side=d['side']
            if sequence in assigned:raise ValueError('Overlapping native depth draw ownership')
            assigned.add(sequence)
            asset=by_pipeline.get(d['pipeline'],0)
            # Asset.index is assigned pending.size()+1 by NativeStereoFamilies::Load.
            family=families[asset-1] if asset else None
            key=(start['frame'],side,asset,classify(family))
            row=counts[key];row['draws']+=1;row['instances']+=d['instances'];row['indexInvocations']+=d['indices']*d['instances']
            if sequence in selected:
                if side or sequence in matched:raise ValueError('Invalid selected MAIN identity')
                matched.add(sequence);row['selectedMain']+=1
    if matched!=selected:raise ValueError('Selected MAIN draws are outside the captured depth scopes')
    result=[]
    for (frame,side,asset,kind),counts in sorted(counts.items()):
        family=families[asset-1] if asset else None
        result.append(dict(frame=frame,side=side,asset=asset,kind=kind,**counts,
                           nativeVs=family['nativeVs'] if family else None,
                           nativePs=family['nativePs'] if family else None))
    return dict(groups=result,selectedMain=len(matched),uncapturedScopes=uncaptured,
                limitation='Shader-resource classification and actual draw counts, not complete runtime eligibility or GPU timings.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    preparation=json.loads((args.capture/'preparation.json').read_text())
    registry=json.loads((args.capture/'families.json').read_text())
    manifest=args.manifest.read_bytes()
    if registry['manifestSha256']!=hashlib.sha256(manifest).hexdigest():raise ValueError('Family package identity mismatch')
    if (preparation['pid'],preparation['dll'])!=(registry['pid'],registry['dll']):raise ValueError('Capture identity mismatch')
    result=analyze(preparation,registry,json.loads(manifest),(args.capture/'preparation-early-draws.bin').read_bytes())
    args.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
