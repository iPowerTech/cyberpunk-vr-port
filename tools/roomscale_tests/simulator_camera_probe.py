"""Read component camera centres during a held-position HMD turn; no focus/keys."""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import time
import uuid
import subprocess
import sys
import ctypes as c
from ctypes import wintypes as w
from live_read import Reader
from simulator_probe import read_status, send, await_pose

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pid',type=int,required=True)
for name in ('debug-address','sequence-address','main-camera','vrcam-camera','entity-quat-address','realign-address','engine-camera-address'):
    parser.add_argument('--'+name,type=lambda s:int(s,0),required=True)
parser.add_argument('--degrees',type=float,default=60)
parser.add_argument('--body-snapshot',action='store_true')
parser.add_argument('--runtime-reset',action='store_true')
parser.add_argument('--mouse-x',type=int,default=0)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
if not args.out.parent.is_dir() or args.out.exists() or abs(args.degrees)>180: raise RuntimeError('Invalid output or turn')
if abs(args.mouse_x)>800 or (args.mouse_x and args.runtime_reset):raise ValueError('Invalid mouse comparison')
directory=Path(os.environ['LOCALAPPDATA'])/'OpenXR-Simulator'
status=read_status(directory); h=status['head_tracking']
original={**h['position'],**{k:h[k] for k in ('yaw','pitch','roll')}}
target={**original,'yaw':original['yaw']+math.radians(args.degrees)}
reader=Reader(args.pid,Path('C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe'),args.debug_address,args.sequence_address)
rows=[]
result={'pid':args.pid,'initial_status':status,'original':original,'target':target,'samples':rows,
        'focus_changed':bool(args.mouse_x),'keys_sent':False,'mouse_counts':args.mouse_x,
        'boundary':'Component snapshots across running updates; endpoint holds used, not same-frame eye pairs.'}

# Read-only view of the existing hand/anchor publication. No live variable writes.
kernel=c.WinDLL('kernel32',use_last_error=True)
kernel.OpenFileMappingW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR];kernel.OpenFileMappingW.restype=w.HANDLE
kernel.MapViewOfFile.argtypes=[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,c.c_size_t];kernel.MapViewOfFile.restype=c.c_void_p
kernel.UnmapViewOfFile.argtypes=[c.c_void_p];kernel.CloseHandle.argtypes=[w.HANDLE]
mapping=kernel.OpenFileMappingW(4,False,'CyberpunkVR_Hands_Shared')
shared=kernel.MapViewOfFile(mapping,4,0,0,1024) if mapping else None
def shared_snapshot():
    if not shared:return None
    values=struct.unpack('<256f',c.string_at(shared,1024))
    return {'cam_bake':values[91:94], 'eye_bake_model':values[116:119],
            'total_offset':values[120:123], 'head_base':values[124:127],
            'view_quat':values[104:108], 'heading':values[141]}

def position(addr): return [x/131072 for x in struct.unpack('<3i',reader.read(addr,12))]

def sample(label):
    row=reader.sample(); row['label']=label
    row['main']=position(args.main_camera+0xe0); row['vrcam']=position(args.vrcam_camera+0xe0)
    row['centre']=[(a+b)/2 for a,b in zip(row['main'],row['vrcam'])]
    row['engine_camera']=position(args.engine_camera_address)
    q=struct.unpack('<4f',reader.read(args.entity_quat_address,16)); row['entity_yaw']=2*math.atan2(q[2],q[3])
    row['realign']=struct.unpack('<f',reader.read(args.realign_address,4))[0]
    row['main_to_entity']=[a-b for a,b in zip(row['main'],row['after'])]
    rows.append(row); return row

def hold(label):
    until=time.monotonic()+1.5
    while time.monotonic()<until: row=sample(label); time.sleep(.02)
    row['shared']=shared_snapshot()
    if args.body_snapshot:
        bridge=Path('C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077/bin/x64/plugins/cyber_engine_tweaks/mods/CETBridge')
        request=bridge/'command.json'; response=bridge/'response.json'
        if request.exists(): raise RuntimeError('CET bridge is busy')
        identity=uuid.uuid4().hex
        expr='(function() return {head=Game.VRBodyBonePos(4),hips=Game.VRBodyBonePos(0),cam=Game.VRCamModelPos(),pos=Game.GetPlayer():GetWorldPosition(),rot=Game.GetPlayer():GetWorldOrientation()} end)()'
        temporary=bridge/('command.'+identity+'.tmp')
        temporary.write_text(json.dumps({'id':identity,'type':'eval','expr':expr}),encoding='ascii')
        os.replace(temporary,request)
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            try:
                reply=json.loads(response.read_text(encoding='utf-8'))
                if reply.get('id')==identity:
                    if not reply.get('ok'): raise RuntimeError(reply.get('error'))
                    body=reply['result']; body=json.loads(body) if isinstance(body,str) else body
                    response.unlink() # bridge.lua uses rename, which needs an absent destination on Windows
                    row['body']=body
                    p=body['head']; q=body['rot']; e=body['pos']
                    # World position from the model-space publication and the full entity quaternion.
                    x,y,z=(p[k] for k in ('x','y','z')); a,b,d,w=(q[k] for k in ('i','j','k','r'))
                    tx,ty,tz=2*(b*z-d*y),2*(d*x-a*z),2*(a*y-b*x)
                    row['head_world']=[e['x']+x+w*tx+b*tz-d*ty,
                                       e['y']+y+w*ty+d*tx-a*tz,
                                       e['z']+z+w*tz+a*ty-b*tx]
                    break
            except (OSError,json.JSONDecodeError): pass
            time.sleep(.02)
        else: raise RuntimeError('CET body snapshot timed out')
    return row

def turn(start,end,label):
    begin=time.monotonic()
    while True:
        t=min(1,(time.monotonic()-begin)/2); u=t*t*(3-2*t)
        send(directory,{k:start[k]+(end[k]-start[k])*u for k in start})
        sample(label)
        if t==1:break
        time.sleep(.01)

def mouse(counts):
    helper=Path(__file__).resolve().parents[2]/'render_camera_RE/scripts/native_re_input.py'
    log=args.out.with_suffix('.input.jsonl')
    process=subprocess.run([sys.executable,'-B',str(helper),'--pid',str(args.pid),
        '--exe','C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe',
        '--mouse-x',str(counts),'--seconds','.5','--log',str(log)],capture_output=True,text=True,timeout=8)
    result.setdefault('mouse_input',[]).append({'counts':counts,'returncode':process.returncode,'output':process.stdout,'stderr':process.stderr})
    if process.returncode:raise RuntimeError('Mouse helper failed; inspect input log')

try:
    before=hold('before'); result['before']=before
    if args.runtime_reset:
        from simulator_menu import inspect
        windows=inspect(args.pid)
        if len(windows)!=1:raise RuntimeError('Ambiguous simulator window')
        def has_reset(items):
            return any((r['id']==1402 and 'Reset View' in r['text']) or has_reset(r.get('items',[])) for r in items)
        if not has_reset(windows[0]['items']):raise RuntimeError('Reset View menu command not verified')
        user=c.WinDLL('user32',use_last_error=True)
        user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
        if not user.PostMessageW(windows[0]['hwnd'],0x111,1402,0):raise c.WinError(c.get_last_error())
        result['reset_command']={'hwnd':windows[0]['hwnd'],'id':1402,'name':'Reset View'}
        time.sleep(1)
        result['status_after_reset']=read_status(directory)
    elif args.mouse_x:
        mouse(args.mouse_x)
    else:
        turn(original,target,'turn')
    after=hold('held'); result['after']=after
    reset_head=read_status(directory)['head_tracking']
    current={**reset_head['position'],**{k:reset_head[k] for k in ('yaw','pitch','roll')}}
    if args.mouse_x:mouse(-args.mouse_x)
    else:turn(current,original,'return')
    result['returned']=hold('returned')
    result['summary']={
        'cct_shift':math.dist(before['after'],after['after']),
        'camera_centre_shift':math.dist(before['centre'],after['centre']),
        'engine_camera_shift':math.dist(before['engine_camera'],after['engine_camera']),
        'camera_centre_delta':[b-a for a,b in zip(before['centre'],after['centre'])],
        'body_yaw_delta_deg':math.degrees(math.remainder(after['entity_yaw']-before['entity_yaw'],2*math.pi)),
        'realign_delta_deg':math.degrees(math.remainder(after['realign']-before['realign'],2*math.pi)),
        'return_camera_error':math.dist(before['centre'],result['returned']['centre'])}
    if args.body_snapshot:
        result['summary']['head_world_shift']=math.dist(before['head_world'],after['head_world'])
        result['summary']['camera_minus_head_before']=[a-b for a,b in zip(before['centre'],before['head_world'])]
        result['summary']['camera_minus_head_after']=[a-b for a,b in zip(after['centre'],after['head_world'])]
        def local_delta(s):
            yaw=s['entity_yaw']; dx,dy,dz=[a-b for a,b in zip(s['centre'],s['head_world'])]
            return [math.cos(yaw)*dx+math.sin(yaw)*dy,-math.sin(yaw)*dx+math.cos(yaw)*dy,dz]
        result['summary']['camera_minus_head_body_local_before']=local_delta(before)
        result['summary']['camera_minus_head_body_local_after']=local_delta(after)
        result['summary']['head_view_offset_change_body_local']=math.dist(local_delta(before),local_delta(after))
except BaseException as exc:
    result['error']=f'{type(exc).__name__}: {exc}'
finally:
    try: send(directory,original); result['final_status']=await_pose(directory,original)
    finally:
        reader.close()
        if shared:kernel.UnmapViewOfFile(shared)
        if mapping:kernel.CloseHandle(mapping)
        args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ('samples','before','after','returned','mouse_input')},indent=2))
if result.get('error'):raise SystemExit(1)
