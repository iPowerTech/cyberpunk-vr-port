"""Summarize preparation order without assuming meanings for unknown camera fields."""
import argparse
import base64
import collections
import json
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('trace',type=Path)
args=parser.parse_args()
data=json.loads(args.trace.read_text());rows=data['records']
assert all(row['order']==i+1 for i,row in enumerate(rows))
summary={'pid':data['pid'],'full':data['full'],'frames':[]}
for frame in sorted({r['frame'] for r in rows}):
    events=[r for r in rows if r['frame']==frame]
    item={'frame':frame,'events':len(events),'cameraNodes':{},'firstGeometry':{},'cameraSnapshotsBeforeFirstGeometry':{}}
    for side in (0,1):
        cameras=[r for r in events if r['kind']==1 and r['side']==side]
        item['cameraNodes'][side]=dict(collections.Counter(r['name'] for r in cameras))
        for row in cameras:assert len(base64.b64decode(row['cameraBase64']))==848
        geometry=[r for r in events if r['kind']==2 and r['side']==side and r['name']=='RenderElements']
        if geometry:
            first=geometry[0]['order'];item['firstGeometry'][side]=first
            item['cameraSnapshotsBeforeFirstGeometry'][side]=dict(collections.Counter(
                str(r['side'])+':'+r['name'] for r in events if r['kind']==1 and r['order']<first))
    summary['frames'].append(item)
print(json.dumps(summary,indent=2))
