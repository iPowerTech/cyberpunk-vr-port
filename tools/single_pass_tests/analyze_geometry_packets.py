"""Compare owned native packet snapshots; equality alone cannot authorize draw skipping."""
import argparse
import collections
import json
import struct
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);args=p.parse_args()
source=json.loads(args.capture.read_text());blob=(args.capture.parent/source['packetDataFile']).read_bytes()
input_blob=(args.capture.parent/source['inputDataFile']).read_bytes() if source.get('inputDataFile') else None
abi=source.get('inputBytes',216);assert abi in (216,256)
payload=(args.capture.parent/source['inputBlobFile']).read_bytes() if source.get('inputBlobFile') else b''
if input_blob is not None:assert len(input_blob)//abi==len(blob)//16 and len(input_blob)%abi==0
groups=collections.defaultdict(lambda:{0:[],1:[]});missing=[]
resolved_groups=collections.defaultdict(lambda:{0:[],1:[]});input_stats=collections.Counter()
for row in source['records']:
    if row['status'] not in (0,1):missing.append(row['sequence']);continue
    if not(row['metadataFlags']&4):missing.append(row['sequence']);continue
    key=(row['frame'],row['node'],row['groupPlane'],row['groupVariant'],row['groupFlags'],tuple(row['modes']))
    start=row['offset']*16;end=start+row['count']*16
    assert 0<=start<=end<=len(blob)
    groups[key][row['side']].extend(blob[i:i+16] for i in range(start,end,16))
    if input_blob is not None:
        for index in range(row['offset'],row['offset']+row['count']):
            snapshot=input_blob[index*abi:(index+1)*abi]
            valid,instances,program,origin=struct.unpack_from('<4I',snapshot)
            a,b=struct.unpack_from('<QQ',blob,index*16)
            input_stats['packets']+=1
            input_stats['multipleInstances']+=instances!=1;input_stats['nonemptyCommandPrograms']+=program!=0
            if abi==216:
                # The old capture read the pool unconditionally. It does not
                # contain the upload buffer or postprocessed billboard matrices.
                if (a&(1<<59) and not b&(1<<50)) or snapshot[16]!=0 or not b&(1<<51):valid&=~16
                input_stats['completeSingleInstance']+=valid==63
                if valid!=63 or instances!=1 or program!=0:continue
                matrices=snapshot[152:216] if b&(1<<50) else snapshot[152:200];program_data=b''
            else:
                input_stats['completeStreams']+=valid==127
                input_stats['preuploaded']+=origin==2;input_stats['unsupportedGeometry']+=origin==0
                if valid!=127:continue
                matrix_at,matrix_bytes,stride,program_at,program_bytes,_=struct.unpack_from('<6I',snapshot,216)
                assert matrix_bytes==instances*stride and stride in (48,64)
                assert matrix_at+matrix_bytes<=len(payload) and program_at+program_bytes<=len(payload)
                assert program_bytes==program
                matrices=payload[matrix_at:matrix_at+matrix_bytes];program_data=payload[program_at:program_at+program_bytes]
            # Remove only table indices whose actual entries were copied. Keep
            # sort keys, draw flags, instance count and every other native bit.
            a&=~(((1<<39)-1)<<17)
            b&=~(((1<<18)-1)|(((1<<17)-1)<<33))
            descriptors=bytearray(snapshot[16:152]);descriptors[88:96]=bytes(8) # owned program replaces its pointer
            resolved_groups[key][row['side']].append(struct.pack('<QQ',a,b)+bytes(descriptors)+program_data+matrices)
reports=[]
for key,eyes in sorted(groups.items()):
    first,second=collections.Counter(eyes[1]),collections.Counter(eyes[0])
    common=first&second
    resolved=resolved_groups[key]
    reports.append({'frame':key[0],'node':key[1],'plane':key[2],'variant':key[3],'flags':key[4],'modes':key[5],
        'vrcamPackets':len(eyes[1]),'mainPackets':len(eyes[0]),'exactCommonPackets':sum(common.values()),
        'vrcamOnly':sum((first-second).values()),'mainOnly':sum((second-first).values()),
        'exactResolvedInputs':sum((collections.Counter(resolved[1])&collections.Counter(resolved[0])).values()) if input_blob is not None else None})
result={'pid':source['pid'],'full':source['full'],'unclassifiedRecords':missing,
    'limitation':'Packed handle equality is not proof that mutable materials, instance records and shader resources match. No native draw may be skipped from this report alone.',
    'sharedStorage':sorted({r['storage'] for r in source['records'] if r['side']==0}&{r['storage'] for r in source['records'] if r['side']==1}),
    'inputStats':dict(input_stats),
    'groups':reports}
args.capture.with_name(args.capture.stem+'-comparison.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
