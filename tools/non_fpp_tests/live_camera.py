"""Bounded camera diagnostics and simulator head trials, without game input.

Resolves an exactly matching DLL/PDB. The only process write enables the existing
diagnostic gate for the capture and restores its original bytes in finally.
Head poses also restore in finally. No focus changes, keys or camera events.
"""
import argparse,ctypes as c,json,math,os,struct,sys,time
from ctypes import wintypes as w
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]/n) for n in ('swimming_tests','roomscale_tests')]
from live_symbols import resolve
from live_read import Reader
from simulator_probe import read_status,send,await_pose

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid',type=int,required=True);p.add_argument('--dll',type=Path,required=True)
p.add_argument('--out',type=Path,required=True);p.add_argument('--poses',action='store_true')
p.add_argument('--yaw-trial',action='store_true')
p.add_argument('--settle',type=float,default=0)
p.add_argument('--seconds',type=float,default=2)
a=p.parse_args();assert 1<=a.seconds<=10 and a.out.parent.is_dir() and not a.out.exists()
assert a.settle==0 or 1<=a.settle<=8
names=['CyberpunkVR_RuntimeDiagnostics','CyberpunkVR_ExternalCameraDebug','CyberpunkVR_ExternalCameraDebugSeq',
       'CyberpunkVR_PoseIdFinal','CyberpunkVR_PoseIdFinalMiss','CyberpunkVR_PoseBlendDebug','CyberpunkVR_PoseBlendDebugSeq',
       'g_camObjMain','cvr::detail::g_vrcam_comp','g_bdActive','g_bdScenePoseValid','g_remoteCamOn',
       'CyberpunkVR_MainIsRightEye','CyberpunkVR_IpdInWorldPos',
       'CyberpunkVR_ExternalSelectionStates','CyberpunkVR_PanzerSteeringDebug','CyberpunkVR_PanzerSteeringDebugSeq',
       'CyberpunkVR_ExternalNotify','cvr::framegen::cameras',
       'CyberpunkVR_FramegenReport','CyberpunkVR_FramegenReportSeq']
syms=resolve(a.pid,a.dll.resolve(),names)
r=Reader(a.pid,r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe',0,0)
k=c.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.WriteProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes=[w.HANDLE]
write_handle=k.OpenProcess(0x28,False,a.pid)
if not write_handle:raise c.WinError(c.get_last_error())
gate=int(syms['CyberpunkVR_RuntimeDiagnostics'],16);original_gate=r.read(gate,4)
directory=Path(os.environ['LOCALAPPDATA'])/'OpenXR-Simulator'
original=None;rows=[];result={'pid':a.pid,'symbols':syms,'rows':rows,'game_input':False,'focus_changed':False}

def write_gate(data):
    size=c.c_size_t();buf=c.create_string_buffer(data)
    if not k.WriteProcessMemory(write_handle,gate,buf,len(data),c.byref(size)) or size.value!=len(data):
        raise c.WinError(c.get_last_error())

def read(name,fmt):return struct.unpack('<'+fmt,r.read(int(syms[name],16),struct.calcsize('<'+fmt)))

def coherent(name,seq,size,index=0):
    for _ in range(50):
        before=struct.unpack('<I',r.read(int(syms[seq],16)+index*4,4))[0]
        if before&1:continue
        data=r.read(int(syms[name],16)+index*size,size)
        after=struct.unpack('<I',r.read(int(syms[seq],16)+index*4,4))[0]
        if before==after and not after&1:return data
    raise RuntimeError('diagnostic snapshot stayed busy')

def external(eye):
    v=struct.unpack('<5Q3i4f7ff4x',coherent('CyberpunkVR_ExternalCameraDebug','CyberpunkVR_ExternalCameraDebugSeq',104,eye))
    return dict(writes=v[0],stamp_us=v[1],pose_id=v[2],origin=v[3],component=hex(v[4]),
                position=[x/131072 for x in v[5:8]],rotation=v[8:12],head=v[12:19],fov=v[19])

def native_pose(address):
    pos=struct.unpack('<3i',r.read(address,12));q=struct.unpack('<4f',r.read(address+16,16))
    return {'position':[x/131072 for x in pos],'rotation':q}

def angle(q,rq):
    d=abs(sum(x*y for x,y in zip(q,rq)))/math.sqrt(sum(x*x for x in q)*sum(x*x for x in rq))
    return math.degrees(2*math.acos(min(1,d)))

def render_cameras():
    # Match the actual render-frame records, not two independently sampled
    # "latest" counters which can legitimately straddle an engine update.
    address=int(syms['cvr::framegen::cameras'],16)
    for _ in range(10):
        data=r.read(address,2*8*112)
        if data==r.read(address,len(data)):break
    else:return []
    entries=[]
    for view in range(2):
        for index in range(8):
            offset=(view*8+index)*112
            if not data[offset+92]:continue
            values=struct.unpack_from('<20f',data,offset)
            entries.append(dict(view=view+1,frame=struct.unpack_from('<I',data,offset+88)[0],
                pose=struct.unpack_from('<Q',data,offset+96)[0],origin=struct.unpack_from('<Q',data,offset+104)[0],
                position=values[4:7],up=values[7:10],right=values[10:13],forward=values[13:16],fov=values[18]))
    return entries

def snapshot(label):
    main,vr=external(0),external(1)
    data=coherent('CyberpunkVR_PoseBlendDebug','CyberpunkVR_PoseBlendDebugSeq',456)
    calls,labelled,rejected,director,count,accepted=struct.unpack_from('<4Q2I',data)
    blend=dict(calls=calls,labelled=labelled,rejected=rejected,director=hex(director),count=count,accepted=accepted)
    row=dict(label=label,time=time.time(),main=main,vrcam=vr,blend=blend,
             final=read('CyberpunkVR_PoseIdFinal','2Q'),miss=read('CyberpunkVR_PoseIdFinalMiss','2Q'))
    row['handoff']=read('CyberpunkVR_ExternalSelectionStates','4Q')
    row['notify']=read('CyberpunkVR_ExternalNotify','3Q')
    row['render_cameras']=render_cameras()
    raw=coherent('CyberpunkVR_FramegenReport','CyberpunkVR_FramegenReportSeq',2048).split(b'\0',1)[0]
    row['framegen']=json.loads(raw) if raw else None
    steer=struct.unpack('<6Q6f2I',coherent('CyberpunkVR_PanzerSteeringDebug','CyberpunkVR_PanzerSteeringDebugSeq',80))
    row['steering']=dict(calls=steer[0],applied=steer[1],model=hex(steer[2]),owner=hex(steer[3]),stamp=steer[4],pose_id=steer[5],
                         native=steer[6:9],target=steer[9:12],reason=steer[12])
    if steer[3]:
        body=struct.unpack('<Q',r.read(steer[3]+0x5d0,8))[0]
        if body:row['vehicle']=native_pose(body+0x10)
    if main['writes'] and vr['writes'] and main['pose_id']==vr['pose_id'] and main['origin']==vr['origin']:
        row['distance']=math.dist(main['position'],vr['position'])
        row['angle_degrees']=angle(main['rotation'],vr['rotation'])
        if int(main['component'],16):row['source']=native_pose(int(main['component'],16)+0x3a0)
    if director:
        row['director_output']=native_pose(director+0x4c0)
    rows.append(row);return row

def hold(label,duration):
    end=time.monotonic()+duration
    while time.monotonic()<end:snapshot(label);time.sleep(.025)

try:
    write_gate(struct.pack('<I',1))
    result['mode']={n:read(n,'I')[0] for n in ('g_bdActive','g_bdScenePoseValid','g_remoteCamOn','CyberpunkVR_MainIsRightEye','CyberpunkVR_IpdInWorldPos')}
    hold('baseline',a.seconds)
    if a.poses or a.yaw_trial:
        assert all(last>first for first,last in zip(rows[0]['final'],rows[-1]['final'])), 'both eye renders must advance before motion tests'
        head=read_status(directory)['head_tracking']
        original={**head['position'],**{key:head[key] for key in ('yaw','pitch','roll')}}
        result['original_head']=original
        assert rows[-1]['vrcam']['writes']>rows[0]['vrcam']['writes'],'MAIN-to-VRCAM handoff must advance'
        cases=[('yaw_left',{'yaw':-.5}),('yaw_right',{'yaw':.5}),('pitch_down',{'pitch':-.35}),
               ('pitch_up',{'pitch':.3}),('roll',{'roll':.25}),('lean_right',{'x':.08}),
               ('lean_forward',{'z':-.08}),('lean_up',{'y':.07}),
               ('combined',{'yaw':.35,'pitch':-.2,'roll':-.15,'x':-.05,'y':-.04,'z':.05})]
        if a.yaw_trial:cases=[('left_slow',{'yaw':-.35}),('right_slow',{'yaw':.35}),('left_fast',{'yaw':-.35}),('right_fast',{'yaw':.35})]
        def ramp(start,target,label,seconds):
            begin=time.monotonic()
            while True:
                t=min(1,(time.monotonic()-begin)/seconds)
                pose={key:start[key]+(target[key]-start[key])*t for key in start}
                # Small real-headset-like positional noise must not steer yaw.
                if t<1:pose['x']+=.002*math.sin(69*(time.monotonic()-begin))
                send(directory,pose);snapshot(label)
                if t>=1:break
                time.sleep(.01)
            await_pose(directory,target)
        for label,delta in cases:
            target={key:value+delta.get(key,0) for key,value in original.items()}
            if a.yaw_trial:ramp(original,target,label+'_ramp',1.4 if 'slow' in label else .35)
            else:send(directory,target);await_pose(directory,target)
            hold(label,a.settle or (2.4 if a.yaw_trial else 1.2))
            if a.yaw_trial:ramp(target,original,label+'_back',.8)
            else:send(directory,original);await_pose(directory,original)
            hold(label+'_return',a.settle or (1.5 if a.yaw_trial else .5))
    first,last=rows[0],rows[-1]
    summary={'rows':len(rows),'main_writes':last['main']['writes']-first['main']['writes'],
             'vrcam_writes':last['vrcam']['writes']-first['vrcam']['writes'],
             'final_delta':[b-a for a,b in zip(first['final'],last['final'])],
             'miss_delta':[b-a for a,b in zip(first['miss'],last['miss'])],
             'last_distance':last.get('distance'),'last_angle':last.get('angle_degrees'),
             'fov':[last['main']['fov'],last['vrcam']['fov']],
             'blend_delta':{n:last['blend'][n]-first['blend'][n] for n in ('calls','labelled','rejected')}}
    summary['handoff_delta']=[b-a for a,b in zip(first['handoff'],last['handoff'])]
    summary['notify_delta']=[b-a for a,b in zip(first['notify'],last['notify'])]
    summary['steering_delta']={n:last['steering'][n]-first['steering'][n] for n in ('calls','applied')}
    summary['last_steering']=last['steering']
    result['summary']=summary
except BaseException as error:
    result['error']=repr(error)
finally:
    try:
        if original:send(directory,original);await_pose(directory,original);result['head_restored']=True
    finally:
        write_gate(original_gate);result['gate_restored']=r.read(gate,4)==original_gate
        k.CloseHandle(write_handle);r.close()
        a.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({key:value for key,value in result.items() if key not in ('rows','symbols')},indent=2))
if result.get('error'):raise SystemExit(1)
