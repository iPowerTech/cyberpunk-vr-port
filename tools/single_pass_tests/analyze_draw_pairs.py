"""Compare native geometry/instance packets; material/resource equivalence is separate."""
import argparse
import base64
import collections
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);args=p.parse_args()
data=json.loads(args.trace.read_text());rows=data['draws'];reports=[]
def geometry(row):
    return (row['pipeline'],row['root'],tuple(row['index']),row['indices'],row['instances'],row['firstIndex'],row['baseVertex'],
        tuple((k,tuple(v)) for k,v in sorted(row['vertices'].items()) if k!='7'),row['rtCount'],tuple(row['targets']),row['depth'])
for frame in sorted({r['frame'] for r in rows}):
    eyes={s:[r for r in rows if r['frame']==frame and r['side']==s] for s in (1,0)}
    main=collections.defaultdict(list)
    for row in eyes[0]:
        if row.get('instanceDataBytes'):main[(geometry(row),base64.b64decode(row['instanceBase64']))].append(row)
    paired=[];unmatched=[]
    for row in eyes[1]:
        if not row.get('instanceDataBytes'):unmatched.append(row['order']);continue
        key=(geometry(row),base64.b64decode(row['instanceBase64']))
        if main[key]:
            partner=main[key].pop(0);paired.append({'vrcamOrder':row['order'],'mainOrder':partner['order'],
                'vrcamFirstInstance':row['firstInstance'],'mainFirstInstance':partner['firstInstance'],'instanceBytes':row['instanceDataBytes']})
        else:unmatched.append(row['order'])
    reports.append({'frame':frame,'draws':{s:len(v) for s,v in eyes.items()},
        'readableInstances':{s:sum(bool(r.get('instanceDataBytes')) for r in v) for s,v in eyes.items()},
        'geometryInstancePairs':paired,'unmatchedVrcam':unmatched,'unmatchedMain':[r['order'] for values in main.values() for r in values]})
result={'pid':data['pid'],'materialAndResourceVerification':'pending; geometry equality alone does not authorize skipping a native draw','frames':reports}
args.trace.with_name(args.trace.stem+'-geometry-pairs.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
