"""Read a paused PlacedCameraPoseFind invocation and its completed-write history."""
import argparse
import json
import math
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"roomscale_tests"))
from live_read import Reader

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--addresses',type=Path,required=True)
p.add_argument('--view',type=int,required=True)
p.add_argument('--quaternion',type=lambda s:int(s,0),required=True)
p.add_argument('--position',type=lambda s:int(s,0),required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
if a.out.exists():raise ValueError('Fresh output required')
cfg=json.loads(a.addresses.read_text(encoding='utf-8-sig'))
r=Reader(cfg['pid'],Path('C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe'),int(cfg['debug'],16),int(cfg['debug_seq'],16))
try:
    q=struct.unpack('<4f',r.read(a.quaternion,16));pos=struct.unpack('<3i',r.read(a.position,12))
    history=r.read(int(cfg['placed_history'],16),64*88)
    head=struct.unpack('<Q',r.read(int(cfg['placed_history_head'],16),8))[0]
    entries=[]
    for identity in range(max(0,head-64),head):
        offset=identity%64*88
        recorded_id=struct.unpack_from('<Q',history,offset+72)[0]
        if recorded_id!=identity:continue
        view=struct.unpack_from('<I',history,offset+80)[0]
        if view!=a.view:continue
        eq=struct.unpack_from('<4f',history,offset);ep=struct.unpack_from('<3i',history,offset+16)
        dot=sum(x*y for x,y in zip(eq,q));norm=math.sqrt(sum(x*x for x in eq)*sum(x*x for x in q))
        entries.append({'id':identity,'age':head-1-identity,'view':view,'q':eq,'position':ep,
            'position_delta_fp':[x-y for x,y in zip(ep,pos)],
            'quat_component_error':min(max(abs(x-y) for x,y in zip(eq,q)),max(abs(x+y) for x,y in zip(eq,q))),
            'angle_deg':math.degrees(2*math.acos(min(1,abs(dot)/norm))),
            'head_position':struct.unpack_from('<3f',history,offset+32),
            'head_q':struct.unpack_from('<4f',history,offset+44),
            'origin':struct.unpack_from('<Q',history,offset+64)[0]})
    result={'pid':cfg['pid'],'view':a.view,'query_q':q,'query_position':pos,'history_head':head,'entries':entries}
    a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    exactpos=[e for e in entries if not any(e['position_delta_fp'])]
    closest=min(entries,key=lambda e:sum(abs(v) for v in e['position_delta_fp'])+e['quat_component_error']) if entries else None
    print(json.dumps({'view':a.view,'query_q':q,'query_position':pos,'same_position_entries':len(exactpos),'closest':closest},indent=2))
finally:r.close()
