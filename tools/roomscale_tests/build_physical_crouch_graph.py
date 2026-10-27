"""Keep the physical crouch camera mount after all weapon/locomotion layers.

Input JSON must come from WolvenKit's export of the installed game resources.
All native crouch variables, transitions, physics and leg poses are retained.
The generated JSON is converted with json_to_cr2w, then packed as an archive.
"""
import argparse,copy,json,sys
from pathlib import Path
sys.setrecursionlimit(20000)

def patch(source,output,crouch_spring):
    data=json.loads(source.read_text(encoding='utf-8-sig'));root=data['Data']['RootChunk']
    handles={}
    def scan(v):
        if isinstance(v,dict):
            if 'HandleId' in v:handles[v['HandleId']]=v['Data']
            for x in v.values():scan(x)
        elif isinstance(v,list):
            for x in v:scan(x)
    scan(data);next_id=max(map(int,handles))+1
    def new(value):
        nonlocal next_id
        # WolvenKit's RED converter requires the type discriminator first.
        value={'$type':value['$type'],**{k:v for k,v in value.items() if k!='$type'}}
        h={'HandleId':str(next_id),'Data':value};next_id+=1;return h
    def name(value):return {'$type':'CName','$storage':'string','$value':value}
    def vector(x,y,z):return {'$type':'Vector4','W':1,'X':x,'Y':y,'Z':z}
    variables=root['variables']['Data']
    assert all(v.get('Data',{}).get('name',{}).get('$value')!='vr_physical_crouch_camera'
               for v in variables['floatVariables']), 'Source already has the physical crouch patch'
    f=copy.deepcopy(next(v['Data'] for v in variables['floatVariables'] if v.get('Data',{}).get('name',{}).get('$value')=='crouch'))
    f.update(name=name('vr_physical_crouch_camera'),default=0,value=0,min=0,max=1)
    variables['floatVariables'].append(new(f))
    v=copy.deepcopy(next(v['Data'] for v in variables['vectorVariables'] if v.get('Data',{}).get('name',{}).get('$value')=='debug_stand_camera_position'))
    v.update(name=name('vr_physical_crouch_camera_anchor'),default=vector(0,0,1.6),
             min=vector(-10,-10,-10),max=vector(10,10,10),x=0,y=0,z=1.6,w=1)
    variables['vectorVariables'].append(new(v))
    # Root output is shared by unarmed and armed poses. An edit inside the
    # locomotion subgraph is overwritten by the subsequent weapon layers.
    output_node=handles[root['rootNode']['HandleRefId']]
    if output_node['$type']=='animAnimNode_Root':
        outputs=[n['Data'] for n in output_node['nodes'] if n.get('Data',{}).get('$type')=='animAnimNode_Output']
        assert len(outputs)==1
        output_node=outputs[0]
    assert output_node['$type']=='animAnimNode_Output'
    original=output_node['node']['node']
    original_id=original.get('HandleId',original.get('HandleRefId'))
    assert original_id is not None
    template=next(n for n in handles.values() if n['$type']=='animAnimNode_Blend2')
    common={k:copy.deepcopy(v) for k,v in template.items() if k.startswith('vis') or k in ('id','debugName','poseInfoLogger')}
    inserted=[]
    def node(typ,**fields):
        n=new({'$type':typ,**common,**fields});inserted.append(n);return n
    def link(n):return {'$type':'animFloatLink','node':n}
    def mathop(operation,left,right):
        return node('animAnimNode_FloatMathOp',operationType=operation,firstInputNode=link(left),secondInputNode=link(right))
    active=node('animAnimNode_FloatVariable',variableName=name('vr_physical_crouch_camera'))
    crouch=node('animAnimNode_FloatVariable',variableName=name('crouch'))
    # Pose tags disappear at state exit, before the native pose finishes
    # blending. Use the game's authored crouch spring as a release envelope;
    # the camera stays owned until only a sub-pixel native contribution remains.
    spring_fields={k:copy.deepcopy(v) for k,v in crouch_spring.items() if k!='$type'}
    spring_fields['inputNode']=link(crouch)
    coverage=node('animAnimNode_SpringDamp',**spring_fields)
    gain=node('animAnimNode_FloatConstant',value=10000)
    amplified=mathop('AGMO_Multiply',coverage,gain)
    gate=node('animAnimNode_FloatClamp',min=0,max=1,inputNode=link(amplified))
    weight=mathop('AGMO_Multiply',active,gate)
    anchor=node('animAnimNode_VectorVariable',variableName=name('vr_physical_crouch_camera_anchor'))
    camera=node('animAnimNode_SetBonePosition',
        bone={'$type':'animTransformIndex','name':name('Torso_fppCamera_Aim_JNT')},
        inputLink={'$type':'animPoseLink','node':{'HandleRefId':original_id}},
        positionMs={'$type':'animVectorLink','node':anchor})
    split=copy.deepcopy(template)
    split['firstInputNode']={'$type':'animPoseLink','node':original}
    split['secondInputNode']={'$type':'animPoseLink','node':camera}
    split['weightNode']=link(weight);split['minInputValue']=0;split['maxInputValue']=1
    split_node=new(split);inserted.append(split_node)
    output_node['node']['node']=split_node
    root['nodesToInit'].extend({'HandleRefId':n['HandleId']} for n in inserted)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8')
    return {'source':str(source),'output':str(output),'new_nodes':[v['HandleId'] for v in inserted],
            'native_crouch_scalar_retained':True,'camera_bone':'Torso_fppCamera_Aim_JNT'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();results=[]
    assert not (a.out/'player_locomotion.animgraph.json').exists(), 'Use a fresh output directory; locomotion override is retired'
    locomotion=json.loads((a.source/'player_locomotion.animgraph.json').read_text())
    springs=[]
    def find_spring(v):
        if isinstance(v,dict):
            if v.get('$type')=='animAnimNode_SpringDamp' and v.get('inputNode',{}).get('node',{}).get('Data',{}).get('variableName',{}).get('$value')=='crouch':springs.append(v)
            for x in v.values():find_spring(x)
        elif isinstance(v,list):
            for x in v:find_spring(x)
    find_spring(locomotion);assert len(springs)==1
    name='player_base'
    results.append(patch(a.source/(name+'.animgraph.json'),a.out/(name+'.animgraph.json'),springs[0]))
    print(json.dumps(results,indent=2))
