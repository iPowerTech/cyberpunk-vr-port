"""Convert user-confirmed native left/right ladder snapshots to a named grip profile.

Inputs are plugin-owned FK publications (VRIK disabled), never a reused engine
pose pointer. Both hands are recorded separately; no mirrored approximation.
"""
import argparse, json, math, struct
from pathlib import Path

def mul(a,b):
    x,y,z,w=a;X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)
def inv(q): return (-q[0],-q[1],-q[2],q[3])
def norm(q):
    n=math.sqrt(sum(x*x for x in q));return tuple(x/n for x in q)
def rotate(q,v): return mul(mul(q,(*v,0)),inv(q))[:3]
def numbers(v):
    values=[]
    for x in v:
        value=f'{x:.9g}'
        if '.' not in value and 'e' not in value:value+='.0'
        values.append(value+'f')
    return '{'+','.join(values)+'}'

def build(capture,root):
    names={int(s.split('\t')[0]):s.split('\t')[1] for s in (capture/'player_names.txt').read_text().splitlines() if '\t' in s}
    ids={s:i for i,s in names.items()}
    parents=list(x[0] for x in struct.iter_unpack('<h',(capture/'g_VRBoneParent.bin').read_bytes()))
    sides=[]
    fixture=root/'tools/vrik_tests/fixtures/ladder'
    for side,label in enumerate(('left','right')):
        prefix=label.title();hand=ids[prefix+'Hand']
        positions=list(struct.iter_unpack('<3f',(capture/f'g_VRFKSnapPos-{label}.bin').read_bytes()))
        rotations=[norm(q) for q in struct.iter_unpack('<4f',(capture/f'g_VRFKSnapRot-{label}.bin').read_bytes())]
        local=[]
        for i,p in enumerate(parents):
            q=norm(mul(inv(rotations[p]),rotations[i])) if p>=0 else rotations[i]
            v=rotate(inv(rotations[p]),[positions[i][k]-positions[p][k] for k in range(3)]) if p>=0 else positions[i]
            local.append((*v,1,*q,1,1,1,0))
        (fixture/f'side-grip-{label}.bin').write_bytes(b''.join(struct.pack('<12f',*v) for v in local))
        # Contact height is the centre of the four MCP knuckles. The measured
        # rail front surface is model Y=.475, with rails at X=+/- .30 metres.
        height=sum(positions[ids[prefix+'Hand'+finger+'1']][2] for finger in ('Index','Middle','Ring','Pinky'))/4
        contact=(-.3 if side==0 else .3,.475,height)
        offset=rotate(inv(rotations[hand]),[contact[k]-positions[hand][k] for k in range(3)])
        fingers=[]
        for i,name in names.items():
            if i>=len(parents) or not name.startswith(prefix) or 'Hand' not in name or '_' in name or name in (prefix+'Hand',prefix+'HandEnd'):continue
            finger=next((k for k,f in enumerate(('Thumb','Index','Middle','Ring','Pinky')) if f in name),None)
            if finger is not None:fingers.append({'name':name[len(prefix):],'group':finger,'rotation':local[i][4:8]})
        assert len(fingers)==19
        sides.append({'wrist':rotations[hand],'contact':offset,'fingers':fingers,'wristModel':positions[hand],'contactModel':contact})
    # The native entity basis matched [ladder-right, -normal, up] in both takes.
    header=['#pragma once','// Native side grips captured in PID4064 with VRIK off, 2026-09-22.',
            '// See tools/vrik_tests/fixtures/ladder/grip-provenance.json.',
            'namespace cvr::ladder::profile {',
            'struct Hand { float rotation[4],contact[3]; };',
            'inline constexpr Hand hands[2]={']
    header += ['    {'+numbers(s['wrist'])+','+numbers(s['contact'])+'},' for s in sides]
    header += ['};','struct Finger { const char* suffix; int group; float rotation[2][4]; };','inline constexpr Finger fingers[]={']
    for a,b in zip(sides[0]['fingers'],sides[1]['fingers']):
        assert a['name']==b['name']
        header.append(f'    {{"{a["name"]}",{a["group"]},{{{numbers(a["rotation"])},{numbers(b["rotation"])}}}}},')
    header+=['};','}']
    (root/'include/Anim/LadderGripProfile.hpp').write_text('\n'.join(header)+'\n',encoding='utf-8')
    report={'pid':4064,'dll':'bc823ce1243609f4df60700f12505a801807a36766c98427730d3b1e82cabb3d',
        'vrik':0,'capture':'separate user-confirmed native side grips; FK flag restored to zero',
        'localFixture':'reconstructed from model FK; unit scale, native local translations retained',
        'basis':'entity axes equal ladder right/-normal/up; front rail surface X=+/-0.30,Y=0.475',
        'hands':sides}
    (fixture/'grip-provenance.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps([{k:s[k] for k in ('wristModel','contactModel','contact','wrist')} for s in sides],indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2])
    a=p.parse_args();build(a.capture,a.root)
