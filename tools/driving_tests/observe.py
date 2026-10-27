"""Read-only idle driving/VRIK capture. Does not change input, poses or settings.

Publications are sampled asynchronously; this is not a solver-boundary trace.
"""
import argparse
import ctypes
import json
import math
from pathlib import Path
import struct
import sys
import time

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / name)
               for name in ('swimming_tests', 'roomscale_tests')]
from live_symbols import resolve
from live_read import Reader

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--dll', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--seconds', type=float, default=20)
p.add_argument('--wheel-stride', type=int, choices=(56, 64), default=64)
p.add_argument('--camera-debug',action='store_true')
a = p.parse_args()
assert 1 <= a.seconds <= 30 and a.out.parent.is_dir() and not a.out.exists()
fields = {
    'hands': ('g_handsStable', '128f'),
    'hand_seq': ('g_handsStableSeq', 'I'),
    'wheel': ('cvr::anim::g_wheel', str(a.wheel_stride * 2) + 's'),
    'center': ('cvr::anim::g_wheelCenter', '3f'),
    'center_valid': ('cvr::anim::g_wheelCenterValid', '?'),
    'span': ('cvr::anim::g_wheelSpan', 'f'),
    'steer': ('cvr::anim::g_wheelSteer', 'f'),
    'angle': ('cvr::anim::g_wheelSteerDeg', 'f'),
    'right': ('g_VRPalmModelR', '3f'),
    'left': ('g_VRPalmModelL', '3f'),
    'raw': ('g_VRHandRawModel', '6f'),
    'camera': ('g_VRCamModelPos', '3f'),
    'camera_rotation': ('g_VRCamModelRot', '4f'),
    'valid': ('g_VRPalmModelValid', 'i'),
    'view': ('g_viewPkt', '17f'),
    'view_valid': ('g_viewPktValid', '?'),
    'fresh': ('g_VRIKFreshTotal', 'i'),
    'replay': ('g_VRIKReplayTotal', 'i'),
    'cache_tick': ('g_solveCacheTick', 'I'),
    'cache_count': ('g_solveCacheN', 'i'),
    'bone_buffer': ('g_AnimPoseLastBoneBuf', 'Q'),
    'bone_count': ('g_VRBoneCount', 'i'),
    'driving': ('g_isDriving', '?'),
}
if a.camera_debug:
    fields.update({
        'pair':('s_vrikTransform','18f'), 'pair_seq':('s_vrikTransformSeq','I'),
        'cache_captured':('s_solveCacheCapturedMs','Q'),
        'cam_valid':('g_VRCamPosValid','i'), 'pair_valid':('g_VRCamPairValid','i'),
    })
extra=['g_VRBoneParent','g_VRRightBoneIdx','g_VRLeftBoneIdx','g_VRHeadBoneIdx','s_eyeCentres'] if a.camera_debug else []
symbols = resolve(a.pid, a.dll.resolve(), [n for n, _ in fields.values()]+extra)
reader = Reader(a.pid, r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe', 0, 0)
reader.k.GetTickCount64.restype=ctypes.c_uint64
parents=[];roles={}
if a.camera_debug:
    parents=struct.unpack('<800h',reader.read(int(symbols['g_VRBoneParent'],16),1600))
    roles={name:struct.unpack('<i',reader.read(int(symbols[name],16),4))[0]
           for name in ('g_VRRightBoneIdx','g_VRLeftBoneIdx','g_VRHeadBoneIdx')}
def multiply(a,b):
    x,y,z,w=a;u,v,t,s=b
    return (w*u+x*s+y*t-z*v,w*v-x*t+y*s+z*u,w*t+x*v-y*u+z*s,w*s-x*u-y*v-z*t)
def rotate(q,p):
    x,y,z,w=q
    return multiply(multiply(q,(*p,0)),(-x,-y,-z,w))[:3]
def fk(data,bone):
    chain=[];at=bone
    while at>=0:
        if at>=len(data)//48 or at in chain:raise RuntimeError('invalid live bone chain')
        chain.append(at);at=parents[at]
    pos=(0,0,0);rot=(0,0,0,1)
    for at in reversed(chain):
        v=struct.unpack_from('<12f',data,at*48);p=rotate(rot,v[:3])
        pos=tuple(x+y for x,y in zip(pos,p));rot=multiply(rot,v[4:8])
    return pos
rows, errors, ledgers = [], [], []
last_ledger=-1
start = time.monotonic()
try:
    while time.monotonic() - start < a.seconds:
        row = {'t': time.monotonic() - start}
        try:
            for key, (name, fmt) in fields.items():
                value = struct.unpack('<' + fmt, reader.read(int(symbols[name], 16), struct.calcsize('<' + fmt)))
                row[key] = value[0] if len(value) == 1 else value
            if a.camera_debug:
                data=reader.read(int(symbols['s_vrikTransform'],16),72)
                now=reader.k.GetTickCount64()
                row['pair_age_ms']=now-struct.unpack_from('<Q',data,48)[0]
                row['pair_flag']=struct.unpack_from('<I',data,44)[0]
                row['cache_age_ms']=now-row['cache_captured']
                if row['pair_age_ms']>150 and row['t']-last_ledger>.05 and len(ledgers)<100:
                    addr=int(symbols['s_eyeCentres'],16)
                    data=reader.read(addr,1160);seq=struct.unpack_from('<Q',data,1152)[0]
                    if seq==struct.unpack('<Q',reader.read(addr+1152,8))[0]:
                        ledgers.append({'t':row['t'],'pair_age_ms':row['pair_age_ms'],'next':seq,
                                        'records':[struct.unpack_from('<9i',data,i*36) for i in range(32)]})
                        last_ledger=row['t']
                count=row['bone_count'];assert 0<count<=800
                bones=reader.read(row['bone_buffer'],48*count)
                row['actual']={name:fk(bones,bone) for name,bone in roles.items()}
            data = row.pop('wheel')
            row['wheel'] = []
            for h in range(2):
                offset = h * a.wheel_stride
                row['wheel'].append({
                    'blend': struct.unpack_from('<f', data, offset)[0],
                    'engaged': bool(data[offset + 4]), 'at_wheel': bool(data[offset + 5]),
                    'target_valid': bool(data[offset + 8]),
                    'target': struct.unpack_from('<3f', data, offset + 12),
                    'animated': struct.unpack_from('<3f', data, offset + 24),
                    'anim_valid': bool(data[offset + 52]),
                })
            row['hands'] = row['hands'][:16]
            rows.append(row)
        except OSError as error:
            errors.append({'t': row['t'], 'error': str(error)})
            if len(errors) > 5: break
        time.sleep(.004)
finally:
    reader.close()
summary = {'samples': len(rows), 'errors': len(errors), 'peaks': {}, 'jumps': []}
for key in ('right', 'left', 'camera', 'raw'):
    changes = [(max(math.dist(x[key][j:j+3], y[key][j:j+3])
                    for j in range(0, len(x[key]), 3)), y['t'], i)
               for i, (x, y) in enumerate(zip(rows, rows[1:]))]
    summary['peaks'][key] = max(changes, default=(0, 0, 0))
    summary['jumps'].extend({'key': key, 'metres': v, 't': t, 'row': i+1}
                            for v, t, i in changes if v > .08)
report = {'pid': a.pid, 'sha256': symbols['sha256'], 'symbols': symbols,
          'rows': rows, 'errors': errors, 'ledgers':ledgers,'summary': summary, 'game_memory_writes': False,
          'boundary': 'Asynchronous observations, not a solver-boundary trace.'}
a.out.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2))
