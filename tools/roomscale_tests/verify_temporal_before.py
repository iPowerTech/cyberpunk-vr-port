"""Check saved evidence for raw-CCT versus rendered-frame HMD disagreement."""
import argparse
import base64
import hashlib
import json
import math
from pathlib import Path

from live_read import decode

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
if args.out.exists() or not args.out.parent.is_dir():raise RuntimeError('Fresh output required')
result={'pid':19332,'dll_before':'3bcdd9ee38376d40c3d0dc9abeec3a042fabdac10b2cd0383e11b7e3c5db6200',
        'boundary':'Each seqlock packet is coherent; observations across independent packets are not presented as one engine invocation.',
        'traces':{}}
for kind,name in (('hmd','roomscale-body-temporal-19332-hmd-fast-pose-before.json'),
                  ('wasd','roomscale-body-temporal-19332-wasd-before.json')):
    path=root/'build'/name;raw=path.read_bytes();data=json.loads(raw)
    assert data['pid']==19332 and not data.get('error') and not data.get('cleanup_error')
    for r in data['samples']:
        d=decode(base64.b64decode(r['raw_base64'],validate=True))
        assert r['publication_sequence']%2==0
        assert tuple(r['after'])==d['after'] and r['pose_sequence']==d['pose_sequence']
    moving=[r for r in data['samples'] if r['phase'] in ('hmd_out','hmd_back','wasd') and
            r['stable_epoch'] and r['stable_used'] and r['stable_body']]
    assert len(moving)>20 and all(r['gates']==15 for r in moving)
    assert moving[-1]['used'][1]==moving[0]['used'][1], 'Source switching must not be blamed without evidence'
    item={'path':str(path.relative_to(root)),'sha256':hashlib.sha256(raw).hexdigest(),
          'validated_snapshots':len(data['samples']),'stable_moving_samples':len(moving),
          'lua_source_uses_during_motion':0,
          'camera_model_x_range_m':max(r['cam_model'][0] for r in moving)-min(r['cam_model'][0] for r in moving),
          'hips_model_x_range_m':max(r['hips'][0] for r in moving)-min(r['hips'][0] for r in moving),
          'cct_peak_speed_mps':max(math.hypot(*r['velocity'][:2]) for r in moving)}
    if kind=='hmd':
        pose_rows=[r for r in moving if r.get('stable_pose_reads')]
        def gap(r):return math.dist(r['raw_roomscale']['head'],[r['frame_pose']['position'][0],-r['frame_pose']['position'][2]])
        item['raw_vs_frame_peak_m']=max(gap(r) for r in pose_rows)
        consumed=[r for r in pose_rows if r['raw_roomscale']['seq']==r['pose_sequence'] and
                  math.dist(r['raw_roomscale']['head'],r['consumed'])<.0001]
        assert consumed,'Need actual consumed runtime sample matches'
        item['confirmed_consumed_raw_samples']=len(consumed)
        item['consumed_raw_vs_frame_peak_m']=max(gap(r) for r in consumed)
        assert item['raw_vs_frame_peak_m']>.07 and item['consumed_raw_vs_frame_peak_m']>.04
        assert item['camera_model_x_range_m']>.07
    else:
        helpers=data['input_helpers']
        for helper in helpers:
            log=json.loads(helper['stdout']);assert helper['code']==0
            assert log['restored_original_focus'] and not log['release_errors']
        assert json.loads(helpers[0]['stdout'])['keys']==['D']
        assert json.loads(helpers[-1]['stdout'])['release_all']
        assert item['camera_model_x_range_m']<.002
    result['traces'][kind]=item
result['status']='Before-correction input mismatch verified; corrected DLL needs the same fast live test.'
args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
