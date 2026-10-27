"""Analyze asynchronous body telemetry without treating it as a single-call trace."""
import argparse
import json
import math
from pathlib import Path
import statistics


def inverse_rotate(q,v):
    a,b,c,w=-q[0],-q[1],-q[2],q[3];x,y,z=v
    tx,ty,tz=2*(b*z-c*y),2*(c*x-a*z),2*(a*y-b*x)
    return [x+w*tx+b*tz-c*ty,y+w*ty+c*tx-a*tz,z+w*tz+a*ty-b*tx]


def summarize(rows):
    stable=[r for r in rows if r['stable_body'] and r['stable_used'] and r['stable_epoch']]
    # Take the last stable sample of an epoch, after the usual pose work. This
    # reduces partial-publication bias but is not claimed to identify a GPU frame.
    by_epoch={r['epoch']:r for r in stable}; chosen=list(by_epoch.values())
    out={'samples':len(rows),'stable_epochs':len(chosen)}
    if not chosen:return out
    for key in ('hips','head','cam_model','head_delta'):
        values=[r[key] for r in chosen]
        out[key]={'mean':list(map(statistics.mean,zip(*values))),
                  'range':[max(v[i] for v in values)-min(v[i] for v in values) for i in range(3)],
                  'max_xy_step':max((math.dist(a[:2],b[:2]) for a,b in zip(values,values[1:])),default=0)}
    errors={'native':[],'lua':[]}
    for r in chosen:
        for source in errors:
            paired=r[source]
            target=inverse_rotate(paired['entity_q'],[paired['span'][i]-r['head_delta'][i] for i in range(3)])
            errors[source].append(math.dist(r['hips'][:2],target[:2]))
    out['hip_error_vs_pair_minus_latest_delta']={k:{'median':statistics.median(v),'max':max(v)} for k,v in errors.items()}
    if all('body_span' in r['native'] for r in chosen):
        paired=[math.dist(r['hips'][:2],inverse_rotate(r['native']['entity_q'],r['native']['body_span'])[:2]) for r in chosen]
        out['hip_error_vs_paired_body_base']={'median':statistics.median(paired),'max':max(paired)}
    out['head_delta_xy_peak_to_peak']=max(math.dist(a['head_delta'][:2],b['head_delta'][:2]) for a in chosen for b in chosen)
    out['source_counts_delta']=[chosen[-1]['used'][i]-chosen[0]['used'][i] for i in range(2)]
    out['cct_travel']=math.dist(chosen[0]['after'],chosen[-1]['after'])
    out['cct_z_range']=max(r['after'][2] for r in chosen)-min(r['after'][2] for r in chosen)
    out['planar_cct_speed_max']=max(math.hypot(*r['velocity'][:2]) for r in chosen)
    out['native_pair_model_xy_range']=[max(inverse_rotate(r['native']['entity_q'],r['native']['span'])[i] for r in chosen)-
        min(inverse_rotate(r['native']['entity_q'],r['native']['span'])[i] for r in chosen) for i in range(2)]
    out['gates']=sorted({r['gates'] for r in chosen})
    poses=[r for r in chosen if r.get('stable_pose_reads')]
    if poses:
        diff=[math.dist(r['raw_roomscale']['head'],[r['frame_pose']['position'][0],-r['frame_pose']['position'][2]]) for r in poses]
        consumed_diff=[math.dist(r['raw_roomscale']['head'],r['consumed']) for r in poses]
        out['raw_head_vs_render_frame_m']={'max':max(diff),'median':statistics.median(diff)}
        out['raw_head_vs_consumed_m']={'max':max(consumed_diff),'median':statistics.median(consumed_diff)}
        matched=[r for r in poses if r['frame_pose'].get('sequence')==r['pose_sequence']]
        if matched:
            errors=[math.dist([r['frame_pose']['position'][0],-r['frame_pose']['position'][2]],r['consumed']) for r in matched]
            out['matched_frame_vs_consumed_m']={'samples':len(matched),'max':max(errors),'median':statistics.median(errors)}
    out['largest_hip_steps']=[]
    for a,b in sorted(zip(chosen,chosen[1:]),key=lambda pair:math.dist(pair[0]['hips'][:2],pair[1]['hips'][:2]),reverse=True)[:5]:
        out['largest_hip_steps'].append({'epoch':b['epoch'],'step':math.dist(a['hips'][:2],b['hips'][:2]),
            'hips_before':a['hips'],'hips_after':b['hips'],'head_delta':b['head_delta'],'cam_model':b['cam_model'],
            'used_delta':[b['used'][i]-a['used'][i] for i in range(2)],'native_span':b['native']['span'],'lua_span':b['lua']['span']})
    return out


parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('report',type=Path)
parser.add_argument('--out',type=Path)
parser.add_argument('--brief',action='store_true')
args=parser.parse_args()
data=json.loads(args.report.read_text(encoding='utf-8'))
phases={}
for row in data['samples']:phases.setdefault(row['phase'],[]).append(row)
summary={'pid':data['pid'],'mode':data['mode'],'phases':{name:summarize(rows) for name,rows in phases.items()}}
if args.out:
    if args.out.exists() or not args.out.parent.is_dir():raise ValueError('Fresh output required')
    args.out.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
if args.brief:
    for phase in summary['phases'].values():phase.pop('largest_hip_steps',None)
print(json.dumps(summary,indent=2))
