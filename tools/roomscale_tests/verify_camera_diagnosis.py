"""Verify the captured pre-fix camera/body and simulator-reset evidence."""
import hashlib
import json
import math
from pathlib import Path

root=Path(__file__).resolve().parents[2]
paths={
    'turn':root/'build/roomscale-live-9388-head-vs-camera-before-2-20260919.json',
    'reset':root/'build/roomscale-live-9388-runtime-reset-before-20260919.json'}
reports={k:json.loads(p.read_text(encoding='utf-8')) for k,p in paths.items()}
turn=reports['turn']
assert turn['summary']['cct_shift']==0
assert turn['summary']['camera_centre_shift']<.0002
assert turn['summary']['head_world_shift']>.04
errors=[]
for stage in ('before','after'):
    sample=turn[stage]; body=sample['body']; q=body['rot']; h=body['hips']
    yaw=2*math.atan2(q['k'],q['r'])
    hips_world_xy=[math.cos(yaw)*h['x']-math.sin(yaw)*h['y'],
                   math.sin(yaw)*h['x']+math.cos(yaw)*h['y']]
    eye_offset=[sample['main'][i]-sample['centre'][i] for i in range(2)]
    error=math.dist(hips_world_xy,eye_offset)
    assert error<.0002,(stage,error,hips_world_xy,eye_offset)
    errors.append(error)
reset=reports['reset']
assert reset['reset_command']['id']==1402
assert reset['status_after_reset']['head_tracking']['position']=={'x':0.0,'y':1.7,'z':0.0}
assert reset['before']['origin']==reset['after']['origin']
assert reset['summary']['cct_shift']>.12
assert reset['summary']['camera_centre_shift']>.27
summary={
    'pid':9388,'pre_fix_dll':'ce9e3baf13f8347f236d08305ad92af700440bb7e10eecbba6f58adaa7c9c348',
    'hip_vs_main_eye_offset_error_m':errors,
    'turn':turn['summary'],'runtime_reset':reset['summary'],
    'reset_origin_unchanged':reset['before']['origin'],
    'artifacts':{k:{'path':str(p.relative_to(root)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for k,p in paths.items()},
    'acceptance':'Verified pre-fix causes. New DLL still requires live before/after validation.'}
out=root/'docs/roomscale-camera-causes-20260919.json'
out.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print('PASS: hips follow MAIN eye offset within',max(errors),'metres; runtime reset was treated as motion')
print(out)
