"""Separate manual offsets, model bake and physical residue in a saved turn."""
import argparse
import json
import math
from pathlib import Path

def rotate(v,a):
    c,s=math.cos(a),math.sin(a)
    return [c*v[0]-s*v[1],s*v[0]+c*v[1],v[2]]
def add(a,b):return [x+y for x,y in zip(a,b)]
def sub(a,b):return [x-y for x,y in zip(a,b)]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('report',type=Path);args=p.parse_args()
data=json.loads(args.report.read_text(encoding='utf-8'));out={}
for key in ('before','after','returned'):
    r=data[key];s=r['shared'];b=add(s['cam_bake'],s['eye_bake_model'])
    manual=sub(s['total_offset'],b)
    raw=[s['head_base'][0],-s['head_base'][2],s['head_base'][1]]
    residue=sub(raw,[*r['consumed'],0])
    observed=sub(r['centre'],r['engine_camera'])
    old=add(rotate(add(residue,manual),r['yaw']),rotate(b,r['entity_yaw']))
    new=add(rotate(residue,r['yaw']),rotate(add(b,manual),r['entity_yaw']))
    out[key]={'manual_offset':manual,'physical_residue':residue,'bake':s['cam_bake'],'eye_bake':s['eye_bake_model'],
              'entity_yaw':r['entity_yaw'],'tracking_yaw':r['yaw'],'realign':r['realign'],
              'observed_delta':observed,'current_formula_error':math.dist(observed,old),
              'predicted_camera_only_correction':sub(new,old),
              'body_local_head_offset':rotate(sub(r['centre'],r['head_world']),-r['entity_yaw']),
              'predicted_body_local_head_offset':rotate(sub(add(r['centre'],sub(new,old)),r['head_world']),-r['entity_yaw'])}
out['predicted_shift']=math.dist(out['before']['predicted_body_local_head_offset'],out['after']['predicted_body_local_head_offset'])
print(json.dumps(out,indent=2))
