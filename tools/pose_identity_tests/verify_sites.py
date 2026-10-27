"""Read-only verification of pose-transport ABI sites in the installed game."""
import hashlib,re,struct,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
data=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(data).hexdigest()=='a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991'
pe=struct.unpack_from('<I',data,0x3c)[0]
count=struct.unpack_from('<H',data,pe+6)[0];opt=struct.unpack_from('<H',data,pe+20)[0]
sections=[struct.unpack_from('<IIII',data,pe+24+opt+i*40+8) for i in range(count)]
def read(rva,n):
    for vs,va,rs,raw in sections:
        if va<=rva and rva+n<=va+rs:return data[raw+rva-va:raw+rva-va+n]
    raise AssertionError(hex(rva))
header=(root/'include/Hooks/CameraPoseSites.hpp').read_text()
sites=re.findall(r'Site\s+(\w+)\{(0x[0-9A-Fa-f]+),\s*"([0-9A-Fa-f]+)"\}',header)
assert len(sites)==28
for name,rva,encoded in sites:
    expected=bytes.fromhex(encoded);assert read(int(rva,16),len(expected))==expected,name
    print('PASS',name,rva)
call=read(0x4e50f9,5)
assert call[0]==0xe8 and 0x4e50fe+struct.unpack_from('<i',call,1)[0]==0x4e4030
for site,target in [(0x25faa04,0x2611a30),(0x25faa39,0x2611afc),(0x1ecd51c,0x25f9a7c)]:
    call=read(site,5)
    assert call[0]==0xe8 and site+5+struct.unpack_from('<i',call,1)[0]==target
