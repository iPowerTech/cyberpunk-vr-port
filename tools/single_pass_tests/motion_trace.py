"""Bounded simulator motion / early-camera comparison; restore the original pose."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pid',type=int,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--shader',action='store_true',help='Also require exact eligible shader-camera predictions')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
sim=Path(os.environ['LOCALAPPDATA'])/'OpenXR-Simulator'
status=json.loads((sim/'runtime_status.json').read_text())['head_tracking']
original={**status['position'],**{k:status[k] for k in ('yaw','pitch','roll')}}
target=sim/'head_pose_command.json';temporary=sim/'head_pose_command.single-pass.tmp'
args.out.mkdir(parents=True,exist_ok=True)
stop=threading.Event();errors=[];results=[]
case={'yaw':0,'pitch':0,'roll':0,'speed':1,'position':0}

def write(pose):
    temporary.write_text(json.dumps(pose))
    for attempt in range(100):
        try:os.replace(temporary,target);return
        except PermissionError:
            if attempt==99:raise
            time.sleep(.002)

def feed():
    start=time.perf_counter()
    try:
        while not stop.is_set():
            t=time.perf_counter()-start;c=case.copy();angle=t*c['speed']
            write({**original,
                'yaw':original['yaw']+math.radians(c['yaw'])*math.sin(angle),
                'pitch':original['pitch']+math.radians(c['pitch'])*math.sin(angle*.73),
                'roll':original['roll']+math.radians(c['roll'])*math.sin(angle*1.17),
                'x':original['x']+c['position']*math.sin(t*13),
                'y':original['y']+c['position']*.5*math.sin(t*17),
                'z':original['z']+c['position']*.8*math.sin(t*11)})
            stop.wait(.016)
    except Exception as error:errors.append(str(error))

thread=threading.Thread(target=feed);thread.start()
try:
    cases=[('slow_yaw',dict(yaw=5,speed=.7)),('fast_yaw',dict(yaw=20,speed=5)),
           ('pitch_roll',dict(pitch=15,roll=12,speed=2)),
           ('position_jitter',dict(position=.007,speed=2)),
           ('combined_slow',dict(yaw=10,pitch=8,roll=5,position=.005,speed=1)),
           ('combined_fast',dict(yaw=20,pitch=15,roll=10,position=.01,speed=5))]
    for name,values in cases:
        case={'yaw':0,'pitch':0,'roll':0,'speed':1,'position':0,**values}
        time.sleep(.35)
        for repeat in range(2):
            path=args.out/f'{name}-{repeat}.json'
            subprocess.run([sys.executable,'-B',str(root/'tools/single_pass_tests/read_trace.py'),
                '--pid',str(args.pid),'--frames','2','--out',str(path)],check=True,capture_output=True,text=True)
            subprocess.run([sys.executable,'-B',str(root/'tools/single_pass_tests/analyze_peer_camera.py'),str(path)],
                check=True,capture_output=True,text=True)
            comparison=json.loads(path.with_name(path.stem+'-peer-comparison.json').read_text())
            matches=[r['best'] for r in comparison['rows']]
            exact=sum(bool(m) and m['positionExact'] and m['quatExact'] and m['jitterExact'] and m['matrixWordsDifferent']==0 for m in matches)
            result={'case':name,'repeat':repeat,'predictions':len(matches),'exact':exact,
                'maxRelativeError':max((m['maxRelativeError'] for m in matches if m),default=None)}
            shaders=[r['shaderBest'] for r in comparison['rows'] if r.get('shaderBest')]
            result.update(shaderPredictions=len(shaders),shaderExact=sum(s['wordsDifferent']==0 for s in shaders))
            results.append(result);print(json.dumps(result),flush=True)
finally:
    stop.set();thread.join();write(original)
    deadline=time.monotonic()+3
    while target.exists() and time.monotonic()<deadline:time.sleep(.01)
    report={'pid':args.pid,'originalPose':original,'restoreCommandConsumed':not target.exists(),
            'feedErrors':errors,'cases':results}
    (args.out/'summary.json').write_text(json.dumps(report,indent=2))
    assert not target.exists(),'Restore command was not consumed'
    assert not errors,errors
    print('Original HMD pose restored',flush=True)
assert len(results)==12 and all(r['predictions'] and r['exact']==r['predictions'] for r in results),results
if args.shader:
    assert sum(r['shaderPredictions'] for r in results)>0 and all(r['shaderExact']==r['shaderPredictions'] for r in results),results
