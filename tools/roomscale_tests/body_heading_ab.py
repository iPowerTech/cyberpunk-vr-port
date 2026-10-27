"""Identical old/new DLL comparison; paired camera/HMD measurements, no focus.

Raw rows are flushed to JSONL during the run. Create OUT.stop to stop and restore.
Uses the ordinary simulator: upright sweeps change one HMD pose atomically;
independent-head tests use slow, settled three-device IPC steps.
"""
import argparse,ctypes as ct,json,math,os,re,statistics,struct,sys,threading,time
from pathlib import Path

sys.path[:0]=[str(Path(__file__).resolve().parents[1]/p) for p in ('body_rotation_tests','swimming_tests')]
from live_matrix import Probe,D,wrap,mul,atomic
from simulator_input import PadGate


def yaw(q):
    x,y,z,w=q
    return math.atan2(2*(w*z+x*y),1-2*(y*y+z*z))


class Comparison(Probe):
    def __init__(self,a):
        super().__init__(a)
        from live_symbols import resolve
        names=['g_lcf','g_lcfSeq','g_camWriteRing','g_camWriteRingHead','g_camWriteQuat',
               'g_camWritePose','g_camWritePoseId','CyberpunkVR_RoomscaleDebug','CyberpunkVR_RoomscaleDebugSeq',
               'CyberpunkVR_RuntimeDiagnostics','s_hybridBody','g_menuModeValue','g_telemetry']
        extra=resolve(a.pid,a.dll.resolve(),names)
        self.symbols.update(extra);self.addr.update({n:int(extra[n],16) for n in names})
        self.stop=threading.Event();self.thread=None;self.latest={};self.failure=None
        self.phase='startup';self.rows=[];self.mode_events=[];self.commands=[]
        self.k=self.r.k
        self.k.WriteProcessMemory.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_void_p,ct.c_size_t,ct.POINTER(ct.c_size_t)]
        self.writer=self.k.OpenProcess(0x1028,False,a.pid);assert self.writer
        self.saved_diagnostics=self.r.read(self.addr['CyberpunkVR_RuntimeDiagnostics'],4)
        self.original_head={'x':0.,'y':1.7,'z':0.,'yaw':0.,'pitch':0.,'roll':0.}
        self.original_hands=[dict(hand=h,posX=(-.2 if h==0 else .2),posY=-.3,posZ=-.4,yaw=0.,pitch=-.3) for h in (0,1)]
        self.neck=[0.,1.6,.08];self.current_head=dict(self.original_head)
        self.pad=None;self.checkpoints=[]
        t=self.types['symbol:g_camWriteRing'];self.ring_size=t['size']
        self.head_size=self.types['OpenXRHeadPose']['size']
        self.pose_offset=t['fields']['pose']['offset'];self.id_offset=t['fields']['id']['offset']
        self.valid_offset=t['fields']['valid']['offset']
        self.head_valid_offset=self.pose_offset+self.types['OpenXRHeadPose']['fields']['valid']['offset']

    def write(self,address,data):
        b=ct.create_string_buffer(bytes(data));done=ct.c_size_t()
        if not self.k.WriteProcessMemory(self.writer,address,b,len(data),ct.byref(done)) or done.value!=len(data):
            raise ct.WinError(ct.get_last_error())

    def read(self,address,fmt):return struct.unpack(fmt,self.r.read(address,struct.calcsize(fmt)))

    def sample(self):return self.latest  # the pose helper never races the recorder

    def capture(self):
        row={'t':time.monotonic()-self.t0,'phase':self.phase}
        for _ in range(5):
            seq=self.get('g_lcfSeq','<I')
            if seq&1:continue
            b=self.r.read(self.addr['g_lcf'],52)
            if seq==self.get('g_lcfSeq','<I'):break
        else:return None
        camera=struct.unpack('<7f3I3f',b)
        row.update(camera_position=camera[:3],camera_quat=camera[3:7],camera_sequence=camera[7])
        key=b[12:28];matches=[];nearest=10.
        # The ring publishes valid last, then advances head. Exclude unfinished
        # slots and retry if publication advanced while reading the ring.
        for _ in range(3):
            head=self.get('g_camWriteRingHead','<Q')
            ring=self.r.read(self.addr['g_camWriteRing'],16*self.ring_size)
            if head!=self.get('g_camWriteRingHead','<Q'):continue
            for i in range(16):
                entry=ring[i*self.ring_size:(i+1)*self.ring_size]
                index=struct.unpack_from('<Q',entry,self.id_offset)[0]
                candidate=struct.unpack_from('<4f',entry)
                distance=min(math.dist(camera[3:7],candidate),math.dist(camera[3:7],[-v for v in candidate]))
                if struct.unpack_from('<I',entry,self.valid_offset)[0]==1 and max(0,head-16)<=index<head:
                    nearest=min(nearest,distance)
                # Locate normalizes the already-written quaternion again. Allow
                # only float roundoff (<0.00023 degrees), far below pose steps;
                # distinct matching HMD orientations are still rejected below.
                if distance<=2e-6 and struct.unpack_from('<I',entry,self.valid_offset)[0]==1 and max(0,head-16)<=index<head and entry[self.head_valid_offset]:
                    matches.append((index,entry[self.pose_offset:self.pose_offset+self.head_size],distance))
            break
        if not matches:
            # The latest publication may precede its history insertion by a few
            # instructions. Require an unchanged full packet and exact quaternion.
            ident=self.get('g_camWritePoseId','<Q')
            q=self.r.read(self.addr['g_camWriteQuat'],16);p=self.r.read(self.addr['g_camWritePose'],self.head_size)
            if q==key and p[28] and ident==self.get('g_camWritePoseId','<Q') and q==self.r.read(self.addr['g_camWriteQuat'],16) and p==self.r.read(self.addr['g_camWritePose'],self.head_size):
                matches=[(-1,p,0)]
        if matches:
            heads=[struct.unpack_from('<4f',p,12) for _,p,_ in matches]
            spread=max(min(math.dist(heads[0],q),math.dist(heads[0],[-v for v in q])) for q in heads)
            row['head_match_spread']=spread
            # Candidates differing only by sub-millidegree float/filter tails
            # are equivalent at this measurement's 0.001-degree resolution.
            if spread<=4e-6:
                index,p,distance=min(matches,key=lambda item:(item[2],-item[0]));q=struct.unpack_from('<4f',p,12)
                h=[q[0],-q[2],q[1],q[3]]
                base=mul(camera[3:7],[-h[0],-h[1],-h[2],h[3]])
                row.update(head_quat=q,head_position=struct.unpack_from('<3f',p),
                           base_quat=base,base_yaw_deg=yaw(base)/D,pose_origin=struct.unpack_from('<Q',p,32)[0],ring_id=index,
                           pairing_quat_distance=distance)
        row['paired']='head_quat' in row
        row['nearest_record_quat_distance']=nearest
        row['offset_deg']=self.get('CyberpunkVR_BodyYawRealignRad')/D
        row['body_yaw_deg']=self.get('CyberpunkVR_BodyYawFinalRad')/D
        row['body_active']=self.get('CyberpunkVR_BodyYawFollow','<i')
        row['cone_deg']=self.get('CyberpunkVR_BodyYawFollowDeadDeg')
        row['hybrid_tracked']=bool(self.r.read(self.addr['s_hybridBody']+self.types['cvr::body::HybridBodyYaw']['fields']['tracked']['offset'],1)[0])
        # Native physics result, without modifying the move or animation feature.
        d=None
        for _ in range(3):
            seq=self.get('CyberpunkVR_RoomscaleDebugSeq','<I')
            if seq&1:continue
            candidate=self.r.read(self.addr['CyberpunkVR_RoomscaleDebug'],168)
            if seq==self.get('CyberpunkVR_RoomscaleDebugSeq','<I'):d=candidate;break
        state=struct.unpack_from('<Q',d,40)[0] if d else 0
        if state:
            provider=self.read(state+0xA8,'<Q')[0]
            row['entity_position']=[v/131072 for v in self.read(state+0x1C0,'<3i')]
            row['entity_quat']=self.read(state+0x1D0,'<4f')
            row['native_request']=struct.unpack_from('<3f',d,88)
            row['velocity']=struct.unpack_from('<3f',d,136)
            if provider and self.read(provider+0x10,'<Q')[0]==state:
                row['camera_input']=self.read(provider+0x9C,'<3f')
            row['camera_relative_entity']=[a-b for a,b in zip(row['camera_position'],row['entity_position'])]
        return row

    def record(self):
        try:
            with self.a.out.with_suffix('.jsonl').open('w',encoding='utf-8') as f:
                last_flush=0
                while not self.stop.is_set():
                    row=self.capture()
                    if row:
                        self.rows.append(row);self.latest=row;f.write(json.dumps(row,separators=(',',':'))+'\n')
                    if time.monotonic()-last_flush>.5:f.flush();last_flush=time.monotonic()
                    self.stop.wait(.002)
        except BaseException as e:self.failure=repr(e);self.stop.set()

    def delay(self,seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if self.failure:raise RuntimeError(self.failure)
            if self.a.out.with_suffix('.stop').exists():raise RuntimeError('User stopped A/B test')
            time.sleep(min(.02,max(0,end-time.monotonic())))

    def phase_hold(self,name,seconds=.7):
        self.phase=name;self.delay(seconds)

    def settings(self,**values):
        content=self.ini.read_bytes()
        for key,value in values.items():
            content,n=re.subn(rb'(?m)^('+key.encode()+rb'\s*=\s*)[^\r\n]+',lambda m:m[1]+str(value).encode(),content)
            assert n==1,key
        atomic(self.ini,content);self.delay(.45)
        self.mode_events.append({'t':time.monotonic()-self.t0,'values':values})

    def head_at(self,degrees):
        self.current_head={**self.original_head,'yaw':degrees*D}
        self.commands.append({'t':time.monotonic()-self.t0,'head':self.current_head})
        self.head(self.current_head)

    def head_sweep(self,a,b,speed,phase):
        self.phase=phase
        # Same fixed angular command sequence for both DLLs. Transport timings
        # are retained; actual camera measurements use its paired HMD sample.
        steps=max(1,math.ceil(abs(b-a)/(speed/60)))
        start=time.monotonic()
        for i in range(steps+1):
            due=start+i/60
            if time.monotonic()<due:self.delay(due-time.monotonic())
            self.head_at(a+(b-a)*i/steps)

    def devices(self,b0,b1,h0,h1,pitch,phase):
        self.phase=phase;steps=max(1,math.ceil(max(abs(b1-b0),abs(h1-h0))/3))
        for i in range(steps+1):
            u=i/steps
            self.pose('low_ready',(b0+(b1-b0)*u)*D,(h0+(h1-h0)*u)*D,pitch*D,jitter=0)
            self.delay(.08)

    def checkpoint(self,name):
        row=dict(self.latest);row['name']=name;self.checkpoints.append(row)
        print(json.dumps({'checkpoint':name,'body_deg':row.get('body_yaw_deg'),'offset_deg':row.get('offset_deg'),
                          'base_yaw_deg':row.get('base_yaw_deg'),'tracked':row.get('hybrid_tracked')}),flush=True)

    def protocol(self):
        self.settings(xr_physical_body_rotation=0,xr_tracked_body_rotation=0,xr_hybrid_body_rotation=1,xr_movement_source=0)
        for c in self.original_hands:self.hand(c)
        self.head_at(0);self.phase_hold('baseline',1.5);self.checkpoint('baseline')
        for speed in (30,90,360):
            for sign in (-1,1):
                label=f'upright/{speed}/{sign}'
                self.head_sweep(0,sign*90,speed,label+'/out')
                self.phase_hold(label+'/held',.8)
                self.head_sweep(sign*90,0,speed,label+'/back')
                self.phase_hold(label+'/neutral',.6)
                self.checkpoint(label)
        # A fixed-position, instant 90-degree head turn separates the subsequent
        # body catch-up from the already stopped headset.
        for sign in (-1,1):
            self.phase=f'catchup/{sign}';self.head_at(sign*90);self.delay(1.2)
            self.checkpoint(self.phase);self.head_at(0);self.phase_hold('catchup/recover',1)
        # Independent head/body direction using the actual default Hybrid mode.
        for sign in (-1,1):
            self.devices(0,0,0,0,-35,'counterlook/enter');self.phase_hold('counterlook/enter_settle',.8)
            self.devices(0,sign*60,0,sign*60,-35,f'counterlook/{sign}/body_turn')
            self.phase_hold(f'counterlook/{sign}/body_settle',.8)
            self.devices(sign*60,sign*60,sign*60,0,-35,f'counterlook/{sign}/head_return')
            self.phase_hold(f'counterlook/{sign}/held',1);self.checkpoint(f'counterlook/{sign}')
            self.phase=f'forward/{sign}'
            self.pad.stick(y=.5)
            try:self.delay(.24)
            finally:self.pad.stick()
            self.phase_hold(f'forward/{sign}/coast',.4)
            self.checkpoint(f'forward/{sign}')
            self.phase=f'reverse/{sign}';self.pad.stick(y=-.5)
            try:self.delay(.24)
            finally:self.pad.stick()
            self.phase_hold(f'reverse/{sign}/coast',.6)
            self.devices(sign*60,0,0,0,-35,'counterlook/body_return')
            self.pose('low_ready',0,0,0,jitter=0);self.phase_hold('counterlook/recover',1)
        # Explicit mode change is reported separately from ordinary head motion.
        for c in self.original_hands:self.hand(c)
        self.head_at(0);self.phase_hold('switch/neutral',.8)
        self.head_sweep(0,90,90,'switch/turn');self.phase_hold('switch/before',1);self.checkpoint('switch/before')
        self.phase='switch/off';self.settings(xr_physical_body_rotation=0,xr_tracked_body_rotation=0,xr_hybrid_body_rotation=0)
        self.delay(1);self.checkpoint('switch/off')
        self.phase='switch/on';self.settings(xr_hybrid_body_rotation=1);self.delay(1);self.checkpoint('switch/on')

    def run(self):
        error=None;restore=[]
        # Save cleanup information before any automated input or live patch.
        self.a.out.with_suffix('.ini.before').write_bytes(self.saved_ini)
        self.a.out.with_suffix('.restore.json').write_text(json.dumps({'pid':self.a.pid,'head':self.original_head,
            'hands':self.original_hands,'diagnostics':self.saved_diagnostics.hex(),'symbols':self.symbols}))
        try:
            self.write(self.addr['CyberpunkVR_RuntimeDiagnostics'],struct.pack('<I',1))
            end=time.monotonic()+100
            while time.monotonic()<end:
                if self.get('g_PlayerPoseHeadingReady','<?') and self.get('g_menuModeValue','<i')==0:
                    f=self.frame()
                    if f['valid'] and all(f['tracked']):break
                time.sleep(.2)
            else:raise RuntimeError('Loaded gameplay/tracking did not become ready')
            self.t0=time.monotonic();self.thread=threading.Thread(target=self.record,daemon=True);self.thread.start()
            with PadGate(self.a.pid,self.a.game,self.a.simulator) as self.pad:self.protocol()
        except BaseException as e:error=repr(e)
        finally:
            self.phase='restore'
            try:
                self.clear_pending();self.head(self.original_head)
                for c in self.original_hands:self.hand(c)
                atomic(self.ini,self.saved_ini)
                time.sleep(.6)
            except BaseException as e:restore.append(repr(e))
            self.stop.set()
            if self.thread:self.thread.join(5)
            try:self.write(self.addr['CyberpunkVR_RuntimeDiagnostics'],self.saved_diagnostics)
            except BaseException as e:restore.append(repr(e))
            self.k.CloseHandle(self.writer);self.r.close()
            result={'version':self.a.version,'pid':self.a.pid,'symbols':self.symbols,'types':self.types,
                    'checkpoints':self.checkpoints,'commands':self.commands,'mode_events':self.mode_events,
                    'raw_file':str(self.a.out.with_suffix('.jsonl')),'samples':len(self.rows),
                    'error':error or self.failure,'restore_errors':restore,'focus_changed':False,
                    'test_only_background_pad_gate':True,'sequential_three_device_ipc':True}
            self.a.out.write_text(json.dumps(result,indent=2))
            print(json.dumps({k:result[k] for k in ('version','samples','error','restore_errors')}),flush=True)
        if error or self.failure or restore:raise RuntimeError(error or self.failure or restore)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    for name in ('dll','types','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--version',required=True)
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
    p.add_argument('--simulator',type=Path,default=Path.home()/'Downloads/OpenXR-Simulator-v1.5.0-win64/openxr_simulator.dll')
    a=p.parse_args();a.batch_runtime=None;a.case='ab';a.poses='low_ready'
    assert a.out.parent.is_dir() and not a.out.exists()
    Comparison(a).run()
