"""A vtable of the new build next to the same vtable of the old one, slot by slot.

    py -3.13 -B tools/patchday/vtable_diff.py --new-vtable 0x3577168 [--old-vtable 0x...] [--names sdk_header.hpp]

Each slot's function is carried between builds by its address-library hash (unique on both). Without
--old-vtable the old table is found as the .rdata slot run that holds the carried functions. Prints per new
slot: offset, new function, the old slot that holds the same function (or NEW), and the masked-code verdict.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import AddressLibrary, Image, find_game_files  # noqa: E402
import sigs  # noqa: E402

IMAGE_BASE = 0x140000000


def load(game):
    exe, lib = find_game_files(game)
    img = Image(exe)
    return img, AddressLibrary(lib, img)


def slots(img, vt, limit=400):
    out = []
    for i in range(limit):
        raw = img.read(vt + 8 * i, 8)
        if raw is None:
            break
        p, = struct.unpack('<Q', raw)
        r = p - IMAGE_BASE
        if not (0 <= r < (1 << 32)) or r not in img.text:
            break
        out.append(r)
    return out


def carry(src_lib, dst_lib, rva):
    for h in src_lib.exact(rva):
        if len(src_lib.by_hash.get(h, ())) == 1:
            r = dst_lib.unique(h)
            if r is not None:
                return r
    return None


def masked(img, rva):
    f = img.function_at(rva)
    if f is None or f[0] != rva:
        return None
    return sigs.masked_hash(img, sigs.decode(img, f[0], f[1])), f[1] - f[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--new-vtable', required=True)
    ap.add_argument('--old-vtable')
    a = ap.parse_args()
    oimg, olib = load(a.old)
    nimg, nlib = load(a.new)
    nvt = int(a.new_vtable, 16)
    nslots = slots(nimg, nvt)
    if a.old_vtable:
        ovt = int(a.old_vtable, 16)
    else:
        votes = {}
        data = oimg.data
        for i, r in enumerate(nslots):
            o = carry(nlib, olib, r)
            if o is None:
                continue
            needle = struct.pack('<Q', IMAGE_BASE + o)
            pos = data.find(needle)
            while pos >= 0:
                sec = next((s for s in oimg.sections if s.raw <= pos < s.raw + s.rsize), None)
                if sec is not None and sec.name == '.rdata':
                    rva = sec.va + pos - sec.raw
                    for k in range(-40, 41):
                        votes[rva - 8 * k] = votes.get(rva - 8 * k, 0) + 0  # placeholder keeps the dict small
                    votes[rva - 8 * i] = votes.get(rva - 8 * i, 0) + 1
                pos = data.find(needle, pos + 1)
        ovt = max(votes, key=votes.get)
        print(f'old vtable guessed at {ovt:#x} ({votes[ovt]} of {len(nslots)} slots agree)')
    oslots = slots(oimg, ovt)
    where = {r: i for i, r in enumerate(oslots)}
    print(f'new vtable {nvt:#x}: {len(nslots)} slots; old {ovt:#x}: {len(oslots)} slots')
    for i, r in enumerate(nslots):
        o = carry(nlib, olib, r)
        j = where.get(o) if o is not None else None
        verdict = ''
        if o is not None:
            mo, mn = masked(oimg, o), masked(nimg, r)
            verdict = 'same-code' if mo and mn and mo[0] == mn[0] else (f'changed {mo[1] if mo else "?"}->{mn[1] if mn else "?"}')
        old_txt = f'old slot {j} ({8 * j:#x})' if j is not None else ('NOT IN OLD TABLE' if o else 'NEW (no hash on old)')
        flag = '' if j == i else '  <-- moved' if j is not None else '  <-- new'
        print(f'{8 * i:#05x} [{i:3}] {r:#x}  {old_txt} {verdict}{flag}')


if __name__ == '__main__':
    main()
