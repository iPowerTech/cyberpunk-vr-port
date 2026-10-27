"""Coherent simulator trajectories through the actual native body motion path."""
import argparse,json,math,time
from pathlib import Path
from live_matrix import Probe,D,wrap

class PredictionProbe(Probe):
    def custom_suite(self):
        poses=('low_ready',) if self.a.quick else ('rest','low_ready','rifle','crossed','hands_high','one_hand_high')
        for pose in poses:
            self.prepare(pose)
            for direction in (-1,1):
                label=f'{pose}/{direction}/continuous'
                self.phase=label;start=len(self.rows)
                self.sweep(pose,0,direction*360*D,180,label,jitter=0)
                self.phase=label+'/stop';self.pose(pose,direction*360*D,jitter=0);self.hold(.8)
                records=self.rows[start:]
                moving=[r for r in records if r['phase']==label]
                # Simulator commands have their actual transport times recorded.
                # This is a native end-to-end error, not an assumed 90Hz latency.
                peak=max(abs(wrap((r['offset_deg']-r['expected_deg'])*D))/D for r in moving)
                settled=abs(wrap((records[-1]['offset_deg']-direction*360)*D))/D
                stop=[r for r in records if r['phase']==label+'/stop']
                over=max(wrap((r['offset_deg']-direction*360)*D)*direction/D for r in stop)
                result={'kind':'continuous_native_turn','pose':pose,'direction':direction,
                    'peak_error_deg':peak,'settled_error_deg':settled,'stop_overshoot_deg':max(0,over),
                    'samples':len(records),'pass':peak<25 and settled<2 and over<3.2}
                self.results.append(result);print(json.dumps(result),flush=True)
                # A 360-degree tracking rotation is again neutral, including all
                # three device orientations. Return the command label to zero.
                self.pose(pose,0,jitter=0);self.hold(.3)
        self.prepare('low_ready')
        for kind in ('acceleration','oscillation'):
            self.phase=kind;begin=len(self.rows);start=time.monotonic();body=0
            while (t:=time.monotonic()-start)<4:
                body=(90*math.sin(t*2.0) if kind=='oscillation' else (90*t*t if t<1 else 90+180*(t-1)))*D
                self.pose('low_ready',body,body,-35*D,jitter=0)
            self.phase=kind+'/stop';self.pose('low_ready',body,body,-35*D,jitter=0);self.hold(.8)
            settled=abs(wrap(self.rows[-1]['offset_deg']*D-body))/D
            result={'kind':kind,'settled_error_deg':settled,'samples':len(self.rows)-begin,'pass':settled<2}
            self.results.append(result);print(json.dumps(result),flush=True)
            # A gentle return prevents an artificial whole-tracking reset.
            self.sweep('low_ready',body,0,90,kind+'/return',jitter=0);self.hold(.6)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('dll','types','out','batch-runtime'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--pid',type=int,required=True)
    p.add_argument('--quick',action='store_true',help='One representative posture, both turn directions, acceleration and reversal')
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
    a=p.parse_args();a.case='prediction';a.poses=None
    assert a.out.parent.is_dir() and not a.out.exists()
    PredictionProbe(a).run()
