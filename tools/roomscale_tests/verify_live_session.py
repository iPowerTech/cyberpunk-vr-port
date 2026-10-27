"""Verify and index a saved native roomscale live session; no live control."""
import argparse
import base64
import hashlib
import json
import math
from pathlib import Path

from live_read import decode

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pid', type=int, required=True)
parser.add_argument('--day', required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
if not args.out.parent.is_dir() or args.out.exists():
    raise RuntimeError('Fresh output under existing parent required')

def load(label):
    path = root / f'build/roomscale-live-{args.pid}-{label}-{args.day}.json'
    payload = path.read_bytes()
    data = json.loads(payload)
    assert data['pid'] == args.pid
    frames = data.get('samples', [])
    if label == 'step':
        frames = [data[k] for k in ('game_before', 'game_shifted', 'game_held', 'game_restored')]
    for frame in frames:
        raw = decode(base64.b64decode(frame['raw_base64'], validate=True))
        assert frame['publication_sequence'] % 2 == 0
        for key in ('calls', 'injected', 'feedback_reads', 'player', 'movement', 'backend', 'origin', 'reason', 'gates'):
            assert frame[key] == raw[key], (label, key)
        for key in ('native', 'requested', 'before', 'after', 'velocity', 'consumed', 'feedback'):
            assert tuple(frame[key]) == raw[key] and all(map(math.isfinite, frame[key])), (label, key)
    return data, {'path': str(path.relative_to(root)), 'sha256':hashlib.sha256(payload).hexdigest(),
                  'verified_snapshots':len(frames)}

index = {'date':args.day, 'pid':args.pid, 'dll_sha256':'ce9e3baf13f8347f236d08305ad92af700440bb7e10eecbba6f58adaa7c9c348',
         'exe_base':'0x7ff72f870000', 'plugin_base':'0x7ffa3ec60000',
         'debug_address':'0x7ffa3ef041b0', 'sequence_address':'0x7ffa3ef04258',
         'tests':{}, 'not_accepted':{}}
step, artifact = load('step')
b, moved, held, returned = (step[k] for k in ('game_before', 'game_shifted', 'game_held', 'game_restored'))
assert all(r['gates']==15 for r in (b,moved,held,returned))
assert moved['after'] == held['after'] and held['velocity'] == [0,0,0]
assert returned['after'] == b['after']
assert moved['injected']-b['injected'] == 1 and returned['injected']-b['injected'] == 2
travel = math.dist(b['after'][:2],moved['after'][:2])
assert abs(travel-.02) < .001
assert all(step[k]['session_state']=='VISIBLE' for k in ('initial','shifted','restored'))
index['tests']['step'] = dict(artifact, planar_travel=travel, exact_return=True, held_drift=0, background_visible=True)

for label in ('right','forward','vertical','turnwalk-flat','animation-turnwalk','wall','mixed-dense'):
    data, artifact = load(label)
    assert data.get('checks') and all(data['checks'].values()), label
    assert not data.get('error') and not data.get('cleanup_error') and not data.get('keyboard_error'), label
    rows = data['samples']
    held_rows = [r for r in rows if r['label']=='held']
    tail = held_rows[len(held_rows)//2:]
    assert len(tail)>10
    assert max(math.dist(tail[0]['after'][:2],r['after'][:2]) for r in tail)<.001
    assert max(math.hypot(*r['velocity'][:2]) for r in tail)<.01
    assert tail[0]['injected']==tail[-1]['injected']
    entry = dict(artifact, summary=data['summary'], checks=data['checks'],
                 scripted_focus_changes=data['focus_changed'], scripted_keys=data['keys_sent'],
                 session_states=[data['initial_status']['session_state'],data['final_status']['session_state']])
    if label in ('right','forward','turnwalk-flat','animation-turnwalk'):
        assert data['summary']['free_space_error']<.001
        assert data['summary']['return_error']<.001
    if label=='vertical':
        assert data['baseline']['after']==data['reached']['after']
        assert data['baseline']['injected']==data['reached']['injected']
    if label=='animation-turnwalk':
        assert data['summary']['move_xy_peak']==0
        assert data['summary']['animation_direction_mean'][1]>.9
        assert abs(data['summary']['final_head_body_yaw_gap_degrees'])<5.05
        entry['animation_boundary']='Live AnimFeature_PlayerMovement inputs confirmed; final leg pose/video cadence not certified.'
    if label=='wall':
        assert data['summary']['blocked_requested_distance']>.1
        assert abs(data['summary']['consumed'][1]-.5)<.001
        assert abs(data['summary']['retreat_distance']-.5)<.001
    if label=='mixed-dense':
        unique={r['calls'] for r in rows if math.hypot(*r['native'][:2])>.0001 and math.hypot(*r['requested'][:2])>.0001}
        assert len(unique)>1
        entry['unique_mixed_cct_calls']=len(unique)
        entry['keyboard_input_id']='f22e5aafe45c4cc3bc16d6de83e3b3ce'
    index['tests'][label]=entry

recenter, artifact=load('recenter')
assert all(recenter['checks'].values())
a,z=recenter['before_recenter'],recenter['after_recenter']
assert a['after']==z['after'] and a['injected']==z['injected']
assert z['origin']==a['origin']+1 and z['consumed']==[0,0]
assert abs(z['entity_yaw']-a['entity_yaw'])<.001 and z['realign']==0
index['tests']['recenter']=dict(artifact, checks=recenter['checks'], old_origin=a['origin'],new_origin=z['origin'],
    capsule_delta=[0,0,0], tracking_basis_change_degrees=math.degrees(z['yaw']-a['yaw']),
    boundary='Capsule/body continuity checked; rendered eye-camera continuity is not measured.')

for label, reason in (
    ('turnwalk','Trajectory crossed a 16.7cm height drop; extra native XY and 19.4cm return difference. Not accepted as a free-space/stair test; no code cause established.'),
    ('mixed','0.18s W pulse exceeded the harness 0.7m total-travel bound; original pose restored and key-up confirmed.'),
    ('mixed-short','0.08s W was received, but sampling caught no same-tick native+physical pair; superseded by dense sampling.')):
    data, artifact=load(label)
    index['not_accepted'][label]=dict(artifact,reason=reason,summary=data.get('summary'),error=data.get('error'))

index['remaining']=[
    'Controlled stairs/steep-slope and moving-platform attribution.',
    'Quantitative rendered MAIN/VRCAM camera and hand-anchor continuity.',
    'Vehicle/menu/save-load transition live coverage.',
    'Automatic physical crouch-to-capsule/PSM integration is not implemented.'
]
index['production_changes_this_session']=False
args.out.write_text(json.dumps(index,indent=2)+'\n',encoding='utf-8')
print(f"PASS {len(index['tests'])} live scenarios; {sum(x['verified_snapshots'] for x in index['tests'].values())} validated diagnostic snapshots")
print(f"Retained {len(index['not_accepted'])} non-accepted probes with explicit boundaries")
print(args.out)
