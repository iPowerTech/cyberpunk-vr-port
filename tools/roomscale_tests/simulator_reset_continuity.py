"""Check simulator Reset View continuity; leave the runtime at its new neutral pose."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import math
import os
from pathlib import Path
import struct
import time

from live_read import Reader
from simulator_menu import inspect
from simulator_probe import read_status

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--addresses',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
if args.out.exists() or not args.out.parent.is_dir():raise ValueError('Fresh output required')
cfg=json.loads(args.addresses.read_text(encoding='utf-8'))
addresses={k:int(v,16) for k,v in cfg.items() if isinstance(v,str) and v.startswith('0x')}
reader=Reader(cfg['pid'],Path('C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe'),addresses['debug'],addresses['debug_seq'])
directory=Path(os.environ['LOCALAPPDATA'])/'OpenXR-Simulator'
rows=[]
result={'pid':cfg['pid'],'samples':rows,'initial_status':read_status(directory),
        'focus_changed':False,'keys_sent':False,
        'boundary':'CCT snapshots are seqlocked; component eye positions are asynchronous. Reset is left at its new neutral pose: restoring old raw coordinates would cause new physical walking.'}
def position(name):
    return [v/131072 for v in struct.unpack('<3i',reader.read(addresses[name]+0xe0,12))]
def sample(phase):
    row=reader.sample();row['phase']=phase
    row['main']=position('main');row['vrcam']=position('vrcam')
    row['centre']=[(a+b)*.5 for a,b in zip(row['main'],row['vrcam'])]
    q=struct.unpack('<4f',reader.read(addresses['entity_quat'],16))
    row['entity_yaw']=2*math.atan2(q[2],q[3])
    rows.append(row)
    return row
def hold(phase,duration):
    end=time.monotonic()+duration
    while time.monotonic()<end:last=sample(phase);time.sleep(.003)
    return last
def has_reset(items):
    return any((i['id']==1402 and 'Reset View' in i['text']) or has_reset(i.get('items',[])) for i in items)
try:
    windows=inspect(cfg['pid'])
    if len(windows)!=1 or not has_reset(windows[0]['items']):raise RuntimeError('Simulator Reset View command not uniquely verified')
    before=hold('before',1);result['before']=before
    user=c.WinDLL('user32',use_last_error=True)
    user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    user.PostMessageW.restype=w.BOOL
    result['command']={'hwnd':hex(windows[0]['hwnd']),'title':windows[0]['title'],'id':1402}
    if not user.PostMessageW(windows[0]['hwnd'],0x111,1402,0):raise c.WinError(c.get_last_error())
    after=hold('after',3);result['after']=after
    result['final_status']=read_status(directory)
    post=[r for r in rows if r['phase']=='after']
    new=[r for r in post if r['origin']!=before['origin']]
    capsule=max(math.dist(r['after'],before['after']) for r in post)
    camera=max(math.dist(r['centre'],before['centre']) for r in post)
    requested=max((math.hypot(*r['requested'][:2]) for r in new),default=0)
    result['summary']={'origin_before':before['origin'],'origin_after':after['origin'],
        'max_capsule_shift_m':capsule,'max_camera_centre_shift_m':camera,
        'camera_centre_endpoint_shift_m':math.dist(after['centre'],before['centre']),
        'new_origin_max_requested_m':requested,'consumed_after':after['consumed'],
        'new_injections':after['injected']-before['injected'],
        'body_yaw_change_deg':math.degrees(math.remainder(after['entity_yaw']-before['entity_yaw'],2*math.pi))}
    result['checks']={'new_origin':after['origin']==before['origin']+1,
        'capsule_continuity_1mm':capsule<.001,'camera_continuity_5mm':camera<.005,
        'no_filter_tail_1mm':requested<.001,'consumption_reset_1mm':math.hypot(*after['consumed'])<.001}
except BaseException as exc:result['error']=f'{type(exc).__name__}: {exc}'
finally:
    reader.close()
    args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ('samples','before','after')},indent=2))
if result.get('error') or not result.get('checks') or not all(result['checks'].values()):raise SystemExit(1)
