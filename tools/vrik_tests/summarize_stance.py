"""Export named local/model bone tables from a captured stance, without a game."""
from pathlib import Path
import argparse,json,struct,math,hashlib
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--armed',action='store_true');args=p.parse_args();root=args.capture
def load(name):return json.loads((root/name).read_text())
def floats(blob,width):return list(struct.iter_unpack('<'+str(width)+'f',bytes.fromhex(blob['data'])))
def mul(a,b):
 x,y,z,w=a;X,Y,Z,W=b;return [w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z]
def rotate(q,v):return mul(mul(q,[*v,0]),[-q[0],-q[1],-q[2],q[3]])[:3]
def normalize(q):
 n=math.sqrt(sum(x*x for x in q));return [x/n for x in q] if n>1e-8 else [0,0,0,1]
def angle(a,b):return math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(normalize(a),normalize(b)))))))
meta=load('pair-01-before.json');n=meta['context']['count'];fields=meta['fields'];names={int(x.split('\t',1)[0]):x.split('\t',1)[1] for x in (root/'bone-names.txt').read_text().splitlines()[1:]};assert len(names)==n
parents=struct.unpack('<'+str(n)+'h',bytes.fromhex(fields['g_VRBoneParent']['data']));owned=list(bytes.fromhex(fields['g_VRUpperOwned']['data']));shadows=struct.unpack('<'+str(n)+'i',bytes.fromhex(fields['g_VRShadowSource']['data']))
def fk(local):
 P=[];Q=[]
 for i,row in enumerate(local):
  parent=parents[i];assert parent<i
  q=Q[parent] if parent>=0 else [0,0,0,1];pos=P[parent] if parent>=0 else [0,0,0]
  P.append([a+b for a,b in zip(pos,rotate(q,row[:3]))]);Q.append(normalize(mul(q,row[4:8])))
 return list(zip(P,Q))
pairs=[]
for number in (2,3):
 before=load(f'pair-{number:02d}-before.json');after=load(f'pair-{number:02d}-after.json')
 assert before['context']['rsp']==after['context']['rsp'] and before['context']['bone_buffer']==after['context']['bone_buffer']
 assert before['sequence']['data']==after['sequence']['data'] and before['rig_revision']['data']==after['rig_revision']['data']
 assert before['hands']['data'][:160]==after['hands']['data'][:160], 'hand/head inputs changed inside solve'
 assert before['debugger_pause_ms']<200
 localBefore=floats(before['bones'],12);localAfter=floats(after['bones'],12);modelBefore=fk(localBefore);modelAfter=fk(localAfter)
 table=[];changes=[]
 for i in range(n):
  bp,ap=localBefore[i],localAfter[i]
  delta={'translation_mm':math.dist(bp[:3],ap[:3])*1000,'rotation_deg':angle(bp[4:8],ap[4:8]),'scale_max_delta':max(abs(a-b) for a,b in zip(bp[8:11],ap[8:11]))}
  if delta['translation_mm']>.001 or delta['rotation_deg']>.01 or delta['scale_max_delta']>1e-5:changes.append({'index':i,'name':names[i],**delta})
  table.append({'index':i,'name':names[i],'parent':parents[i],'parent_name':names.get(parents[i]),'owned':bool(owned[i]),'shadow_source':shadows[i],
    'before_local':bp,'after_local':ap,'before_model':modelBefore[i],'after_model':modelAfter[i],'solver_delta':delta})
 (root/f'pair-{number:02d}-named-bones.json').write_text(json.dumps(table,indent=2)+'\n')
 (root/f'pair-{number:02d}-before.bin').write_bytes(bytes.fromhex(before['bones']['data']));(root/f'pair-{number:02d}-after.bin').write_bytes(bytes.fromhex(after['bones']['data']))
 pairs.append({'pair':number,'debugger_pause_ms':before['debugger_pause_ms'],'same_rsp_and_buffer':True,'same_hand_sequence':True,'same_controller_head_input':True,'same_rig_revision':True,'changed_bones':changes})
stream=load('solved-model-stream.json');assert all(bool(f['armed'])==args.armed for f in stream['frames'])
payload=(root/'solved-model-stream.bin').read_bytes();variability=[]
for i in range(n):
 if not owned[i] and shadows[i]<0:continue
 poses=[struct.unpack_from('<3f',payload,row['offset']+i*12) for row in stream['frames']]
 rotations=[struct.unpack_from('<4f',payload,row['offset']+n*12+i*16) for row in stream['frames']]
 variability.append({'index':i,'name':names[i],'max_position_from_first_mm':max(math.dist(poses[0],x)*1000 for x in poses),'max_rotation_from_first_deg':max(angle(rotations[0],x) for x in rotations)})
summary={'pid':meta['pid'],'state':'armed' if args.armed else 'unarmed','bone_count':n,'paired_solves':pairs,'stream_frames':len(stream['frames']),'owned_count':stream['owned_count'],'variability':variability,'fk_convention':'Matches production VRIK position/quaternion FK; local QsTransform scales are recorded separately, not folded into these model transforms','pair_01_usage':'Static rig metadata only; timed before/after pairs02/03 are the comparison baseline','sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in root.iterdir() if f.is_file() and f.name!='summary.json'}}
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({'bones':n,'paired_solves':len(pairs),'stream_frames':summary['stream_frames'],'changed_bones_per_solve':[len(x['changed_bones']) for x in pairs],'largest_idle_variations':sorted(variability,key=lambda x:x['max_position_from_first_mm'],reverse=True)[:6]},indent=2))
