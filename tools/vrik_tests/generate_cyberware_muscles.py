"""Extract only artificial-muscle deformation from native, VRIK-off attacks.

No full attack pose/event is replayed. Mantis uses parent-local joint deltas;
Gorilla uses the native muscle thickness channels. Opposite hands are mirrored
through their own reference parent frames, so each VR hand can pulse alone.
"""
import gzip,hashlib,json,math,struct,sys
from pathlib import Path
from capture_cyberware_poses import mul,rotate,inverse,unit

ROOT=Path(__file__).resolve().parents[2];COUNT=16
def fnv(name):
    h=14695981039346656037
    for b in name.encode():h=((h^b)*1099511628211)&((1<<64)-1)
    return h
def load(kind,folder):
    layout=json.loads((folder/'native-attack-layout.json').read_text());n=layout['bones'];stride=12+n*48
    raw=gzip.decompress((folder/'native-attack-poses.bin.gz').read_bytes());samples=[]
    for i in range(len(raw)//stride):
        t,phase=struct.unpack_from('<dI',raw,i*stride)
        model=list(struct.iter_unpack('<12f',raw[i*stride+12:(i+1)*stride]))
        # The mapper buffer is briefly LOCAL before the final native FK.
        if model[23][2]>.5 and model[24][2]>.5:samples.append((t,model))
    assert len(samples)>20
    fixture=Path(__file__).parent/'fixtures'/kind
    names=[s.split('\t',1)[-1] for s in (fixture/'player_names.txt').read_text().splitlines()]
    parent=struct.unpack('<'+'h'*len(names),(fixture/'player_parents.bin').read_bytes())
    ref=list(struct.iter_unpack('<12f',(fixture/'reference_full.bin').read_bytes()));rq=[]
    for i,b in enumerate(ref):rq.append(unit(mul(rq[parent[i]],b[4:8])) if parent[i]>=0 else unit(b[4:8]))
    return layout,samples,names,parent,rq
def local(model,i,parents):
    bone=model[i];p=parents[i];parent=model[p]
    q=unit(bone[4:8]);pq=unit(parent[4:8])
    pos=rotate(inverse(pq),[bone[k]-parent[k] for k in range(3)])
    return pos,unit(mul(inverse(pq),q)),[bone[8+k]/parent[8+k] for k in range(3)]
def mirror_q(q):return [q[0],-q[1],-q[2],q[3]]
def mirror_p(p):return [-p[0],p[1],p[2]]
def float_list(v):
    def one(x):
        s=f'{x:.9g}';return s+('' if '.' in s or 'e' in s else '.0')+'f'
    return '{'+','.join(one(x) for x in v)+'}'
header=['#pragma once','// Generated from native VRIK-off attacks by generate_cyberware_muscles.py.',
        'namespace cvr::cyberware {',f'inline constexpr int MuscleSampleCount={COUNT};',
        'struct MuscleSample {float position[3],rotation[4],scale[3];};',
        'struct MuscleCurve {const char* name;int family;MuscleSample samples[MuscleSampleCount];};',
        'inline constexpr MuscleCurve MuscleCurves[] = {']
fixture_path=Path(__file__).parent/'fixtures/cyberware-muscles.json'
if '--extract' not in sys.argv and fixture_path.exists():
    saved=json.loads(fixture_path.read_text())
    if 'curves' in saved:
        for curve in saved['curves']:
            header.append(f'    {{"{curve["name"]}",{curve["family"]},{{')
            for values in curve['samples']:header.append('        {'+','.join(map(float_list,values))+'},')
            header.append('    }},')
        header+=['};','}']
        (ROOT/'include/Anim/CyberwareMuscles.hpp').write_text('\n'.join(header)+'\n')
        print('Regenerated muscle header from the compact native fixture')
        raise SystemExit
provenance={}
curves=[]
for kind,family,folder,source_side,window,suffixes in [
    ('mantis',2,ROOT/'build/cyberware-wrist-20261010/run2','l_',(2.05,2.95),['forearmLeftMuscle01_JNT','forearmRightMuscle01_JNT','wristSuspensionLeftMuscle01_JNT','wristSuspensionRightMuscle01_JNT']),
    ('gorilla',1,ROOT/'build/cyberware-combat-20261010/combat2','r_',(.84,1.34),['muscle_a','muscle_b'])]:
    layout,samples,names,parents,rq=load(kind,folder)
    provenance[kind]={'capture':str(folder.relative_to(ROOT)),'sha256':hashlib.sha256((folder/'native-attack-poses.bin.gz').read_bytes()).hexdigest(),'source_hand':source_side,'window_seconds':window,'samples':COUNT}
    for side in ('l_','r_'):
        for suffix in suffixes:
            source=source_side+suffix;target=side+suffix;i=layout['names'].index(fnv(source))
            base=local(samples[0][1],i,layout['parents'])
            si=names.index(source);ti=names.index(target)
            source_parent=rq[parents[si]];target_parent=rq[parents[ti]]
            header.append(f'    {{"{target}",{family},{{')
            curve={'name':target,'family':family,'samples':[]};curves.append(curve)
            for k in range(COUNT):
                t=window[0]+(window[1]-window[0])*k/(COUNT-1)
                _,model=min(samples,key=lambda sample:abs(sample[0]-t))
                p,q,scale=local(model,i,layout['parents'])
                dp=[p[a]-base[0][a] for a in range(3)];dq=unit(mul(q,inverse(base[1])))
                ds=[scale[a]/base[2][a] for a in range(3)]
                if family==1:dp=[0,0,0];dq=[0,0,0,1]
                else:ds=[1,1,1]
                if side!=source_side:
                    dp=rotate(inverse(target_parent),mirror_p(rotate(source_parent,dp)))
                    world=mul(mul(source_parent,dq),inverse(source_parent))
                    dq=unit(mul(mul(inverse(target_parent),mirror_q(world)),target_parent))
                if k in (0,COUNT-1):dp=[0,0,0];dq=[0,0,0,1];ds=[1,1,1]
                assert all(math.isfinite(x) for x in dp+dq+ds)
                assert math.sqrt(sum(x*x for x in dp))<.04 and max(ds)<2.1
                curve['samples'].append([dp,dq,ds])
                header.append('        {'+','.join(map(float_list,(dp,dq,ds)))+'},')
            header.append('    }},')
header+=['};','}']
(ROOT/'include/Anim/CyberwareMuscles.hpp').write_text('\n'.join(header)+'\n')
fixture_path.write_text(json.dumps({'provenance':provenance,'curves':curves},indent=2)+'\n')
print('Generated native muscle-only curves:',provenance)
