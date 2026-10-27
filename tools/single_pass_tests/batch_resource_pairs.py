"""Validate paired-resource shader variants for the entire saved graphics inventory."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
from batch_shaders import PASSES,read_json,run,save_json
from patch_resource_pairs import patch_pairs


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--dxc-dir',type=Path,required=True)
    p.add_argument('--assembler',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();manifest=read_json(a.manifest);a.out.mkdir(parents=True,exist_ok=True)
    results={};started=time.monotonic()
    for sha,row in manifest['shaders'].items():
        if row['status']!='validated' or row['stage'] not in ('vs','ps'):continue
        source=a.manifest.parent/'cache'/row['key']/'native.ll'
        item=dict(status='rejected',stage=row['stage'],runtimeEligible=False)
        try:
            raw=source.read_bytes()
            if hashlib.sha256(raw).hexdigest()!=row['artifacts']['native.ll']:raise ValueError('Native IR hash changed')
            ir,report=patch_pairs(raw.decode('utf-8'),100 if row['stage']=='vs' else 101)
            output=a.out/(sha+'.ll');output.write_text(ir,encoding='utf-8')
            binary=output.with_suffix('.dxil')
            validation=run([a.assembler,a.dxc_dir,output,binary,*PASSES])
            item.update(status='validated',report=report,sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),validation=validation)
        except Exception as error:
            item['reason']=str(error)
        results[sha]=item
        if len(results)%25==0:print(json.dumps(dict(processed=len(results),statuses=dict(Counter(r['status'] for r in results.values())))),flush=True)
    summary=dict(shaders=len(results),statuses=dict(Counter(r['status'] for r in results.values())),
                 cbvPairs=sum(sum(p['kind']=='cbuffer' for p in r.get('report',{}).get('pairs',[])) for r in results.values()),
                 srvPairs=sum(sum(p['kind']=='srv' for p in r.get('report',{}).get('pairs',[])) for r in results.values()),
                 elapsedSeconds=time.monotonic()-started)
    save_json(a.out/'manifest.json',dict(summary=summary,shaders=results))
    print(json.dumps(summary,indent=2));return int(summary['statuses'].get('rejected',0)>0)


if __name__=='__main__':raise SystemExit(main())
