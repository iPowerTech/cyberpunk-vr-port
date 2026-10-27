"""Compare early private camera predictions with native peer inputs (exploratory)."""
import argparse
import base64
import json
import math
import struct
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('trace',type=Path)
args=parser.parse_args()
data=json.loads(args.trace.read_text());rows=data['records'];results=[]
# Only numeric camera fields, excluding unspecified struct padding.
matrix_ranges=[(0x50,0x150),(0x190,0x290),(0x2B0,0x370)]
for prediction in rows:
    if not prediction.get('predictionValid'):continue
    a=base64.b64decode(prediction['predictionBase64'])
    candidates=[r for r in rows if r['kind']==1 and r['side']==prediction.get('predictionPeerSide',0) and r['frame']==prediction['frame']
                and r.get('inputValid') and r['name']=='RenderElements' and r['width']==prediction['width'] and r['height']==prediction['height']]
    matches=[]
    for native in candidates:
        b=base64.b64decode(native['inputBase64']);diffs=[];worst=0;relative=0
        for start,end in matrix_ranges:
            for offset in range(start,end,4):
                x=struct.unpack_from('<f',a,offset)[0];y=struct.unpack_from('<f',b,offset)[0]
                assert math.isfinite(x) and math.isfinite(y)
                error=abs(x-y);worst=max(worst,error);relative=max(relative,error/(1+abs(y)))
                if a[offset:offset+4]!=b[offset:offset+4]:diffs.append(hex(offset))
        matches.append({'nativeOrder':native['order'],'positionExact':a[:12]==b[:12],
            'quatExact':a[0x10:0x20]==b[0x10:0x20],'jitterExact':a[0x370:0x384]==b[0x370:0x384],
            'matrixWordsDifferent':len(diffs),'maxAbsoluteError':worst,'maxRelativeError':relative,'differentOffsets':diffs})
    matches.sort(key=lambda x:(not x['positionExact'],not x['jitterExact'],x['maxRelativeError'],x['matrixWordsDifferent']))
    shader_matches=[]
    if prediction.get('shaderPredictionValid'):
        shader=base64.b64decode(prediction['shaderPredictionBase64'])
        valid=[int(x,16) for x in prediction.get('validShaderWordMask',[])]
        for native in candidates:
            expected=base64.b64decode(native['cameraBase64'])
            words=[hex(i) for i in range(0,848,4) if shader[i:i+4]!=expected[i:i+4]]
            eligible=[o for o in words if not valid or valid[(int(o,16)//4)//64]&(1<<((int(o,16)//4)%64))]
            shader_matches.append({'nativeOrder':native['order'],'wordsDifferent':len(eligible),'differentOffsets':eligible,
                'allWordsDifferent':len(words),'unavailableWords':212-sum(x.bit_count() for x in valid) if valid else 0})
        shader_matches.sort(key=lambda x:x['wordsDifferent'])
    results.append({'order':prediction['order'],'frame':prediction['frame'],'counter':prediction['predictionCounter'],
        'best':matches[0] if matches else None,'shaderBest':shader_matches[0] if shader_matches else None})
report={'pid':data['pid'],'predictions':len(results),'rows':results}
output=args.trace.with_name(args.trace.stem+'-peer-comparison.json');output.write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
