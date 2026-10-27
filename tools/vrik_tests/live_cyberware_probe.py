"""Bounded read-only rig/hand telemetry during native equipment changes.

Write a label to OUT/phase.txt between equipment requests; OUT/stop.txt ends
the capture. The caller owns game startup, simulator input and cleanup.
"""
import argparse,json,math,struct,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/swimming_tests'),str(ROOT/'tools/roomscale_tests')]
from live_symbols import resolve
from live_read import Reader

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--dll',type=Path,required=True);p.add_argument('--seconds',type=float,default=480)
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
    a=p.parse_args();assert 0<a.seconds<=600
    a.out.mkdir(parents=True,exist_ok=True)
    names=['g_VRBoneCount','g_VRFKCount','g_AnimPoseMatchCalls','g_PlayerTrackBufA','g_PlayerTrackBufB',
           'g_VRBind','g_VRRightBoneIdx','g_VRLeftBoneIdx','g_VRFKSnapPos','g_VRHandRawModel','s_playerBinding']
    symbols=resolve(a.pid,a.dll.resolve(),names);address={n:int(symbols[n],16) for n in names}
    (a.out/'symbols.json').write_text(json.dumps(symbols,indent=2))
    r=Reader(a.pid,a.game/'bin/x64/Cyberpunk2077.exe',0,0)
    def read(address,fmt):return struct.unpack(fmt,r.read(address,struct.calcsize(fmt)))
    def get(n,fmt):return read(address[n],fmt)
    start=time.monotonic();phase='initial';next_phase=0
    try:
        with (a.out/'poses.jsonl').open('x') as f:
            while time.monotonic()-start<a.seconds and not (a.out/'stop.txt').exists():
                now=time.monotonic()
                if now>=next_phase:
                    try:phase=(a.out/'phase.txt').read_text().strip()
                    except OSError:pass
                    next_phase=now+.05
                try:
                    n=get('g_VRBoneCount','<i')[0];matches=get('g_AnimPoseMatchCalls','<Q')[0]
                    row={'t':now-start,'phase':phase,'bones':n,'fk':get('g_VRFKCount','<i')[0],
                         'matches':matches,'bind':get('g_VRBind','<i')[0],
                         'tracks':[get('g_PlayerTrackBufA','<Q')[0],get('g_PlayerTrackBufB','<Q')[0]]}
                    obj=get('s_playerBinding','<Q')[0]
                    if obj:
                        rig=read(obj+8,'<Q')[0];row['native_rig']=rig
                        row['native_bones']=read(rig+0x2c,'<I')[0] if rig else 0
                    targets=get('g_VRHandRawModel','<6f');row['wrist_error_mm']=[]
                    for side,name in enumerate(('Left','Right')):
                        idx=get('g_VR'+name+'BoneIdx','<i')[0]
                        if 0<=idx<n:
                            pos=read(address['g_VRFKSnapPos']+idx*12,'<3f')
                            row['wrist_error_mm'].append(math.dist(pos,targets[side*3:side*3+3])*1000)
                    if n!=get('g_VRBoneCount','<i')[0]:continue
                    f.write(json.dumps(row)+'\n');f.flush()
                except OSError:
                    # A freed engine pose during rebind is an expected read miss.
                    pass
                time.sleep(.004)
    finally:r.close()

if __name__=='__main__':main()
