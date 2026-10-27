"""Compare recorded hand packets and MODEL-stage bones; no live access or writes."""
import argparse
import json
import math
from pathlib import Path
import statistics

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('trace',type=Path)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--hand-rate',type=float,default=40)
args=parser.parse_args()
if args.out.exists() or not args.out.parent.is_dir():raise ValueError('Fresh output required')
trace=json.loads(args.trace.read_text(encoding='utf-8'))
def peak_step(values):
    return max((math.dist(a,b)*1000 for a,b in zip(values,values[1:])),default=0)
summary={'pid':trace['pid'],'source':str(args.trace),'phases':{},
    'boundary':'Asynchronous observations. MODEL-stage rows require a stable bone buffer and agreement with the separately published head; other stages are counted, not interpreted as model coordinates. These metrics do not establish visual acceptance.'}
for phase in dict.fromkeys(row['phase'] for row in trace['samples']):
    all_rows=[r for r in trace['samples'] if r['phase']==phase]
    stable=[r for r in all_rows if r['bone_buffer']['stable'] and r['hand_packet']['stable'] and r['stable_epoch']]
    rows=[r for r in stable if math.dist(r['head'],r['bone_buffer']['positions']['head'])<.1]
    result={'samples':len(all_rows),'stable_model_samples':len(rows),'other_stage_samples':len(stable)-len(rows),
        'cct_travel_m':math.dist(all_rows[0]['after'],all_rows[-1]['after'])}
    for side in ('right','left'):
        result[side+'_input_step_mm']=peak_step([r['hand_packet'][side] for r in rows])
        result[side+'_bone_step_mm']=peak_step([r['bone_buffer']['positions'][side+'_hand'] for r in rows])
    result['head_bone_step_mm']=peak_step([r['bone_buffer']['positions']['head'] for r in rows])
    for key in ('hand_publish_counters','placed_pose_counters'):
        if key in all_rows[0]:result[key]=[b-a for a,b in zip(all_rows[0][key],all_rows[-1][key])]
    matching=[r for r in rows if r.get('stable_pose_reads') and r['frame_pose'].get('sequence')==r['pose_sequence']]
    if matching:result['hand_reference_vs_frame_mm']={'samples':len(matching),
        'max':max(math.dist(r['hand_packet']['head_base'],r['frame_pose']['position'])*1000 for r in matching)}
    summary['phases'][phase]=result

# Coefficient check is only valid for the controlled translation-only simulator
# fixture: right controller stays at head+(0.2,-0.3,-0.4), no rotation.
packets={}
for row in trace['samples']:
    if row['hand_packet']['stable'] and row.get('stable_pose_reads') and 'stamp_ms' in row['hand_packet']:
        if math.dist(row['hand_packet']['head_base'],row['frame_pose']['position'])<1e-6:
            packets.setdefault(row['hand_packet']['sequence'],row)
coefficients=[]
rows=sorted(packets.values(),key=lambda r:r['hand_packet']['sequence'])
for before,after in zip(rows,rows[1:]):
    if trace.get('axis','x')!='x':continue
    a,b=before['hand_packet'],after['hand_packet']
    if before['phase']!=after['phase'] or after['phase'] not in ('hmd_out','hmd_back') or b['sequence']-a['sequence']!=2:continue
    if any(abs(v)>1e-6 for v in a['hmd_q'][:3]+b['hmd_q'][:3]):continue
    dt=((b['stamp_ms']-a['stamp_ms'])%100000)*.001
    previous=a['head_base'][0]+a['right'][0]
    current=b['head_base'][0]+b['right'][0]
    target=after['raw_roomscale']['head'][0]+.2
    raw_ahead=((after['raw_roomscale']['stamp_us']/1000-b['stamp_ms']+50000)%100000)-50000
    if abs(target-previous)<.004 or raw_ahead>.5 or not 0<dt<.25:continue
    observed=(current-previous)/(target-previous)
    coefficients.append({'phase':after['phase'],'call':after['calls'],'dt_ms':dt*1000,
        'observed':observed,'linear':min(1,args.hand_rate*dt),'exponential':-math.expm1(-args.hand_rate*dt)})
summary['filter_coefficient']={'rate':args.hand_rate,'samples':coefficients}
if coefficients:
    for form in ('linear','exponential'):
        errors=[abs(p['observed']-p[form]) for p in coefficients]
        summary['filter_coefficient'][form+'_error']={'median':statistics.median(errors),'max':max(errors)}
args.out.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
