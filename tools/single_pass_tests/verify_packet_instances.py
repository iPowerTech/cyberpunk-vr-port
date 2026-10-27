"""Check resolved native instances against actual D3D draw bindings in the same call."""
import argparse
import collections
import json
from pathlib import Path
import struct
p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);args=p.parse_args()
meta=json.loads(args.capture.read_text());assert meta['inputBytes']==256 and meta['drawRecordBytes']==1096
inputs=(args.capture.parent/meta['inputDataFile']).read_bytes();payload=(args.capture.parent/meta['inputBlobFile']).read_bytes()
raw=(args.capture.parent/meta['drawDataFile']).read_bytes();assert len(raw)%1096==0
draws=collections.defaultdict(list)
for at in range(0,len(raw),1096):
    qpc=struct.unpack_from('<Q',raw,at+8)[0];side=struct.unpack_from('<I',raw,at+48)[0];count=struct.unpack_from('<I',raw,at+56)[0]
    first=struct.unpack_from('<I',raw,at+68)[0];va,size,stride=struct.unpack_from('<QII',raw,at+112+7*16)
    resource=struct.unpack_from('<Q',raw,at+992)[0];valid=struct.unpack_from('<I',raw,at+1016)[0];thread=struct.unpack_from('<I',raw,at+1088)[0]
    draws[(side,thread)].append({'qpc':qpc,'begin':va+first*stride,'end':va+(first+count)*stride,'stride':stride,
        'resource':resource,'data':raw[at+1024:at+1024+valid] if valid<=64 else b''})
totals=collections.Counter();failures=[]
for row in meta['records']:
    actual=[d for d in draws[(row['side'],row['thread'])] if row['beginQpc']<=d['qpc']<=row['endQpc']]
    for index in range(row['offset'],row['offset']+row['count']):
        s=inputs[index*256:(index+1)*256];valid,count,program,origin=struct.unpack_from('<4I',s)
        offset,byte_count,stride,_,_,_=struct.unpack_from('<6I',s,216);gpu,resource=struct.unpack_from('<QQ',s,240)
        if origin==2 and gpu:
            totals['preuploadedPackets']+=1
            hits=[d for d in actual if d['stride']==stride and d['resource']==resource and d['begin']<=gpu and gpu+count*stride<=d['end']]
            if hits:totals['preuploadedAddressVerified']+=1
            else:failures.append({'packet':index,'call':row['sequence'],'reason':'No matching actual bound GPU range','gpu':hex(gpu),'count':count,'stride':stride,'drawsInCall':len(actual)})
        elif origin==1 and count==1 and valid&16:
            expected=payload[offset:offset+byte_count];assert len(expected)==byte_count
            if any(d['data']==expected for d in actual):totals['cpuSingleExact']+=1
            else:totals['cpuSingleNotObserved']+=1 # native batching can combine multiple packets into one draw
report={'pid':meta['pid'],'draws':len(raw)//1096,'totals':dict(totals),'failures':failures,
    'limitation':'Address/range verification validates the source buffer. It does not authorize merging materials or reorder scene passes.'}
args.capture.with_name(args.capture.stem+'-instances-verified.json').write_text(json.dumps(report,indent=2))
print(json.dumps({**report,'failures':failures[:12],'failureCount':len(failures)},indent=2))
