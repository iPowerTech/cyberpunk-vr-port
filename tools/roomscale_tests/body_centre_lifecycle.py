"""Verify the fixed optical centre across native stick turning and recenter."""
import argparse,json,math,time,statistics
from pathlib import Path
from body_center_probe import CentreProbe
from live_symbols import resolve

class LifecycleProbe(CentreProbe):
    def __init__(self,a):
        super().__init__(a)
        v=resolve(a.pid,a.dll.resolve(),['g_pendingSnapYawDeltaBits'])
        self.addr['g_pendingSnapYawDeltaBits']=int(v['g_pendingSnapYawDeltaBits'],16)
    def protocol(self):
        import struct
        self.settings(xr_physical_body_rotation=0,xr_tracked_body_rotation=0,xr_hybrid_body_rotation=1,xr_movement_source=0)
        for c in self.original_hands:self.hand(c)
        self.head_at(0);self.phase_hold('baseline',1)
        self.head_sweep(0,60,90,'physical_turn');self.phase_hold('physical_hold',.8)
        self.phase='native_snap'
        self.write(self.addr['g_pendingSnapYawDeltaBits'],struct.pack('<f',30))
        self.delay(.8);self.checkpoint('native_snap')
        self.phase='recenter'
        self.settings(xr_recenter=1);self.delay(.8);self.checkpoint('recenter')
        self.settings(xr_recenter=0)
        self.phase='mode_off';self.settings(xr_hybrid_body_rotation=0);self.delay(.5)
        self.checkpoint('mode_off')
        self.phase='mode_on';self.settings(xr_hybrid_body_rotation=1);self.delay(.5)
        self.checkpoint('mode_on')
        self.phase='stationary_final';self.delay(.5)
    def run(self):
        super().run()
        rows=[json.loads(s) for s in self.a.out.with_suffix('.jsonl').read_text().splitlines()]
        base=[r for r in rows if r['phase']=='baseline'][-12:]
        p0=[statistics.mean(r['camera_position'][k] for r in base) for k in range(3)]
        tested=[r for r in rows if r['phase'] not in ('startup','restore') and not r['phase'].startswith('baseline')]
        maximum=max(math.dist(r['camera_position'][:2],p0[:2])*1000 for r in tested)
        before_snap=[r for r in rows if r['phase']=='physical_hold'][-1]
        after_snap=[r for r in rows if r['phase']=='native_snap'][-1]
        snap_delta=(after_snap['body_yaw_deg']-before_snap['body_yaw_deg']+180)%360-180
        summary={'max_centre_xy_shift_mm':maximum,'samples':len(tested),'pass':maximum<.25,
                 'native_snap_delta_deg':snap_delta,'native_snap_exercised':abs(snap_delta)>5,
                 'verified_scope':['physical_follow','recenter','mode_off_on']}
        report=json.loads(self.a.out.read_text());report['centre_lifecycle']=summary
        self.a.out.write_text(json.dumps(report,indent=2));print(json.dumps(summary),flush=True)
        if not summary['pass']:raise RuntimeError('Native turn/recenter shifted the optical centre')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    for name in ('dll','types','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
    p.add_argument('--simulator',type=Path,required=True)
    a=p.parse_args();a.batch_runtime=None;a.case='centre_lifecycle';a.poses='low_ready';a.version='after'
    assert a.out.parent.is_dir() and not a.out.exists()
    LifecycleProbe(a).run()
