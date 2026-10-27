"""Short physical crouch/native movement/camera probe, using simulator input only."""
import argparse,json,math,statistics,time,struct
from pathlib import Path
from body_center_probe import CentreProbe
from live_symbols import resolve

class CrouchProbe(CentreProbe):
    def __init__(self,a):
        super().__init__(a)
        names=['CyberpunkVR_PhysicalCrouchDepth','CyberpunkVR_PhysicalCrouchDesired',
               'CyberpunkVR_PhysicalCrouchApplied','CyberpunkVR_PhysicalCrouchActual']
        v=resolve(a.pid,a.dll.resolve(),names)
        self.addr.update({n:int(v[n],16) for n in names})
    def capture(self):
        r=super().capture()
        if r:
            for key in ('Depth','Desired','Applied','Actual'):
                r['crouch_'+key.lower()]=self.get('CyberpunkVR_PhysicalCrouch'+key,'<f' if key=='Depth' else '<i')
        return r
    def posture(self,drop=0,forward=0,pitch=0,phase='posture'):
        self.phase=phase
        self.current_head={**self.original_head,'y':self.original_head['y']-drop,
                           'z':self.original_head['z']-forward,'pitch':math.radians(-pitch)}
        self.head(self.current_head);self.delay(1.1)
        self.checkpoint(phase)
    def check_state(self,expected):
        r=self.latest
        assert r['crouch_desired']==expected and r['crouch_applied']==expected and r['crouch_actual']==expected,{
            'phase':self.phase,**{k:v for k,v in r.items() if k.startswith('crouch_')}}
    def walk(self,label):
        self.phase=label;start=len(self.rows);self.pad.stick(y=.5)
        try:self.delay(.5)
        finally:self.pad.stick()
        rows=self.rows[start:];self.delay(.35)
        speeds=[math.hypot(*r['velocity'][:2]) for r in rows[len(rows)//2:] if 'velocity' in r]
        speed=statistics.median(speeds)
        heights=[r['entity_position'][2] for r in rows if 'entity_position' in r]
        assert heights and max(heights)-min(heights)<.07,('Uneven or unsupported test surface',label)
        if label=='crouched_walk':assert all(r['crouch_actual']==1 for r in rows), 'Crouch exited during movement'
        assert speed>.1,('No native movement',label,speed)
        return speed
    def protocol(self):
        self.settings(xr_physical_crouch=1,xr_physical_crouch_depth=.25,xr_movement_source=0,xr_physical_body_rotation=0,xr_tracked_body_rotation=0,xr_hybrid_body_rotation=0)
        for h in self.original_hands:self.hand(h)
        self.posture(phase='standing')
        if self.latest['crouch_actual'] and not self.latest['crouch_applied']:
            # Save files can retain manual crouch. Clear it through the normal
            # pad action, never by overriding the game's stance/capsule.
            self.phase='clear_saved_manual_crouch'
            self.pad.write(self.pad.controllers[1]+36,struct.pack('<2f',0,-1))
            self.delay(.15)
            self.pad.write(self.pad.controllers[1]+36,struct.pack('<2f',0,0))
            self.delay(1)
        self.check_state(0)
        self.stand_z=self.latest['camera_position'][2]
        standing=self.walk('standing_walk')
        self.posture(phase='standing_after_walk');self.stand_z=self.latest['camera_position'][2]
        self.posture(drop=.4,phase='crouch');self.check_state(1)
        camera_drop=self.stand_z-self.latest['camera_position'][2]
        crouched=self.walk('crouched_walk')
        assert crouched<standing*.85,('Native crouch did not slow movement',standing,crouched)
        self.posture(phase='stand_up');self.check_state(0)
        self.posture(drop=.4,pitch=35,phase='crouch_looking_down');self.check_state(1)
        self.posture(phase='stand_again');self.check_state(0)
        # Slow hinge so physical displacement is classified, not a teleport.
        for i in range(31):
            angle=math.radians(40*i/30)
            self.head({**self.original_head,'y':self.original_head['y']+.63*(math.cos(angle)-1),
                       'z':self.original_head['z']-.63*math.sin(angle)})
            self.delay(.025)
        self.phase_hold('pure_bend',.8);self.check_state(0)
        self.posture(phase='reset');self.check_state(0)
        self.posture(drop=.4,phase='disable_while_crouched');self.check_state(1)
        self.settings(xr_physical_crouch=0);self.delay(.8);self.check_state(0)
        self.posture(phase='final_standing')
        self.summary={'standing_speed_m_s':standing,'crouch_speed_m_s':crouched,
                      'speed_ratio':crouched/standing,'camera_drop_m':camera_drop,
                      'camera_expected_drop_m':.4,'camera_extra_drop_m':camera_drop-.4}
        print(json.dumps(self.summary),flush=True)
        assert abs(camera_drop-.4)<.025,('Physical crouch double-lowered camera',self.summary)
    def run(self):
        try:super().run()
        finally:
            if hasattr(self,'summary'):
                p=self.a.out;j=json.loads(p.read_text());j['crouch_summary']=self.summary
                p.write_text(json.dumps(j,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    for k in ('dll','types','out','simulator'):p.add_argument('--'+k,type=Path,required=True)
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
    a=p.parse_args();a.batch_runtime=None;a.case='physical_crouch';a.poses='low_ready';a.version='physical_crouch'
    assert not a.out.exists();CrouchProbe(a).run()
