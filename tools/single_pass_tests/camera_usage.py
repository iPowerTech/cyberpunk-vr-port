"""Conservative DWORD reads of the explicitly typed CameraShaderConsts binding."""
import argparse
import json
import re
from pathlib import Path

def camera_usage(source):
    metadata=re.findall(r'(?m)^!\d+ = !\{i32 (\d+), (%[\w.]*CameraShaderConsts)\* undef, !"[^"]*", '
        r'i32 (\d+), i32 (\d+), i32 (\d+), i32 (\d+), null\}$',source)
    if len(metadata)!=1:raise ValueError('Expected one explicitly typed camera binding')
    cb_id,_,space,register,count,size=metadata[0];size=int(size)
    if int(count)!=1 or size<=0 or size%4:raise ValueError('Unsupported camera binding size/array')
    handles=re.findall(r'(%[\w.]+) = call %dx.types.Handle @dx.op.createHandle'
        r'\(i32 57, i8 2, i32 '+cb_id+r', i32 '+register+r', i1 false\)',source)
    if len(handles)!=1:raise ValueError('Expected one static camera handle')
    handle=handles[0]
    pattern=r'(?m)^\s*(%[\w.]+) = call %dx.types.CBufRet.\w+ @dx.op.cbufferLoadLegacy.\w+'
    pattern+=r'\(i32 59, %dx.types.Handle '+re.escape(handle)+r', i32 (%[\w.]+|\d+)\).*$'
    loads=list(re.finditer(pattern,source))
    if not loads or len(re.findall(re.escape(handle)+r'\b',source))!=len(loads)+1:
        raise ValueError('Unsupported camera handle use')
    words=set();dynamic=False;opaque=False
    lines=source.splitlines()
    for load in loads:
        value,index=load.groups()
        if index.startswith('%'):
            dynamic=True;words.update(range(size//4));continue
        index=int(index)
        uses=[line for line in lines if value in re.findall(r'%[\w.]+',line) and line.strip()!=load[0].strip()]
        components=set()
        for line in uses:
            m=re.fullmatch(r'\s*%[\w.]+ = extractvalue %dx.types.CBufRet.\w+ '+re.escape(value)+r', ([0-3])\s*(?:;.*)?',line)
            if m:components.add(int(m[1]))
            else:opaque=True;components.update(range(4))
        for component in components:
            word=index*4+component
            if word>=size//4:raise ValueError('Camera read exceeds declared binding')
            words.add(word)
    return {'register':int(register),'space':int(space),'bytes':size,'words':sorted(words),
            'dynamicIndices':dynamic,'opaqueAggregateUses':opaque}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('shader',type=Path)
    args=parser.parse_args();print(json.dumps(camera_usage(args.shader.read_text(encoding='utf-8')),indent=2))
