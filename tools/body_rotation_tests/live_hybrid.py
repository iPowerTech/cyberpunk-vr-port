"""Exercise actual hybrid routing through the coherent simulator test channel."""
import argparse,json,math,struct,time
from pathlib import Path
from live_matrix import Probe,D,wrap,resolve

class HybridProbe(Probe):
 def __init__(self,args):
  super().__init__(args)
  address=resolve(args.pid,args.dll.resolve(),['s_hybridBody'])
  self.hybrid_address=int(address['s_hybridBody'],16)
  self.phase_offset=self.types['cvr::body::HybridBodyYaw']['fields']['tracked']['offset']
  self.mode_address=self.addr['g_liveControls']+self.types['LiveControls']['fields']['xrBodyRotationMode']['offset']
  self.head_address=self.instance+self.types['OpenXRManager']['fields']['m_publishedHandHead']['offset']
  self.head_type=self.types['OpenXRHeadPose']
 def sample(self):
  row=super().sample()
  row['hybrid_tracked']=struct.unpack('<?',self.r.read(self.hybrid_address+self.phase_offset,1))[0]
  row['body_mode']=struct.unpack('<i',self.r.read(self.mode_address,4))[0]
  data=self.r.read(self.head_address,self.head_type['size']);fields=self.head_type['fields']
  q=[struct.unpack_from('<f',data,fields['ori'+k]['offset'])[0] for k in ('X','Y','Z','W')]
  x,y,z,w=q;n=x*x+y*y+z*z+w*w
  row['head_down_deg']=math.asin(max(-1,min(1,2*(y*z-w*x)/n)))/D if n>.5 else None
  off=fields['bodyBend']['offset']+self.types['cvr::body::BendSample']['fields']['angle']['offset']
  row['bend_deg']=struct.unpack_from('<f',data,off)[0]/D
  row['published_head_position']=[struct.unpack_from('<f',data,fields['pos'+k]['offset'])[0] for k in ('X','Y','Z')]
  row['published_head_rotation']=q
  if 'cvr::body::BendTracker' in self.types:
   desc=self.types['cvr::body::BendTracker'];address=self.instance+self.types['OpenXRManager']['fields']['m_bodyBendTracker']['offset']
   bend_data=self.r.read(address,desc['size'])
   row['bend_tracker']={name:struct.unpack_from('<3f' if name in ('m_head','m_lever','m_neck') else '<f',bend_data,desc['fields'][name]['offset']) for name in ('m_head','m_lever','m_neck','m_right','m_forward','m_drop')}
  return row
 def phase_is(self,tracked):
  self.hold(.2);r=self.sample()
  assert r['body_mode']==3 and r['active']==1,r
  assert r['hybrid_tracked']==tracked and r['cone']==(0 if tracked else 10),r
  return r
 def motion(self,label,pose,body0,body1,head0,head1,down0=0,down1=0,seconds=.55,shift=None):
  self.phase=label;begin=time.monotonic()
  if not self.batch_runtime:
   # The normal simulator cannot publish three devices atomically. Exercise
   # settled phases with small steps, and allow each complete tuple to settle;
   # do not treat these samples as a continuous hardware motion/latency test.
   steps=max(1,math.ceil(max(abs(body1-body0),abs(head1-head0),abs(down1-down0))/3),math.ceil(seconds/.12))
   for i in range(steps+1):
    u=i/steps;v=u*u*(3-2*u)
    self.pose(pose,(body0+(body1-body0)*v)*D,(head0+(head1-head0)*v)*D,
        -(down0+(down1-down0)*v)*D,head_shift=shift(v) if shift else None)
    self.hold(.12)
   return
  while True:
   u=min(1,(time.monotonic()-begin)/seconds)
   v=u*u*(3-2*u)
   self.pose(pose,(body0+(body1-body0)*v)*D,(head0+(head1-head0)*v)*D,
       -(down0+(down1-down0)*v)*D,head_shift=shift(v) if shift else None)
   if u>=1:break
 def record(self,kind,pose,**values):
  item=dict(kind=kind,pose=pose,**values);self.results.append(item);print(json.dumps(item),flush=True)
 def custom_suite(self):
  assert 'xr_hybrid_body_rotation=1' in self.ini.read_text()
  self.mode(0) # explicit tracked-only off; installed hybrid remains on
  for pose in (('rest',) if self.a.quick else ('rest','rifle','right_pistol','crossed')):
   for sign in (-1,1):
    self.phase=pose+'/neutral';self.pose(pose,0,0,jitter=0);self.hold(.7);self.phase_is(False)
    baseline=self.get('CyberpunkVR_BodyYawRealignRad')/D
    inside=baseline+sign*6
    self.motion(pose+'/inside',pose,0,0,0,inside);self.hold(.3)
    inside_error=abs(wrap((self.get('CyberpunkVR_BodyYawRealignRad')/D-baseline)*D))/D
    assert inside_error<1,('inside cone',pose,inside_error)
    outside=baseline+sign*25
    self.motion(pose+'/outside',pose,0,0,inside,outside);self.hold(.7);self.phase_is(False)
    before=self.get('CyberpunkVR_BodyYawRealignRad')/D
    assert abs(wrap((outside-before)*D))/D<10.5,('head follow',pose,outside,before)
    self.motion(pose+'/down',pose,0,0,outside,outside,0,30);self.phase_is(True)
    handover=abs(wrap((self.get('CyberpunkVR_BodyYawRealignRad')/D-before)*D))/D
    glance=outside+sign*30
    self.motion(pose+'/down_glance',pose,0,0,outside,glance,30,30);self.hold(.35)
    head_only=abs(wrap((self.get('CyberpunkVR_BodyYawRealignRad')/D-before)*D))/D
    self.motion(pose+'/body_turn',pose,0,sign*30,glance,glance+sign*30,30,30,seconds=.8)
    self.hold(.5);self.phase_is(True)
    turn_error=abs(wrap((self.get('CyberpunkVR_BodyYawRealignRad')/D-before-sign*30)*D))/D
    self.motion(pose+'/up',pose,sign*30,sign*30,glance+sign*30,glance+sign*30,30,0)
    self.hold(.8);self.phase_is(False)
    residual=abs(wrap((self.get('CyberpunkVR_BodyYawRealignRad')/D-glance-sign*30)*D))/D
    self.record('hybrid_cycle',pose,direction=sign,inside_error_deg=inside_error,handover_deg=handover,
        down_head_only_deg=head_only,tracked_turn_error_deg=turn_error,head_cone_residual_deg=residual,
        **{'pass':handover<2 and head_only<2 and turn_error<2 and residual<10.5})
  # A physical hinge with a level HMD tests the separate bend cue, not pitch.
  for axis in ('forward','right'):
   pose='low_ready';self.phase=axis+'/neutral';self.pose(pose,0,0,jitter=0);self.hold(.8);self.phase_is(False)
   shift=lambda v:[.63*math.sin(40*D*v) if axis=='right' else 0,.63*(math.cos(40*D*v)-1),-.63*math.sin(40*D*v) if axis=='forward' else 0]
   self.motion(axis+'/lean',pose,0,0,0,0,seconds=1.2,shift=shift);r=self.phase_is(True)
   peak_bend=r['bend_deg'];assert r['head_down_deg'] is not None and abs(r['head_down_deg'])<1
   self.motion(axis+'/stand',pose,0,0,0,0,seconds=1.2,shift=lambda v:shift(1-v));self.hold(.5);self.phase_is(False)
   self.record('level_head_bend',axis,bend_deg=peak_bend,**{'pass':peak_bend>=5})
  self.phase='boundary/neutral';self.pose('low_ready',0,0,jitter=0);self.hold(.6);self.phase_is(False)
  self.pose('low_ready',0,0,-10.8*D);self.phase_is(True)
  self.phase='boundary/jitter';start=len(self.rows);begin=time.monotonic()
  while (t:=time.monotonic()-begin)<1.2:
   self.pose('low_ready',0,0,-(9+.6*math.sin(t*20))*D)
   if not self.batch_runtime:self.hold(.12)
  stable=all(r['hybrid_tracked'] and r['cone']==0 for r in self.rows[start:])
  self.pose('low_ready',0,0,-7.5*D);self.phase_is(False)
  self.record('pitch_hysteresis','low_ready',**{'pass':stable})

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for flag in ('dll','types','out'):p.add_argument('--'+flag,type=Path,required=True)
 p.add_argument('--batch-runtime',type=Path)
 p.add_argument('--pid',type=int,required=True)
 p.add_argument('--quick',action='store_true',help='One posture for routing, both hinge directions and pitch hysteresis')
 p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
 a=p.parse_args();a.case='hybrid';a.poses=None;assert not a.out.exists();HybridProbe(a).run()
