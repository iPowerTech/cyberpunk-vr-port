"""Inspect fresh adjacent native material runs in the supplied capture."""
import argparse
import json
import os
from pathlib import Path
import sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('capture');p.add_argument('--out',type=Path,required=True)
p.add_argument('--start',type=int,default=10042);p.add_argument('--stop',type=int,default=18000)
p.add_argument('--draws',type=int,default=96)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
module=r'C:\Users\dariulone\Desktop\renderdoc-src\x64\Release\pymodules'
sys.path.insert(0,module);cookie=os.add_dll_directory(module)
import renderdoc as rd
cap=rd.OpenCaptureFile();controller=None
try:
    status=cap.OpenFile(a.capture,'',None);assert status==rd.ResultCode.Succeeded,str(status)
    status,controller=cap.OpenCapture(rd.ReplayOptions(),None);assert status==rd.ResultCode.Succeeded,str(status)
    actions=[]
    def walk(nodes):
        for n in nodes:
            if n.flags & rd.ActionFlags.Drawcall and a.start<=n.eventId<a.stop:actions.append(n)
            walk(n.children)
    walk(controller.GetRootActions());actions.sort(key=lambda n:n.eventId)
    rows=[];runs=[];previous=None
    for n in actions[:a.draws]:
        controller.SetFrameEvent(n.eventId,True);s=controller.GetD3D12PipelineState();pipe=controller.GetPipelineState()
        vs=pipe.GetShaderReflection(rd.ShaderStage.Vertex);ps=pipe.GetShaderReflection(rd.ShaderStage.Pixel)
        om=s.outputMerger;ds=om.depthStencilState
        policy={'depthEnable':ds.depthEnable,'depthFunction':int(ds.depthFunction),'depthWrites':ds.depthWrites,
            'stencilEnable':ds.stencilEnable,'front':[int(ds.frontFace.function),int(ds.frontFace.failOperation),int(ds.frontFace.depthFailOperation),int(ds.frontFace.passOperation),ds.frontFace.reference,ds.frontFace.writeMask],
            'back':[int(ds.backFace.function),int(ds.backFace.failOperation),int(ds.backFace.depthFailOperation),int(ds.backFace.passOperation),ds.backFace.reference,ds.backFace.writeMask],
            'blend':[(b.enabled,b.logicOperationEnabled,b.writeMask) for b in om.blendState.blends],
            'depthReadOnly':om.depthReadOnly,'stencilReadOnly':om.stencilReadOnly}
        shaders=[int(x.resourceId) if x else 0 for x in (vs,ps)]
        outputs=[int(x.resource) for x in pipe.GetOutputTargets() if int(x.resource)]
        key=json.dumps([shaders,outputs,policy],sort_keys=True)
        if key!=previous:
            run={'first':n.eventId,'draws':0,'indices':0,'instances':0,'shaders':shaders,'outputs':outputs,'policy':policy,'shaderData':{}}
            for stage,ref in [('Vertex',vs),('Pixel',ps)]:
                if not ref:continue
                path=a.out/f'{n.eventId}-{stage}.dxil';path.write_bytes(bytes(ref.rawBytes))
                run['shaderData'][stage]={'bytes':len(ref.rawBytes),'file':path.name,
                    'cb':[{'bytes':b.byteSize,'register':b.fixedBindNumber,'space':b.fixedBindSetOrSpace} for b in ref.constantBlocks]}
            runs.append(run);previous=key
            print(json.dumps({'first':n.eventId,'shaders':shaders,'outputs':outputs}),flush=True)
        runs[-1]['draws']+=1;runs[-1]['indices']+=n.numIndices*n.numInstances;runs[-1]['instances']+=n.numInstances;runs[-1]['last']=n.eventId
        rows.append({'event':n.eventId,'indices':n.numIndices,'instances':n.numInstances,'run':len(runs)-1})
    result={'capture':a.capture,'start':a.start,'stop':a.stop,'drawCount':len(rows),'runs':runs,'draws':rows}
    (a.out/'runs.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({'draws':len(rows),'runs':len(runs),'file':str(a.out/'runs.json')}))
finally:
    if controller is not None:controller.Shutdown()
    cap.Shutdown();cookie.close()
