import hashlib,struct,sys
from pathlib import Path
p=Path(sys.argv[1]);data=p.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
count=struct.unpack_from('<H',data,pe+6)[0];opt=struct.unpack_from('<H',data,pe+20)[0]
sections=[struct.unpack_from('<4I',data,pe+24+opt+i*40+8) for i in range(count)]
def offset(rva):
 for vs,va,rs,raw in sections:
  if va<=rva<va+rs:return raw+rva-va
 raise RuntimeError(hex(rva))
assert struct.unpack_from('<H',data,pe+24)[0]==0x20b
export=offset(struct.unpack_from('<I',data,pe+24+112)[0]);n,table=struct.unpack_from('<I',data,export+24)[0],offset(struct.unpack_from('<I',data,export+32)[0])
names=[]
for i in range(n):
 at=offset(struct.unpack_from('<I',data,table+4*i)[0]);end=data.index(b'\0',at);names.append(data[at:end].decode())
required=['CyberpunkVR_PanzerWeaponAimDebug','CyberpunkVR_PanzerWeaponAimDebugSeq','CyberpunkVR_PanzerCannonDebug','CyberpunkVR_PanzerCannonDebugSeq']
assert set(required)<=set(names),'The installed aiming translation unit is missing: '+str(set(required)-set(names))
print('PASS exports',required,'SHA256',hashlib.sha256(data).hexdigest())
