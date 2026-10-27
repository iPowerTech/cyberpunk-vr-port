"""Link the captured material PS to its VS clip-distance prefix for view instancing.

The current adapter handles one exact signature shape and fails closed otherwise.
All material arithmetic/resource operations are unchanged. Only the generated
front-face input ID/register move after an explicit unused clip-distance input.
"""
import argparse
from pathlib import Path
import re
from upgrade_stage import upgrade

def link(source):
    source=upgrade(source)
    original=source
    face=re.search(r'(?m)^(!\d+) = !\{i32 4, !"SV_IsFrontFace", i8 5, i8 13, (!\d+), i8 1, i32 1, i8 1, i32 4, i8 0, (!\d+|null)\}$',source)
    if not face:raise ValueError('Unsupported front-face input shape')
    face_md,zero,properties=face.groups()
    if not re.search(r'(?m)^'+re.escape(zero)+r' = !\{i32 0\}$',source):raise ValueError('Unexpected semantic index')
    inputs=re.search(r'(?m)^(!\d+) = !\{(!\d+, !\d+, !\d+, !\d+, '+re.escape(face_md)+r')\}$',source)
    if not inputs:raise ValueError('Unsupported five-element PS input signature')
    input_ids=inputs[2].split(', ')
    for index,md in enumerate(input_ids[:4]):
        pattern=r'(?m)^'+re.escape(md)+r' = !\{i32 '+str(index)+r', !"'+('SV_Position' if index==0 else 'TEXCOORD')+r'", .*i32 1, i8 4, i32 '+str(index)+r', i8 0, '
        if not re.search(pattern,source):raise ValueError('Unexpected prefix packing')
    new='!'+str(max(map(int,re.findall(r'(?m)^!(\d+) =',source)))+1)
    source=source.replace(inputs[0],inputs[1]+' = !{'+', '.join(input_ids[:4]+[new,face_md])+'}')
    source=source.replace(face[0],f'{face_md} = !{{i32 5, !"SV_IsFrontFace", i8 5, i8 13, {zero}, i8 1, i32 1, i8 1, i32 5, i8 0, {properties}}}')
    source+='\n'+new+f' = !{{i32 4, !"SV_ClipDistance", i8 9, i8 6, {zero}, i8 2, i32 1, i8 1, i32 4, i8 0, null}}\n'
    source,n=re.subn(r'(@dx.op.loadInput.i32\(i32 4, i32 )4(, i32 0, i8 0, i32 undef\))',r'\g<1>5\2',source)
    if n!=1:raise ValueError('Expected exactly one generated front-face read')
    instructions=lambda s:[line for line in s.splitlines() if line.strip() and not line.startswith(('!',';'))]
    restored=re.sub(r'(@dx.op.loadInput.i32\(i32 4, i32 )5(, i32 0, i8 0, i32 undef\))',r'\g<1>4\2',source)
    assert instructions(restored)==instructions(original),'Material instructions changed'
    return source

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('target',type=Path);a=p.parse_args()
    a.target.write_text(link(a.source.read_text(encoding='utf-8')),encoding='utf-8')
