"""Every shader in the game's engine\\shader_final.cache, written as <FNV-1a>.dxil -- the same name the plugin
looks twins up by (Render/HiddenAreaShaders.cpp hashes the bytecode the engine hands D3D12, which is the
container as stored here). Containers are found by their DXBC magic and accepted only when their size field
fits and the DXBC hash checks out. --kind vs|ps|cs keeps one stage (the DXIL part's program header).

  py -3.13 -B extract_shader_cache.py --cache "...\\engine\\shader_final.cache" --out DIR [--kind vs]"""
import argparse
import mmap
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import dxil_container as dc

KINDS = {'ps': 0, 'vs': 1, 'gs': 2, 'hs': 3, 'ds': 4, 'cs': 5}


def fnv1a(data):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def program_kind(container):
    count, = struct.unpack_from('<I', container, 28)
    for i in range(count):
        off, = struct.unpack_from('<I', container, 32 + 4 * i)
        if container[off:off + 4] == b'DXIL':
            version, = struct.unpack_from('<I', container, off + 8)
            return version >> 16
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cache', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--kind', choices=sorted(KINDS))
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    want = KINDS.get(args.kind) if args.kind else None
    found = kept = bad = 0
    with open(args.cache, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        pos = 0
        while True:
            i = mm.find(b'DXBC', pos)
            if i < 0:
                break
            size, = struct.unpack_from('<I', mm, i + 24)
            if 32 < size < 16 << 20 and i + size <= mm.size():
                c = bytes(mm[i:i + size])
                if dc.dxil_hash(c) == c[4:20]:
                    found += 1
                    if want is None or program_kind(c) == want:
                        name = args.out / ('%016x.dxil' % fnv1a(c))
                        if not name.exists():
                            name.write_bytes(c)
                        kept += 1
                    pos = i + size
                    continue
                bad += 1
            pos = i + 4
    print('containers', found, 'kept', kept, 'rejected', bad)


if __name__ == '__main__':
    main()
