"""Body-only native regression through Simulator poses/inputs; no focus or keys.

Run against an exactly matching DLL/PDB with a neutral simulator and a loaded
save. Restores controller input, poses, diagnostics and settings in finally.
Movement cases need an open area; this harness never saves the game.
"""
import argparse, ctypes as ct, json, math, statistics, struct, sys, time
from pathlib import Path

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / p)
               for p in ('body_rotation_tests', 'swimming_tests')]
from live_matrix import Probe, D, wrap, mul
from live_symbols import resolve


def controller_rvas(dll):
    data = dll.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, size = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    sections = [struct.unpack_from('<4I', data, pe+24+size+i*40+8) for i in range(count)]
    result = []
    for x in (-.2, .2):
        pattern = struct.pack('<5f?', x, -.3, -.4, 0, -.3, True)
        at = data.find(pattern)
        assert at >= 0 and data.find(pattern, at+1) < 0, 'Simulator controller layout is not unique'
        result.append(next(va+at-raw for _, va, length, raw in sections if raw <= at < raw+length))
    return result


class NativeProbe(Probe):
    def __init__(self, a):
        super().__init__(a)
        names = ['CyberpunkVR_RoomscaleDebug', 'CyberpunkVR_RuntimeDiagnostics', 'g_viewYawUsedRad',
                 'g_telemetry', 'CyberpunkVR_BodyHeadingCommits', 'CyberpunkVR_BodyHeadingRequests',
                 'CyberpunkVR_BodyHeadingHooksReady', 'CyberpunkVR_DebugBodyHeadingSkip', 'g_VRLocomotionState',
                 'g_lcf','g_lcfSeq','g_pendingSnapYawDeltaBits']
        extra = resolve(a.pid, a.dll.resolve(), names)
        self.symbols.update(extra)
        self.addr.update({k: int(extra[k], 16) for k in names})
        sim = resolve(a.pid, a.simulator.resolve(), [])
        self.simulator = sim
        self.controllers = [int(sim['module_base'], 16)+v for v in controller_rvas(a.simulator)]
        # Validate the loaded layout against actual poses, not just disk constants.
        for h, address in enumerate(self.controllers):
            values = struct.unpack('<5f7?', self.r.read(address, 27))
            assert values[5] and max(abs(x-y) for x,y in zip(values[:3], ((-.2 if h==0 else .2),-.3,-.4))) < .002
        self.k = self.r.k
        self.k.WriteProcessMemory.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_void_p, ct.c_size_t, ct.POINTER(ct.c_size_t)]
        self.writer = self.k.OpenProcess(0x1028, False, a.pid)
        assert self.writer, 'Cannot open bounded simulator/diagnostic writes'
        self.saved_diagnostics = self.r.read(self.addr['CyberpunkVR_RuntimeDiagnostics'], 4)
        self.saved_inputs = [self.r.read(p+21, 23) for p in self.controllers]
        self.native_inputs = []
        self.action = a.action
        if self.action:
            context=self.read(self.action+0x50,'<Q')[0]
            state=self.read(self.addr['CyberpunkVR_RoomscaleDebug']+40,'<Q')[0]
            assert state and self.read(context+8,'<Q')[0]==self.read(state+0xA8,'<Q')[0], 'Action belongs to another provider'
            assert self.read(context+0x38,'<Q')[0]==self.read(state+0x90,'<Q')[0], 'Action belongs to another player'
            obj=self.read(context+0x10,'<Q')[0]
            table=self.read(obj+8,'<Q')[0];count=self.read(obj+0x14,'<I')[0]
            assert 0<count<512
            for name in (0xE386BCF96B1AB2A4,0xE386BDF96B1AB457):
                matches=[table+i*48 for i in range(count) if self.read(table+i*48,'<Q')[0]==name]
                assert len(matches)==1, 'Native input action not unique'
                address=matches[0]+12
                self.native_inputs.append((address,self.r.read(address,4)))
        self.write(self.addr['CyberpunkVR_RuntimeDiagnostics'], struct.pack('<I', 1))
        assert self.get('CyberpunkVR_BodyHeadingHooksReady', '<I') == 1, 'Native hooks were not installed'

    def write(self, address, data):
        buffer, done = ct.create_string_buffer(data), ct.c_size_t()
        if not self.k.WriteProcessMemory(self.writer,address,buffer,len(data),ct.byref(done)) or done.value != len(data):
            raise ct.WinError(ct.get_last_error())

    def read(self, address, fmt):
        return struct.unpack(fmt,self.r.read(address,struct.calcsize(fmt)))

    def skip(self, value):
        self.write(self.addr['CyberpunkVR_DebugBodyHeadingSkip'],struct.pack('<I',value))

    def stick(self, x=0, y=0):
        if self.native_inputs:
            for (address,_),value in zip(self.native_inputs,(x,y)):self.write(address,struct.pack('<f',value))
            return
        self.write(self.controllers[0]+36,struct.pack('<2f',x,y))

    def sample(self):
        row=super().sample()
        state=self.read(self.addr['CyberpunkVR_RoomscaleDebug']+40,'<Q')[0]
        row['view_reference']=self.get('g_viewYawUsedRad')
        row['requests']=self.get('CyberpunkVR_BodyHeadingRequests','<Q')
        row['commits']=self.get('CyberpunkVR_BodyHeadingCommits','<Q')
        row['loco']=self.get('g_VRLocomotionState','<i')
        row['simulator_stick']=self.read(self.controllers[0]+36,'<2f')
        for _ in range(5):
            seq=self.get('g_lcfSeq','<I')
            if seq & 1:continue
            camera=self.read(self.addr['g_lcf'],'<7f3I3f')
            if seq==self.get('g_lcfSeq','<I'):
                row['camera_position']=camera[:3];row['camera_quat']=camera[3:7]
                row['camera_sequence']=camera[7];break
        if state:
            provider=self.read(state+0xA8,'<Q')[0];adapter=self.read(state+0x160,'<Q')[0]
            row['state']=hex(state);row['provider']=hex(provider)
            row['provider_vtable']=hex(self.read(provider,'<Q')[0]) if provider else None
            row['body_quat']=self.read(state+0x1D0,'<4f')
            row['position']=[v/131072 for v in self.read(state+0x1C0,'<3i')]
            if provider and self.read(provider+0x10,'<Q')[0]==state:
                row['camera_input']=self.read(provider+0x9C,'<3f')
            if adapter and self.read(adapter+0x18,'<Q')[0]==state:
                row['adapter_quat']=self.read(adapter+0x30,'<4f')
            backend=self.read(self.addr['CyberpunkVR_RoomscaleDebug']+48,'<Q')[0]
            if backend:row['velocity']=self.read(backend+0x114,'<3f')
        telemetry=self.read(self.addr['g_telemetry'],'<Q')[0]
        action=self.action or (self.read(telemetry+128,'<Q')[0] if telemetry else 0)
        if action:
            row['action']=hex(action);row['move_xy']=self.read(action+0x90,'<2f')
            feature=self.read(action+0xA8,'<Q')[0]
            if feature:
                row['feature']=hex(feature)
                row['graph_direction']=self.read(feature+0x40,'<3f')
                row['graph_speed']=self.read(feature+0x50,'<f')[0]
        return row

    def check(self, name, passed, **data):
        result=dict(kind=name,pass_=bool(passed),**data)
        result['pass']=result.pop('pass_');self.results.append(result)
        print(json.dumps(result),flush=True)

    def custom_suite(self):
        self.prepare('low_ready');start=self.sample();view=start['view_reference']
        initial_accumulator=start['camera_input'][1]
        for sign in (-1,1):
            self.sweep('low_ready',0,sign*90*D,90,'native/turn')
            self.settled('low_ready',sign*90*D,'native/settled');end=self.sample()
            self.check('body_target',abs(wrap((end['offset_deg']-sign*90)*D))<2*D,
                       expected=sign*90,actual=end['offset_deg'])
            self.check('camera_input_untouched',abs(end['camera_input'][1]-initial_accumulator)<.001,
                       initial=initial_accumulator,actual=end['camera_input'])
            self.check('independent_view',abs(wrap(end['view_reference']-view))<.05*D,
                       reference_drift_deg=wrap(end['view_reference']-view)/D)
            q=end['frame']['head'];expected=mul([0,0,math.sin(view/2),math.cos(view/2)],[q[0],-q[2],q[1],q[3]])
            actual=end['camera_quat']
            dot=abs(sum(x*y for x,y in zip(expected,actual)))/math.sqrt(sum(x*x for x in expected)*sum(x*x for x in actual))
            camera_error=2*math.acos(min(1,dot))/D
            self.check('rendered_camera_tracks_hmd',camera_error<.2,angle_error_deg=camera_error)
            # Stop all physical requests while changing the target. The body must
            # stay where the engine accepted it, while the head continues to look.
            self.skip(1)
            before=end;begin=len(self.rows)
            self.sweep('low_ready',sign*90*D,sign*135*D,90,'native/skipped_turn')
            self.phase='native/skipped_hold';self.hold(2)
            rows=self.rows[begin:];end=rows[-1]
            drift=max(abs(wrap((r['body_deg']-before['body_deg'])*D))/D for r in rows)
            self.check('dropped_requests_hold_native_state',drift<.05,body_drift_deg=drift,
                       accepted_frames=end['commits']-before['commits'])
            self.pose('low_ready',sign*90*D,jitter=0);self.hold(.3);self.skip(0);self.hold(.5)
            if self.a.movement:
                self.phase='native/forward';begin=len(self.rows);before=self.sample()
                self.stick(y=.5)
                try:self.hold(self.a.move_seconds)
                finally:self.stick()
                end=self.sample();self.phase='native/forward_stop';self.hold(.6)
                delta=[b-a for a,b in zip(before['position'],end['position'])]
                yaw=end['body_deg']*D
                side=math.cos(yaw)*delta[0]+math.sin(yaw)*delta[1]
                forward=-math.sin(yaw)*delta[0]+math.cos(yaw)*delta[1]
                rows=[r for r in self.rows[begin:] if r.get('graph_speed',0)>.3 and r['phase']=='native/forward']
                directions=[r['graph_direction'] for r in rows]
                self.check('forward_body_and_animgraph',forward>.15 and abs(side)<.15 and bool(directions) and
                           statistics.mean(q[1] for q in directions)>.9,
                           distance_forward=forward,distance_side=side,graph_samples=len(directions),
                           graph_direction_mean=[statistics.mean(q[k] for q in directions) for k in range(3)] if directions else [])
            self.sweep('low_ready',sign*90*D,0,90,'native/return');self.settled('low_ready',0,'native/returned')
        # Mode changes retain the committed tracking frame, even at nonzero yaw.
        self.sweep('low_ready',0,60*D,90,'native/off_prepare');self.settled('low_ready',60*D,'native/off_settled')
        before=self.sample();self.mode(0);self.hold(.3);self.mode(1);self.hold(.4);end=self.sample()
        self.check('mode_transition_reference',abs(wrap(end['view_reference']-before['view_reference']))<.05*D,
                   reference_drift_deg=wrap(end['view_reference']-before['view_reference'])/D)
        self.sweep('low_ready',60*D,0,90,'native/final_return');self.settled('low_ready',0,'native/final')
        if self.a.extended:
            from live_matrix import POSES
            for pose in POSES:
                self.prepare(pose)
                for speed in (5,30,90,180,360):
                    for sign in (-1,1):self.turn_case(pose,speed,sign)
            self.negatives(list(POSES))
            self.mixed()

    def run(self):
        # Base Probe closes its read handle; restore writable state first, at the
        # end of the custom suite and on exceptions, before delegating that close.
        suite=self.custom_suite
        def guarded_suite():
            try:suite()
            finally:
                self.skip(0)
                for p,b in zip(self.controllers,self.saved_inputs):self.write(p+21,b)
                for p,b in self.native_inputs:self.write(p,b)
                self.write(self.addr['CyberpunkVR_RuntimeDiagnostics'],self.saved_diagnostics)
        self.custom_suite=guarded_suite
        try:super().run()
        finally:
            try:
                self.skip(0)
                for p,b in zip(self.controllers,self.saved_inputs):self.write(p+21,b)
                for p,b in self.native_inputs:self.write(p,b)
                self.write(self.addr['CyberpunkVR_RuntimeDiagnostics'],self.saved_diagnostics)
            finally:
                self.k.CloseHandle(self.writer)
                if self.a.out.exists():
                    report=json.loads(self.a.out.read_text())
                    report.update(game_memory_writes=True,simulator=self.simulator,
                                  writes=['native MoveX/MoveY values' if self.native_inputs else 'simulator controller inputs',
                                          'plugin diagnostics','plugin skip gate'])
                    self.a.out.write_text(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True);p.add_argument('--dll',type=Path,required=True)
    p.add_argument('--types',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--simulator',type=Path,default=Path.home()/'Downloads/OpenXR-Simulator-v1.5.0-win64/openxr_simulator.dll')
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
    p.add_argument('--movement',action='store_true')
    p.add_argument('--move-seconds',type=float,default=.25,help='Short forward pulse; choose an unobstructed test area')
    p.add_argument('--action',type=lambda s:int(s,0),default=0,help='Current PID player LocomotionSimple action, verified by its context/provider')
    p.add_argument('--background-pad',action='store_true',help='Temporarily enable native pad event delivery without changing OS focus')
    p.add_argument('--extended',action='store_true',help='Also run the 100-turn/40-negative/10-mixed simulator matrix')
    a=p.parse_args()
    a.batch_runtime=None;a.case='native';a.poses='low_ready'
    assert a.out.parent.is_dir() and not a.out.exists()
    if a.background_pad:
        from simulator_input import PadGate
        with PadGate(a.pid,a.game,a.simulator):NativeProbe(a).run()
        report=json.loads(a.out.read_text());report['background_pad_gate_restored']=True
        a.out.write_text(json.dumps(report))
    else:NativeProbe(a).run()
