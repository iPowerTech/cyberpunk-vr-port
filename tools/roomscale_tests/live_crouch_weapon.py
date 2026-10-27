"""Bounded weapon/crouch pose capture; commands come from a local test file."""
import argparse,json,time,struct
from pathlib import Path
from live_physical_crouch import CrouchProbe
from live_symbols import resolve

class WeaponProbe(CrouchProbe):
    def __init__(self,a):
        super().__init__(a)
        names=['g_hasWeaponEquipped','g_VRRightBoneIdx','g_VRLeftBoneIdx','g_VRFKSnapPos','g_VRFKSnapCount']
        d=resolve(a.pid,a.dll.resolve(),names)
        self.addr.update({k:int(d[k],16) for k in names})
    def capture(self):
        r=super().capture()
        if r:
            r['weapon_equipped']=bool(self.r.read(self.addr['g_hasWeaponEquipped'],1)[0])
            count=self.get('g_VRFKSnapCount','<i')
            r['hands_model']=[]
            for side in ('Right','Left'):
                i=self.get('g_VR'+side+'BoneIdx','<i')
                r['hands_model'].append(self.read(self.addr['g_VRFKSnapPos']+i*12,'<3f') if 0<=i<count else None)
        return r
    def protocol(self):
        self.posture(phase='standing_initial')
        if self.latest['crouch_actual'] and not self.latest['crouch_applied']:
            self.pad.write(self.pad.controllers[1]+36,struct.pack('<2f',0,-1));self.delay(.15)
            self.pad.write(self.pad.controllers[1]+36,struct.pack('<2f',0,0));self.delay(.9)
        command=self.a.out.parent/'command.json';status=self.a.out.parent/'status.json'
        last=None;end=time.monotonic()+210
        while time.monotonic()<end:
            if command.exists():
                try:c=json.loads(command.read_text())
                except (json.JSONDecodeError,OSError):self.delay(.02);continue
                if c.get('id')!=last:
                    last=c['id'];self.phase=c['phase']
                    if c.get('stop'):return
                    if 'drop' in c:self.posture(drop=c['drop'],pitch=c.get('pitch',0),phase=c['phase'])
                    self.delay(c.get('hold',.9));self.checkpoint(c['phase'])
                    status.write_text(json.dumps({'id':last,'sample':self.latest},indent=2))
            self.delay(.03)
        raise RuntimeError('Bounded weapon test expired')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    for k in ('dll','types','out','simulator'):p.add_argument('--'+k,type=Path,required=True)
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
    a=p.parse_args();a.batch_runtime=None;a.case='crouch_weapon';a.poses='low_ready';a.version='crouch_weapon'
    assert not a.out.exists();WeaponProbe(a).run()
