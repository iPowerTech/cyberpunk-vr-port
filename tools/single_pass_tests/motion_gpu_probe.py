"""Compare actual native eye draws with the common GPU draw while the HMD moves."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid',type=int,required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--late-visibility',action='store_true',help='Exercise automatic group matching and eye-exclusive visibility')
p.add_argument('--scene-depth',action='store_true',help='Include full-resolution native depth and target contents')
p.add_argument('--scene-route',action='store_true',help='Route the matched group into the visible scene')
p.add_argument('--direct-resolve',action='store_true',help='Resolve directly into the native eye targets')
p.add_argument('--mixed-materials',action='store_true',help='Include adjacent validated material variants')
p.add_argument('--depth-prepass',action='store_true',help='Compare the opaque depth-only group')
p.add_argument('--start-indices',type=int,default=0,help='Select a later depth group by its source mesh index count')
p.add_argument('--depth-run',type=int,default=0)
p.add_argument('--case',choices=('both','slow','fast'),default='both',help='Run one case when the bounded diagnostic heap has room for only one more capture')
args=p.parse_args();root=Path(__file__).resolve().parents[2]
sim=Path(os.environ['LOCALAPPDATA'])/'OpenXR-Simulator'
state=json.loads((sim/'runtime_status.json').read_text())['head_tracking']
original={**state['position'],**{k:state[k] for k in ('yaw','pitch','roll')}}
target=sim/'head_pose_command.json';temporary=sim/'head_pose_command.single-pass-gpu.tmp'
args.out.mkdir(parents=True,exist_ok=True)
stop=threading.Event();errors=[];results=[];parameters={'speed':.7,'jitter':.002}

def write(pose):
    temporary.write_text(json.dumps(pose))
    for attempt in range(100):
        try:os.replace(temporary,target);return
        except PermissionError:
            if attempt==99:raise
            time.sleep(.002)

def feed():
    started=time.perf_counter()
    try:
        while not stop.is_set():
            t=time.perf_counter()-started;v=parameters.copy();angle=t*v['speed'];j=v['jitter']
            write({**original,'yaw':original['yaw']+math.radians(5)*math.sin(angle),
                'pitch':original['pitch']+math.radians(3)*math.sin(angle*.73),
                'roll':original['roll']+math.radians(2)*math.sin(angle*1.17),
                'x':original['x']+j*math.sin(t*13),'y':original['y']+j*.5*math.sin(t*17),
                'z':original['z']+j*.8*math.sin(t*11)})
            stop.wait(.016)
    except Exception as error:errors.append(str(error))

thread=threading.Thread(target=feed);thread.start()
try:
    for name,values in [('slow',{'speed':.7,'jitter':.002}),('fast',{'speed':24,'jitter':.007})]:
        if args.case!='both' and args.case!=name:continue
        parameters=values;time.sleep(.4);path=args.out/f'{name}.json'
        extra=['--group','--late-visibility','--prepare-gate-ms','50'] if args.late_visibility or args.scene_depth or args.scene_route or args.direct_resolve or args.depth_prepass else []
        if args.scene_depth or args.scene_route or args.direct_resolve or args.depth_prepass:extra.append('--scene-depth')
        if args.scene_route or args.direct_resolve:extra.append('--scene-route')
        if args.direct_resolve:extra.append('--direct-resolve')
        if args.mixed_materials:extra.append('--mixed-materials')
        if args.depth_prepass:extra.append('--depth-prepass')
        if args.start_indices:extra+=['--start-indices',str(args.start_indices)]
        if args.depth_run:extra+=['--depth-run',str(args.depth_run)]
        completed=subprocess.run([sys.executable,'-B',str(root/'tools/single_pass_tests/read_gpu_probe.py'),
            '--pid',str(args.pid),'--native-reference','--rearm-completed','--out',str(path),*extra],capture_output=True,text=True)
        result=json.loads(path.read_text()) if path.exists() else {'error':completed.stderr or completed.stdout}
        result.update(case=name,returncode=completed.returncode);results.append(result);print(json.dumps({k:v for k,v in result.items() if k!='differenceSamples'}),flush=True)
        if completed.returncode:break
finally:
    stop.set();thread.join();write(original)
    deadline=time.monotonic()+3
    while target.exists() and time.monotonic()<deadline:time.sleep(.01)
    (args.out/'summary.json').write_text(json.dumps({'pid':args.pid,'originalPose':original,
        'restoreCommandConsumed':not target.exists(),'feedErrors':errors,'cases':results},indent=2))
    assert not target.exists(),'The simulator did not consume the restore command'
    assert not errors,errors
assert len(results)==(2 if args.case=='both' else 1) and all(r['returncode']==0 for r in results),results
