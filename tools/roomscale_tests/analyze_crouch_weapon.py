"""Check camera-base continuity and settled wrist positions in a weapon trace."""
import argparse,json,math,statistics
from pathlib import Path

def analyze(path):
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    phases=('standing_unarmed','crouch_unarmed','crouch_pistol','crouch_slot2',
            'crouch_katana','standing_katana','crouch_katana_enter')
    def base(r):return [a-b for a,b in zip(r['native_camera_base'],r['entity_position'])]
    initial=[r for r in rows if r['phase']=='standing_unarmed' and 'entity_position' in r]
    reference=[statistics.median(base(r)[k] for r in initial[-20:]) for k in range(3)]
    result={'reference_native_camera_model_m':reference,'phases':{}}
    neutral_hands=None
    for phase in phases:
        rr=[r for r in rows if r['phase']==phase and 'entity_position' in r]
        if not rr:raise RuntimeError('Missing phase: '+phase)
        errors=[math.dist(base(r),reference)*1000 for r in rr]
        tail=rr[-20:]
        hands=[[statistics.median(r['hands_model'][h][k] for r in tail) for k in range(3)] for h in range(2)]
        if phase=='crouch_unarmed':neutral_hands=hands
        item={'samples':len(rr),'max_native_camera_shift_mm':max(errors),'settled_hands_model_m':hands,
              'actual_crouch':tail[-1]['crouch_actual'],'weapon_equipped':tail[-1]['weapon_equipped']}
        if phase.startswith('crouch_') and neutral_hands:
            item['settled_wrist_shift_mm']=[math.dist(hands[h],neutral_hands[h])*1000 for h in range(2)]
        result['phases'][phase]=item
        assert max(errors)<1,('Camera shifted',phase,max(errors))
        assert item['actual_crouch']==int(phase.startswith('crouch_')),('Wrong stance',phase)
        if phase in ('crouch_pistol','crouch_slot2','crouch_katana'):
            assert item['weapon_equipped'],('Weapon not drawn',phase)
            assert max(item['settled_wrist_shift_mm'])<5,('Wrist shifted',phase,item)
    result['pass']=True
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();r=analyze(a.trace);a.out.write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
