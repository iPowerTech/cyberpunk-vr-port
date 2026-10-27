"""Exercise the existing recenter command at a held translated pose, without focus/keys."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import struct
import time

from live_read import Reader
from simulator_probe import send, read_status, await_pose


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('debug-address', 'sequence-address', 'entity-quat-address', 'realign-address'):
        parser.add_argument('--'+arg, type=lambda s: int(s, 0), required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--game-root', type=Path, default=Path('C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077'))
    args = parser.parse_args()
    directory = Path(os.environ['LOCALAPPDATA']) / 'OpenXR-Simulator'
    command = args.game_root / 'bin/x64/plugins/cyber_engine_tweaks/mods/CyberpunkVRPort_VRIK/vrik_recenter.ini'
    if not args.out.parent.is_dir() or args.out.exists() or not command.is_file():
        raise RuntimeError('Fresh output and installed recenter command required')
    status = read_status(directory)
    head = status['head_tracking']
    if any(abs(head[k]) > .0001 for k in ('yaw', 'pitch', 'roll')):
        raise RuntimeError('This probe starts at neutral angles')
    original = {**head['position'], 'yaw':0.0, 'pitch':0.0, 'roll':0.0}
    shifted = {**original, 'x':original['x']+.20}
    reader = Reader(args.pid, args.game_root / 'bin/x64/Cyberpunk2077.exe', args.debug_address, args.sequence_address)
    rows = []
    result = {'pid':args.pid, 'initial_status':status, 'focus_changed':False, 'keys_sent':False,
              'samples':rows, 'boundary':'CCT and body heading only; rendered camera continuity is not measured here.'}
    current = dict(original)

    def sample(label):
        row = reader.sample(); row['label'] = label
        q = struct.unpack('<4f', reader.read(args.entity_quat_address, 16))
        row['entity_yaw'] = 2*math.atan2(q[2],q[3])
        row['realign'] = struct.unpack('<f', reader.read(args.realign_address, 4))[0]
        rows.append(row)
        return row

    def sweep(goal, label):
        nonlocal current
        start = time.monotonic(); before = dict(current)
        while True:
            t = min(1,(time.monotonic()-start)/2); u=t*t*(3-2*t)
            current = {k:before[k]+(goal[k]-before[k])*u for k in before}
            send(directory,current); sample(label)
            if t>=1: break
            time.sleep(.01)
        await_pose(directory,current)

    def hold(label, seconds):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            last=sample(label); time.sleep(.02)
        return last

    try:
        result['initial'] = hold('initial',.5)
        sweep(shifted,'outbound')
        before=hold('held-before',1.0); result['before_recenter']=before
        text=command.read_text(encoding='ascii')
        old=int(re.search(r'recenter\s*=\s*(-?\d+)',text)[1])
        # Avoid colliding with the CET module's next local old+1 counter.
        counter=max(int(time.time()),old+1000)
        if counter>=2**31: raise RuntimeError('Recenter counter exceeds the native int range')
        result['command']={'path':str(command),'previous':old,'requested':counter}
        temporary=command.with_suffix('.ini.roomscale-writing')
        temporary.write_text(f'recenter={counter}\n',encoding='ascii')
        os.replace(temporary,command)
        until=time.monotonic()+5
        while time.monotonic()<until:
            after=sample('recenter-wait')
            if after['origin']!=before['origin']: break
            time.sleep(.02)
        else: raise RuntimeError('Recenter generation did not change')
        after=hold('held-after',1.2); result['after_recenter']=after
        result['checks']={
            'generation_changed':after['origin']==before['origin']+1,
            'capsule_did_not_move':math.dist(after['after'],before['after'])<.001,
            'no_replayed_motion':after['injected']==before['injected'],
            'consumption_reset':math.hypot(*after['consumed'])<.0001,
            'body_not_counterturned':abs(math.remainder(after['entity_yaw']-before['entity_yaw'],2*math.pi))<.001,
            'realign_released':abs(after['realign'])<.001,
            'tracking_active':after['gates']==15,
        }
        sweep(original,'return')
        result['returned']=hold('returned',1.0)
    except BaseException as exc:
        result['error']=f'{type(exc).__name__}: {exc}'
    finally:
        try:
            send(directory,original); result['final_status']=await_pose(directory,original)
        finally:
            reader.close()
            args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))
    if result.get('error') or not result.get('checks') or not all(result['checks'].values()):
        raise SystemExit(1)


if __name__=='__main__':
    main()
