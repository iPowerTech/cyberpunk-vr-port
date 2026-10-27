"""Bounded swimming probes via the simulator; game memory is read-only.

The original HMD and controller poses are restored in finally. No focus changes,
keys, process-memory writes, teleportation or pause toggles are performed.
"""
import argparse,json,math,os,struct,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'roomscale_tests'))
from live_read import Reader
from simulator_probe import read_status,send,await_pose

p=argparse.ArgumentParser();p.add_argument('--addresses',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--case',choices=['posture','roomscale','forward','upward','downward','ascent'],required=True)
a=p.parse_args();cfg=json.loads(a.addresses.read_text());addr={k:int(v,16) for k,v in cfg.items() if isinstance(v,str) and v.startswith('0x')}
assert not a.out.exists() and a.out.parent.is_dir()
directory=Path(os.environ['LOCALAPPDATA'])/'OpenXR-Simulator'
h=read_status(directory)['head_tracking'];original={**h['position'],**{k:h[k] for k in ('yaw','pitch','roll')}}
assert all(abs(original[k])<1e-4 for k in ('yaw','pitch','roll')),'neutral simulator head required'
current=dict(original);rows=[];controllers=[];started=time.monotonic()
r=Reader(cfg['pid'],r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe',addr['CyberpunkVR_RoomscaleDebug'],addr['CyberpunkVR_RoomscaleDebugSeq'])
def get(name,fmt):return struct.unpack(fmt,r.read(addr[name],struct.calcsize(fmt)))
def swim(name,fmt):return get('cvr::swimming::'+name,fmt)
def sample(label):
    for _ in range(30):
        tick=get('g_solveCacheTick','<I')[0]
        solved=get('g_VRIKBodyEyeSolved','<3f');target=get('g_VRIKBodyEyeTarget','<3f')
        body=get('g_VRBodyBone','<33f');stamp=get('s_modelCacheCapturedMs','<Q')[0]
        if tick==get('g_solveCacheTick','<I')[0] and solved==get('g_VRIKBodyEyeSolved','<3f') and target==get('g_VRIKBodyEyeTarget','<3f') and body==get('g_VRBodyBone','<33f'):break
    else:raise RuntimeError('No stable body snapshot')
    motion=swim('s_input','<f?3xf??2x')
    row={'label':label,'t':time.monotonic()-started,'commanded':dict(current),'roomscale':r.sample(),
        'bend':get('g_VRIKBodyBendAngle','<f')[0],'squat':get('s_vrSharedSquatDrop','<f')[0],
        'cone':get('CyberpunkVR_BodyYawFollowDeadDeg','<f')[0],'eye_solved':solved,'eye_target':target,
        'eye_bound':get('g_VRIKBodyEyeBound','<?')[0],'body':body,'cache_stamp':stamp,
        'state':swim('s_state','<i')[0],'fast':swim('s_fast','<i')[0],
        'strokes':swim('s_strokes','<Q')[0],'ascents':swim('s_ascents','<Q')[0],
        'input':dict(zip(['forward','water','ascend','dive','boost'],motion)),
        'automatic':swim('s_automatic','<?')[0],'direction':swim('s_direction','<3f'),
        'redirect':get('CyberpunkVR_SwimmingRedirect','<3f'),
        'feedback_reads':get('CyberpunkVR_SwimmingFeedbackReads','<2Q')}
    rows.append(row)
    if 'CyberpunkVR_SwimmingPropertyFeedbackReads' in addr:
        row['property_feedback_reads']=get('CyberpunkVR_SwimmingPropertyFeedbackReads','<Q')[0]
    if 'cvr::swimming::s_waterContext' in addr:
        row['water_context']=swim('s_waterContext','<?')[0]
    if math.dist(row['roomscale']['after'],rows[0]['roomscale']['after'])>6:raise RuntimeError('Probe displacement exceeded 6m')
    return row
def hold(label,seconds):
    until=time.monotonic()+seconds
    while time.monotonic()<until:sample(label);time.sleep(.02)
def head(pose):
    global current
    current=pose;send(directory,pose)
def sweep(label,goal,seconds=.8):
    start=dict(current);begin=time.monotonic()
    while True:
        t=min(1,(time.monotonic()-begin)/seconds);u=t*t*(3-2*t)
        head({k:start[k]+(goal[k]-start[k])*u for k in start});sample(label)
        if t>=1:break
        time.sleep(.012)
    await_pose(directory,goal)
def controller(command):
    target=directory/'controller_pose_command.json';tmp=directory/(uuid.uuid4().hex+'.writing')
    tmp.write_text(json.dumps(command),encoding='ascii')
    for _ in range(40):
        try:os.replace(tmp,target);break
        except PermissionError:time.sleep(.01)
    else:raise RuntimeError('Controller command locked')
    deadline=time.monotonic()+2
    while target.exists():
        if time.monotonic()>deadline:raise RuntimeError('Controller command timed out')
        time.sleep(.003)
def hands(x,y,z,pitch=0):
    # Simulator positions are yaw-local, not pitch-local. Rotate the requested
    # HMD-relative stroke into that space explicitly for pitched swim tests.
    py=y*math.cos(pitch)-z*math.sin(pitch);pz=y*math.sin(pitch)+z*math.cos(pitch)
    for side in (0,1):controller({'hand':side,'posX':x*(1 if side else -1),'posY':py,'posZ':pz,'yaw':0,'pitch':0})

error=None
try:
    fresh=get('g_handsStable','<128f')
    for side in (0,1):
        i=side*8;assert fresh[i]==1,'controller unavailable'
        x,y,z,w=fresh[i+4:i+8];pitch=math.asin(max(-1,min(1,2*(w*x-y*z))))
        yaw=math.atan2(2*(w*y+x*z),1-2*(x*x+y*y))
        cy,sy,cp,sp=math.cos(yaw/2),math.sin(yaw/2),math.cos(pitch/2),math.sin(pitch/2)
        expected=(cy*sp,sy*cp,-sy*sp,cy*cp)
        assert min(max(abs(v-u) for v,u in zip((x,y,z,w),expected)),max(abs(v+u) for v,u in zip((x,y,z,w),expected)))<.001,'controller roll cannot be restored'
        controllers.append({'hand':side,'posX':fresh[i+1],'posY':fresh[i+2],'posZ':fresh[i+3],'yaw':yaw,'pitch':pitch})
    hold('baseline',.4)
    if a.case=='posture':
        sweep('down',{**original,'y':original['y']-.45,'pitch':-.95});hold('down_hold',.5)
        sweep('side',{**current,'yaw':.35});hold('side_hold',.5)
        sweep('return',original);hold('settled',.6)
    elif a.case=='roomscale':
        sweep('out',{**original,'x':original['x']+.16});hold('out_hold',1.5)
        sweep('return',original);hold('settled',1.5)
    else:
        pitch=.6 if a.case=='upward' else -.6 if a.case=='downward' else 0
        sweep('orient',{**original,'pitch':pitch},.4)
        if a.case=='ascent':
            hands(.30,-.15,-.25);hold('ready',.4)
            for i in range(1,26):
                hands(.30,-.15-.40*i/25,-.25);sample('push');time.sleep(.018)
        else:
            # Deliberately recover from any held state left by the original pose.
            hands(.26,-.45,-.10,pitch);hold('recover',.2)
            hands(.10,-.30,-.45,pitch);hold('ready',.4)
            for i in range(1,26):
                t=i/25;hands(.10+.15*t,-.30,-.45+.32*t,pitch);sample('pull');time.sleep(.018)
        hold('glide',1.6);hold('settled',1.2)
except BaseException as e:error=str(e)
finally:
    head(original);await_pose(directory,original)
    for c in controllers:controller(c)
    r.close()

report={'pid':cfg['pid'],'sha256':cfg['sha256'],'case':a.case,'rows':rows,'original':original,'controllers':controllers,'error':error,'game_memory_writes':False,'focus_changed':False}
a.out.write_text(json.dumps(report),encoding='utf-8')
if not rows:raise RuntimeError(error or 'no samples')
start,last=rows[0],rows[-1];moving=[x for x in rows if math.hypot(*x['roomscale']['native'][:2])>.0001]
summary={'case':a.case,'samples':len(rows),'error':error,
    'bend_max_deg':max(x['bend'] for x in rows)*180/math.pi,'squat_max_m':max(x['squat'] for x in rows),
    'cone_range':[min(x['cone'] for x in rows),max(x['cone'] for x in rows)],
    'eye_error_max_mm':max(math.dist(x['eye_solved'],x['eye_target'])*1000 for x in rows if x['eye_bound']),
    'strokes':last['strokes']-start['strokes'],'ascents':last['ascents']-start['ascents'],
    'forward_peak':max(x['input']['forward'] for x in rows),'ascend_peak':max(x['input']['ascend'] for x in rows),
    'automatic_samples':sum(x['automatic'] for x in rows),'native_motion_samples':len(moving),
    'feedback_reads_delta':[last['feedback_reads'][i]-start['feedback_reads'][i] for i in range(2)],
    'world_delta':[last['roomscale']['after'][i]-start['roomscale']['after'][i] for i in range(3)],
    'velocity_peak':max(math.sqrt(sum(v*v for v in x['roomscale']['velocity'])) for x in rows),
    'last_velocity':last['roomscale']['velocity']}
if 'property_feedback_reads' in last:
    summary['property_feedback_reads_delta']=last['property_feedback_reads']-start['property_feedback_reads']
print(json.dumps(summary));
if error:raise RuntimeError(error)
