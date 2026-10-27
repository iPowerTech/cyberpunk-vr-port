"""Compare captured mouse/HMD turns and test the coordinate-frame hypothesis."""
import argparse
import hashlib
import json
import math
from pathlib import Path

def rotate(p, yaw):
    co,si=math.cos(yaw),math.sin(yaw)
    return [co*p[0]-si*p[1],si*p[0]+co*p[1],p[2]]

def add(a,b): return [x+y for x,y in zip(a,b)]
def sub(a,b): return [x-y for x,y in zip(a,b)]

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mouse',type=Path,required=True)
p.add_argument('--hmd',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
args=p.parse_args()
if not args.out.parent.is_dir() or args.out.exists():raise RuntimeError('Fresh output with existing parent required')
result={'captures':{},'hypothesis':'eye_bake_model is being rotated with tracking yaw (entity-realign), not the entity/model frame that produced it.',
        'boundary':'The alternate-frame prediction is offline only. It is not a post-fix measurement.'}
for kind,path in (('mouse',args.mouse),('hmd',args.hmd)):
    data=json.loads(path.read_text(encoding='utf-8'))
    part={'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
          'summary':data['summary'],'endpoints':{}}
    for key in ('before','after'):
        r=data[key]; s=r['shared']
        body_bake=add(s['cam_bake'],s['eye_bake_model'])
        applied=sub(r['centre'],r['engine_camera'])
        prediction=rotate(body_bake,r['yaw'])
        # These captures deliberately have no physical translation or manual offset.
        assert math.hypot(*s['head_base'])<1e-4 and math.hypot(*s['cam_bake'])<1e-4
        error=math.dist(applied,prediction)
        assert error < .001,(kind,key,error)
        correction=sub(rotate(body_bake,r['entity_yaw']),prediction)
        corrected=add(r['centre'],correction)
        part['endpoints'][key]={'applied_world_offset':applied,'model_offset':body_bake,
            'tracking_yaw':r['yaw'],'entity_yaw':r['entity_yaw'],'realign':r['realign'],
            'old_formula_error_m':error,'predicted_frame_correction':correction,
            'camera_minus_head_body_local':rotate(sub(r['centre'],r['head_world']),-r['entity_yaw']),
            'predicted_camera_minus_head_body_local':rotate(sub(corrected,r['head_world']),-r['entity_yaw'])}
    b,a=(part['endpoints'][k] for k in ('before','after'))
    part['observed_body_relative_shift_m']=math.dist(b['camera_minus_head_body_local'],a['camera_minus_head_body_local'])
    part['predicted_shift_after_frame_fix_m']=math.dist(b['predicted_camera_minus_head_body_local'],a['predicted_camera_minus_head_body_local'])
    result['captures'][kind]=part
mouse,hmd=(result['captures'][k] for k in ('mouse','hmd'))
assert mouse['observed_body_relative_shift_m']<.001
assert hmd['observed_body_relative_shift_m']>.035
assert hmd['predicted_shift_after_frame_fix_m']<.001
args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
