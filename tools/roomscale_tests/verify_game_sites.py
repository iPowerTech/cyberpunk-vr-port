"""Read-only validation of the compiled hook sites against the installed PE."""
from pathlib import Path
import hashlib
import re
import struct
import sys

root = Path(__file__).resolve().parents[2]
data = Path(sys.argv[1]).read_bytes()
expected = "a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991"
assert hashlib.sha256(data).hexdigest() == expected, "EXE differs from the native RE baseline"
pe = struct.unpack_from("<I", data, 0x3c)[0]
assert data[pe:pe+4] == b"PE\0\0"
count = struct.unpack_from("<H", data, pe+6)[0]
opt_size = struct.unpack_from("<H", data, pe+20)[0]
imagebase = struct.unpack_from("<Q", data, pe+24+24)[0]
sections = [struct.unpack_from("<IIII", data, pe+24+opt_size+i*40+8) for i in range(count)]

def read_rva(rva, size):
    for vsize, va, raw_size, raw in sections:
        if va <= rva and rva + size <= va + raw_size:
            return data[raw + rva - va:raw + rva - va + size]
    raise AssertionError(f"unmapped RVA {rva:#x}")

header = (root / "include/Hooks/RoomscaleSites.hpp").read_text()
sites = re.findall(r'Site\s+(\w+)\{(0x[0-9A-Fa-f]+),\s*"([0-9A-Fa-f]+)"\}', header)
assert len(sites) == 6
assert read_rva(0x4c6c9c,15)==bytes.fromhex('488B41088B4914488D0C4948C1E104'), 'native swimming action reader'
for name, rva, code in sites:
    expected_bytes = bytes.fromhex(code)
    assert read_rva(int(rva, 16), len(expected_bytes)) == expected_bytes, name
    print(f"PASS {name}: RVA {rva}, {len(expected_bytes)} original bytes")
call = read_rva(0x6abbaf, 5)
assert call[0] == 0xe8 and 0x6abbb4 + struct.unpack_from("<i", call, 1)[0] == 0x20479ec
for address, expected_function in ((0x2b5d3c0, 0x8e869c), (0x2b5d3c8, 0x4c2018),
                                   (0x2ac9bb0, 0x926020), (0x2b5d490, 0x57b714)):
    assert struct.unpack("<Q", read_rva(address, 8))[0] == imagebase + expected_function
for ret in (0x29840c6, 0x2984f56):
    call=read_rva(ret-5,5)
    assert call[0]==0xe8 and ret+struct.unpack_from("<i",call,1)[0]==0x20479ec
print("PASS native dispatch slots and solver-only velocity return address")

body_source=(root / 'src/Hooks/BodyHeading.cpp').read_text()
body_sites=re.findall(r'Matches\((0x[0-9A-Fa-f]+),"([0-9A-Fa-f]+)"\)',body_source)
assert len(body_sites)==2
for rva,code in body_sites:
    original=bytes.fromhex(code)
    assert read_rva(int(rva,16),len(original))==original, 'native body source/commit observer signature'
assert struct.unpack('<Q',read_rva(0x2b5d3d0,8))[0]==imagebase+0xd455b0
print('PASS body motion provider slot+20 and persistent adapter commit ABI')
