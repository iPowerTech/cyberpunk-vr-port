"""Summarize read-only camera history observations; never claim same-invocation timing."""
import argparse
from collections import deque
import json
import math
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('trace',type=Path)
parser.add_argument('--turn',type=Path)
parser.add_argument('--manual-x',type=float,default=-.012)
parser.add_argument('--manual-y',type=float,default=.174)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
if args.out.exists() or not args.out.parent.is_dir():raise ValueError('Fresh output under existing parent required')
data=json.loads(args.trace.read_text(encoding='utf-8'))
phases={}; history=deque(maxlen=80)
for row in data['samples']:
    for eye in ('main','vrcam'):
        history.append(tuple(round(v*131072) for v in row[eye]))
    observation=row['render_observation']
    if not observation['stable']:continue
    result=phases.setdefault(row['phase'],{'stable_observations':0,'orientation_matches':0,
        'orientation_ties':0,'max_ties':0,'max_candidate_translation_span_m':0,
        'render_position_exact_in_recent_component_history':0})
    result['stable_observations']+=1
    if tuple(observation['position_fp']) in history:
        result['render_position_exact_in_recent_component_history']+=1
    matches=[entry for entry in observation['entries'] if entry['q']==observation['q']]
    if not matches:continue
    result['orientation_matches']+=1
    result['orientation_ties']+=len(matches)>1
    result['max_ties']=max(result['max_ties'],len(matches))
    span=max(math.dist(a['position'],b['position']) for a in matches for b in matches)
    result['max_candidate_translation_span_m']=max(result['max_candidate_translation_span_m'],span)
summary={'pid':data['pid'],'trace':str(args.trace),'phases':phases,
    'boundary':'These are asynchronous stable observations. Candidate span is ambiguity in the old orientation-only key, not measured reprojection error or proof of post-fix visual quality.'}
if args.turn:
    turn=json.loads(args.turn.read_text(encoding='utf-8'))
    before,after=turn['before'],turn['after']
    body_turn=after['entity_yaw']-before['entity_yaw']
    # Each endpoint's tracking-to-body yaw; the residual manual offset is
    # R(-realign)*manual. This also handles a nonzero initial realignment.
    def rotate(yaw):
        return [math.cos(yaw)*args.manual_x-math.sin(yaw)*args.manual_y,
                math.sin(yaw)*args.manual_x+math.cos(yaw)*args.manual_y]
    predicted=[b-a for a,b in zip(rotate(-before['realign']),rotate(-after['realign']))]
    observed=[b-a for a,b in zip(turn['summary']['camera_minus_head_body_local_before'],
                               turn['summary']['camera_minus_head_body_local_after'])]
    summary['turn']={'trace':str(args.turn),'manual_xy_m':[args.manual_x,args.manual_y],
        'body_turn_degrees':math.degrees(body_turn),'cct_shift_m':turn['summary']['cct_shift'],
        'predicted_wrong_basis_xy_m':predicted,'observed_body_local_delta_m':observed,
        'prediction_error_xy_m':math.dist(predicted,observed[:2]),
        'predicted_shift_m':math.hypot(*predicted),'observed_shift_m':math.hypot(*observed)}
args.out.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
