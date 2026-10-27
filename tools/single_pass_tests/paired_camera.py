"""Match an early peer camera to the exact later native depth pass, not a best fit."""
import struct

STRIDE=4616


def decode_cameras(raw):
    if len(raw)%STRIDE:raise ValueError('Invalid camera trace ABI')
    rows=[]
    for i in range(len(raw)//STRIDE):
        data=raw[i*STRIDE:(i+1)*STRIDE]
        order,qpc,object_,frame,node,kind,index,side,thread=struct.unpack_from('<3Q4IiI',data)
        if order!=i+1 or side not in (0,1) or kind not in (1,2):raise ValueError('Invalid camera trace identity')
        context,view,camera,width,height,valid,reason=struct.unpack_from('<3Q4I',data,896)
        prediction,counter=struct.unpack_from('<2I',data,1856)
        shader,previous=struct.unpack_from('<2I',data,2768)
        row=dict(order=order,qpc=qpc,frame=frame,node=node,kind=kind,index=index,side=side,view=view,thread=thread,context=context,
                 cameraAddress=camera,width=width,height=height,inputValid=valid,reason=reason,
                 predictionValid=prediction,counter=counter,shaderValid=shader,previousValid=previous,
                 peerSide=struct.unpack_from('<I',data,4612)[0],mask=struct.unpack_from('<4Q',data,4528))
        row.update(camera=data[48:896],input=data[936:1840],predictedInput=data[1864:2768],
                   predictedShader=data[2776:3624],previous=data[3624:4528],peerPose=data[4560:4592])
        rows.append(row)
    return rows


def compare_cameras(cameras,preparation,frames,views,require_motion=False):
    results=[]
    for frame in range(1,frames+1):
        depths=[];selected=[]
        for side in (0,1):
            calls=[r for r in preparation if r['frame']==frame and r['side']==side and r['kind']==13 and r['stage']==3]
            depth=min(calls,key=lambda r:r['qpc']) if calls else None;depths.append(depth)
            candidates=[r for r in cameras if depth and r['frame']==frame and r['side']==side and r['kind']==1
                        and r['node']==0x23A938 and r['view']==views[side] and r['inputValid'] and r['qpc']<depth['qpc']
                        and r['thread']==depth['thread'] and r['context']==depth['context']]
            selected.append(max(candidates,key=lambda r:r['qpc']) if candidates else None)
        result=dict(frame=frame,passed=False)
        source,target=selected
        if not source or not target:result['error']='Missing camera before the exact depth invocation'
        else:
            result.update(source=source['order'],native=target['order'],sourceReason=source['reason'],
                          sourceThread=source['thread'],nativeThread=target['thread'],
                          sourceSize=[source['width'],source['height']],nativeSize=[target['width'],target['height']],
                          sourceCameraOffset=source['cameraAddress']-source['view'],nativeCameraOffset=target['cameraAddress']-target['view'])
            known=[i for i in range(212) if source['mask'][i//64]&(1<<(i%64))]
            expected_words=list(range(212)) if source['reason']==12 else [i for i in range(212) if i!=147]
            eligible=(source['predictionValid'] and source['shaderValid'] and source['previousValid'] and source['peerSide']==1
                      and known==expected_words and source['qpc']<depths[0]['qpc']<target['qpc']
                      and result['sourceSize']==result['nativeSize'])
            result['eligible']=bool(eligible)
            result['poseChangedFromHistory']=(source['predictedInput'][:12]!=source['previous'][:12] or
                                             source['predictedInput'][16:32]!=source['previous'][16:32])
            result['unavailableShaderWords']=[i for i in range(212) if i not in known]
            result['shaderDifferentOffsets']=[hex(i*4) for i in known if source['predictedShader'][i*4:i*4+4]!=target['camera'][i*4:i*4+4]]
            ranges=[(0,12),(16,32),(0x50,0x150),(0x190,0x290),(0x2B0,0x384)]
            result['inputDifferentOffsets']=[hex(i) for a,b in ranges for i in range(a,b,4)
                                             if source['predictedInput'][i:i+4]!=target['input'][i:i+4]]
            result['passed']=bool(eligible and not result['shaderDifferentOffsets'] and not result['inputDifferentOffsets'])
        results.append(result)
    moving=[r['frame'] for r in results if r.get('poseChangedFromHistory')]
    return dict(passed=bool(results) and all(r['passed'] for r in results) and (bool(moving) or not require_motion),frames=results,
                motionRequired=require_motion,movingFrames=moving,
                unavailableShaderWords=sorted({i for r in results for i in r.get('unavailableShaderWords',range(212))}),
                limitation='Current camera/history constants only; not a complete draw/resource or image-equivalence proof.')


def main():
    import argparse
    import hashlib
    import json
    from pathlib import Path
    from read_preparation import decode
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('preparation',type=Path)
    parser.add_argument('--require-motion',action='store_true')
    args=parser.parse_args()
    source=args.preparation.read_bytes();report=json.loads(source)
    camera_path=args.preparation.with_name(report['pairedCameras']['file'])
    camera_raw=camera_path.read_bytes()
    records=args.preparation.with_name(report['recordsFile']).read_bytes()
    packets=args.preparation.with_name(report['packetsFile']).read_bytes()
    if hashlib.sha256(records).hexdigest()!=report['recordHash']:raise ValueError('Preparation record hash changed')
    rows=decode(records,packets,report['frames'])
    pair=report['pairPreparation']
    audit=compare_cameras(decode_cameras(camera_raw),rows,pair['rootsMerged'],[pair['mainView'],pair['vrcamView']],args.require_motion)
    audit['source']=dict(pid=report['pid'],dll=report['dll'],report=hashlib.sha256(source).hexdigest(),
                        records=report['recordHash'],cameras=hashlib.sha256(camera_raw).hexdigest())
    output=args.preparation.with_name(args.preparation.stem+'-cameras-audit.json')
    output.write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(dict(passed=audit['passed'],frames=len(audit['frames']),moving=audit['movingFrames'],out=str(output))))
    return 0 if audit['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
