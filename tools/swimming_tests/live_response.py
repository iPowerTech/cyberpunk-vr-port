"""Bounded swimming response probe; only simulator hand poses are changed.

Requires neutral simulator HMD angles and an idle player in water. Reads only
matching-DLL symbols, restores both controller poses, and never changes settings,
focus, game memory, inventory, player position or pause state directly.
"""
import argparse,json,math,os,struct,sys,time,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'roomscale_tests'))
from live_read import Reader
from simulator_probe import read_status
from live_symbols import resolve

p=argparse.ArgumentParser();p.add_argument('--pid',required=True,type=int)
p.add_argument('--dll',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
p.add_argument('--duration',required=True,type=float)
p.add_argument('--followup-duration',type=float);a=p.parse_args()
assert .2<=a.duration<=2 and a.out.parent.is_dir() and not a.out.exists()
assert a.followup_duration is None or .2<=a.followup_duration<=2
directory=Path(os.environ['LOCALAPPDATA'])/'OpenXR-Simulator'
head=read_status(directory)['head_tracking']
assert all(abs(head[k])<1e-4 for k in ('yaw','pitch','roll')),'neutral simulator head required; no poses sent'
names=['g_handsStable']+['cvr::swimming::'+x for x in
    ('s_input','s_state','s_fast','s_strokes','s_ascents','s_automatic','s_manual','s_paused','s_tracking')]
symbols=resolve(a.pid,a.dll.resolve(),names);address={n:int(symbols[n],16) for n in names}
r=Reader(a.pid,r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe',0,0)
rows=[];original=[];run=uuid.uuid4().hex;start=time.monotonic();phase='baseline';commanded=0
def read(name,fmt):return struct.unpack(fmt,r.read(address[name],struct.calcsize(fmt)))
def swim(name,fmt):return read('cvr::swimming::'+name,fmt)
def sample():
    motion=swim('s_input','<f?3xf??2x')
    row={'t':time.monotonic()-start,'phase':phase,'progress':commanded,
         'input':dict(zip(('forward','water','ascend','dive','boost'),motion)),
         'state':swim('s_state','<i')[0],'fast':swim('s_fast','<i')[0],
         'strokes':swim('s_strokes','<Q')[0],'ascents':swim('s_ascents','<Q')[0],
         'automatic':swim('s_automatic','<?')[0],'manual':swim('s_manual','<?')[0],
         'paused':swim('s_paused','<?')[0],'tracking':swim('s_tracking','<?')[0]}
    rows.append(row)
    return row
def hold(seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:sample();time.sleep(.012)
def command(value):
    path=directory/'controller_pose_command.json';end=time.monotonic()+3
    while path.exists():
        if time.monotonic()>end:raise RuntimeError('Existing simulator command not consumed')
        time.sleep(.003)
    temp=directory/(run+'.writing');temp.write_text(json.dumps({**value,'_probe_run':run}),encoding='ascii')
    os.replace(temp,path)
    while path.exists():
        if time.monotonic()>end:raise RuntimeError('Simulator hand command not consumed')
        time.sleep(.002)
def hands(x,y,z):
    for side in (0,1):command({'hand':side,'posX':x*(1 if side else -1),'posY':y,'posZ':z,'yaw':0,'pitch':0})

def trajectory(duration,reverse=False):
    global commanded
    began=time.monotonic()
    while True:
        progress=min(1,(time.monotonic()-began)/duration)
        commanded=1-progress if reverse else progress
        hands(.10+.15*commanded,-.30,-.45+.32*commanded);sample()
        if progress>=1:break
        time.sleep(.004)

error=None;mutated=False;beforeFollowup=None
try:
    initial=sample();assert initial['state'] in (1,2) and not initial['paused'] and not initial['manual'],'idle water context required'
    assert initial['tracking'],'both controllers required'
    values=read('g_handsStable','<128f')
    for side in (0,1):
        i=side*8;assert values[i]==1
        x,y,z,w=values[i+4:i+8]
        pitch=math.asin(max(-1,min(1,2*(w*x-y*z))))
        yaw=math.atan2(2*(w*y+x*z),1-2*(x*x+y*y))
        cy,sy,cp,sp=math.cos(yaw/2),math.sin(yaw/2),math.cos(pitch/2),math.sin(pitch/2)
        expected=(cy*sp,sy*cp,-sy*sp,cy*cp)
        assert min(max(abs(v-u) for v,u in zip((x,y,z,w),expected)),max(abs(v+u) for v,u in zip((x,y,z,w),expected)))<.001,'controller roll cannot be restored'
        original.append({'hand':side,'posX':values[i+1],'posY':values[i+2],'posZ':values[i+3],'yaw':yaw,'pitch':pitch})
    phase='settle';mutated=True;hands(.26,-.45,-.10);hold(1.4)
    phase='ready';hands(.10,-.30,-.45);hold(.35)
    before=sample();phase='pull';trajectory(a.duration)
    if a.followup_duration is not None:
        phase='first_glide';hold(.08)
        phase='recovery';trajectory(.18,reverse=True)
        phase='followup_ready';hold(.15);beforeFollowup=sample()
        phase='followup_pull';trajectory(a.followup_duration)
    phase='glide';hold(1.6);phase='stopped';hold(.35)
except BaseException as e:error=str(e)
finally:
    for value in original if mutated else []:
        try:command(value)
        except Exception as e:error=(error+'; ' if error else '')+'restore: '+str(e)
    for name in ('controller_pose_command.json',run+'.writing'):
        path=directory/name
        try:
            if path.exists() and json.loads(path.read_text()).get('_probe_run')==run:path.unlink()
        except (OSError,ValueError):pass
    r.close()
report={'pid':a.pid,'sha256':symbols['sha256'],'duration':a.duration,'followup_duration':a.followup_duration,'head':head,'controllers':original,
        'rows':rows,'error':error,'game_memory_writes':False,'focus_changed':False}
active=[v for v in rows if v['phase'] in ('pull','first_glide','recovery','followup_ready','followup_pull','glide','stopped')]
if active:
    first=next((v for v in active if v['input']['forward']>.1),None)
    complete=next((v for v in active if v['strokes']>before['strokes']),None)
    report['summary']={'strokes':active[-1]['strokes']-before['strokes'],
        'onset_progress':first['progress'] if first else None,'onset_phase':first['phase'] if first else None,
        'lead_before_completion_ms':(complete['t']-first['t'])*1000 if first and complete else None,
        'boost_requested':any(v['input']['boost'] for v in active),
        'fast_feedback_seen':any(v['fast']==1 for v in active),'automatic_samples':sum(v['automatic'] for v in active),
        'forward_peak':max(v['input']['forward'] for v in active),'stopped_forward':active[-1]['input']['forward']}
    if beforeFollowup:
        normalIntent=next((v for v in rows if v['phase']=='followup_pull' and v['input']['forward']>.9
                           and not v['input']['boost']),None)
        normal=next((v for v in rows if v['phase']=='followup_pull' and v['input']['forward']>.9
                     and not v['input']['boost'] and v['fast']==0),None)
        report['summary']['followup']={
            'fast_at_start':beforeFollowup['fast'],
            'strokes':active[-1]['strokes']-beforeFollowup['strokes'],
            'normal_feedback_while_moving':normal is not None,
            'normal_intent_to_feedback_ms':(normal['t']-normalIntent['t'])*1000 if normal and normalIntent else None,
            'normal_before_previous_glide_expired_ms':(complete['t']+1.15-normal['t'])*1000 if normal and complete else None}
a.out.write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report.get('summary',{'error':error}),indent=2))
if error:raise RuntimeError(error)
