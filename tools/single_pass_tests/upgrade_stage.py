"""Preserve shader instructions while rebuilding SM6.1 dependency metadata."""
import argparse
from pathlib import Path
import re

def upgrade(source):
    original=source
    for key,pattern,replacement in (
        ('dx.shaderModel',r'!"(vs|ps|gs|hs|ds)", i32 6, i32 0',r'!"\1", i32 6, i32 1'),
        ('dx.version',r'i32 1, i32 0','i32 1, i32 1')):
        ref=re.search(r'(?m)^!'+re.escape(key)+r' = !\{(!\d+)\}$',source)
        if not ref:raise ValueError('Missing '+key)
        md=re.search(r'(?m)^'+re.escape(ref[1])+r' = !\{(.*)\}$',source)
        value,count=re.subn(pattern,replacement,md[1])
        if count!=1:raise ValueError('Expected an unmodified SM6.0/DXIL1.0 graphics stage')
        source=source.replace(md[0],ref[1]+' = !{'+value+'}')
    source=re.sub(r'(?m)^!dx.viewIdState = .*\n','',source)
    # Executable IR stays byte-for-byte unchanged. DXC regenerates PSV metadata.
    body=lambda s:'\n'.join(line for line in s.splitlines() if not line.startswith('!'))
    assert body(source)==body(original)
    return source

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('target',type=Path);a=p.parse_args()
    a.target.write_text(upgrade(a.source.read_text(encoding='utf-8')),encoding='utf-8')
