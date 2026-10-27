"""Summarize paired-camera old/new runs without conflating raw-HMD latency."""
import argparse,json,math,statistics
from pathlib import Path


def wrap(value):return math.remainder(value,360)
def mean_angle(values):
    values=list(values)
    return math.degrees(math.atan2(sum(math.sin(math.radians(v)) for v in values),sum(math.cos(math.radians(v)) for v in values)))
def quantile(values,p):
    values=sorted(values)
    return values[min(len(values)-1,int((len(values)-1)*p))] if values else None
def stats(values):
    values=list(values)
    return dict(n=len(values),mean=statistics.mean(values),p95=quantile(values,.95),p99=quantile(values,.99),max=max(values)) if values else {'n':0}
def unique(rows):
    out={}
    for row in rows:out[(row['phase'],row['camera_sequence'])]=row
    return list(out.values())
def end_rows(rows,phase):
    rows=[r for r in rows if r['phase']==phase]
    return [r for r in rows if r['t']>=rows[-1]['t']-.25] if rows else []


def analyze(path):
    report=json.loads(path.read_text())
    assert not report['error'] and not report['restore_errors'],(report['error'],report['restore_errors'])
    rows=[json.loads(line) for line in Path(report['raw_file']).read_text().splitlines()]
    frames=unique(rows);paired=[r for r in frames if r['paired']]
    baseline=end_rows(paired,'baseline');assert baseline
    reference=mean_angle(r['base_yaw_deg'] for r in baseline)
    normal=[r for r in paired if r['phase'].startswith(('upright/','catchup/'))]
    normal_all=[r for r in frames if r['phase'].startswith(('upright/','catchup/'))]
    delta=[abs(wrap(r['base_yaw_deg']-reference)) for r in normal]
    baseline_relative=[statistics.mean(r['camera_relative_entity'][k] for r in baseline) for k in range(3)]
    baseline_head=[statistics.mean(r['head_position'][k] for r in baseline) for k in range(3)]
    baseline_camera=[statistics.mean(r['camera_position'][k] for r in baseline) for k in range(3)]
    baseline_entity=[statistics.mean(r['entity_position'][k] for r in baseline) for k in range(3)]
    translation=[math.dist(r['camera_relative_entity'][:2],baseline_relative[:2])*1000 for r in normal if 'camera_relative_entity' in r]
    hmd_translation=[math.dist(r['head_position'],baseline_head)*1000 for r in normal]
    result={'version':report['version'],'dll_sha256':report['symbols']['sha256'],'sample_count':len(rows),
            'sampled_camera_frames':len(frames),'paired_camera_frames':len(paired),'paired_fraction':len(paired)/len(frames),
            'reference_yaw_deg':reference,'normal_extra_yaw_deg':stats(delta),
            'normal_pairing_fraction':len(normal)/len(normal_all),
            'normal_camera_world_xy_mm':stats(math.dist(r['camera_position'][:2],baseline_camera[:2])*1000 for r in normal_all),
            'normal_entity_world_xy_mm':stats(math.dist(r['entity_position'][:2],baseline_entity[:2])*1000 for r in normal_all if 'entity_position' in r),
            'normal_camera_xy_relative_entity_mm':stats(translation),'normal_hmd_position_change_mm':stats(hmd_translation),
            'pairing_quat_distance':stats(r.get('pairing_quat_distance',0) for r in paired),
            'native_camera_yaw_accumulator_peak_deg':max(abs(r['camera_input'][1]) for r in normal if 'camera_input' in r),
            'initial_entity_position':baseline[-1]['entity_position'],'movement':[],'per_sweep':{}}
    unpaired=[r for r in normal_all if not r['paired']]
    result['normal_unpaired_count']=len(unpaired)
    result['normal_unpaired_nearest_distance']=stats(r['nearest_record_quat_distance'] for r in unpaired if 'nearest_record_quat_distance' in r)
    for speed in (30,90,360):
        selected=[r for r in normal if r['phase'].startswith(f'upright/{speed}/')]
        result['per_sweep'][str(speed)]=stats(abs(wrap(r['base_yaw_deg']-reference)) for r in selected)
    for sign in (-1,1):
        held=end_rows(paired,f'counterlook/{sign}/held')
        moving=[r for r in rows if r['phase']==f'forward/{sign}']
        angles=[];requests=[];velocities=[]
        for r in moving:
            body=math.radians(r['body_yaw_deg'])
            for name,destination in (('velocity',angles),('native_request',requests)):
                if name not in r:continue
                x,y=r[name][:2]
                if math.hypot(x,y)<(.25 if name=='velocity' else .002):continue
                side=math.cos(body)*x+math.sin(body)*y
                forward=-math.sin(body)*x+math.cos(body)*y
                destination.append(math.degrees(math.atan2(side,forward)))
            if 'velocity' in r:velocities.append(math.hypot(*r['velocity'][:2]))
        item={'sign':sign,'held_body_offset_deg':statistics.mean(r['offset_deg'] for r in held) if held else None,
              'velocity_direction_from_body_deg':mean_angle(angles) if angles else None,
              'native_request_direction_from_body_deg':mean_angle(requests) if requests else None,
              'velocity_samples':len(angles),'request_samples':len(requests),'speed_m_s':stats(velocities)}
        if len(moving)>1:
            dx,dy=[moving[-1]['entity_position'][i]-moving[0]['entity_position'][i] for i in range(2)]
            item['displacement_m']=[dx,dy]
        result['movement'].append(item)
    before=end_rows(paired,'switch/before');off=end_rows(paired,'switch/off')
    assert before and off,'Missing paired feature-toggle measurements'
    result['disable_feature_extra_yaw_deg']=wrap(mean_angle(r['base_yaw_deg'] for r in off)-mean_angle(r['base_yaw_deg'] for r in before))
    result['body_offset_before_disable_deg']=statistics.mean(r['offset_deg'] for r in before)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('reports',nargs='+',type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    result=[analyze(path) for path in a.reports]
    a.out.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
