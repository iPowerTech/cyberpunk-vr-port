"""Where did a hand-reversed field go: functions of the old build that touch several of the given offsets,
carried to the new build by address-library hash, with the offsets they touch there.

    py -3.13 -B tools/patchday/field_carry.py 0x25C 0x264 0x268 0x2A3 0x2BC [--min 2] [--old ..] [--new ..]

A class whose RTTI-listed fields moved by +N usually moved its hand-reversed (non-RTTI) fields too, but not
always by the same N (a member inserted between them). Every [reg+disp] operand with disp in the given set is
counted per function; functions with at least --min distinct hits are carried to the new build, where the
displacements within +-0x40 of each wanted offset are printed, so the shift of each field can be read off.
"""
import argparse
import collections
import os
import sys

import capstone
from capstone import x86

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import AddressLibrary, Image, find_game_files  # noqa: E402


def disps(img, func, md):
    raw = img.read(func[0], func[1] - func[0])
    out = collections.Counter()
    if raw is None:
        return out
    for ins in md.disasm(raw, func[0]):
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM and op.mem.base not in (0, x86.X86_REG_RIP, x86.X86_REG_RSP, x86.X86_REG_RBP):
                out[op.mem.disp] += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('offsets', nargs='+')
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--min', type=int, default=2)
    ap.add_argument('--top', type=int, default=25)
    a = ap.parse_args()
    want = [int(x, 16) for x in a.offsets]
    oe, ol = find_game_files(a.old)
    ne, nl = find_game_files(a.new)
    oimg, nimg = Image(oe), Image(ne)
    OL, NL = AddressLibrary(ol, oimg), AddressLibrary(nl, nimg)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    # prefilter on the raw disp32 bytes (every wanted offset is > 0x7F, so it is encoded as disp32), then decode
    import numpy as np
    text = next(s for s in oimg.sections if s.name == '.text')
    code = oimg.data[text.raw:text.raw + text.rsize]
    begins, ends, _ = oimg.pdata
    per_func = collections.defaultdict(set)
    for w in want:
        needle = w.to_bytes(4, 'little')
        pos = code.find(needle)
        while pos >= 0:
            rva = text.va + pos
            i = int(np.searchsorted(begins, rva, side='right')) - 1
            if i >= 0 and begins[i] <= rva < ends[i]:
                per_func[i].add(w)
            pos = code.find(needle, pos + 1)
    found = []
    for i, ws in per_func.items():
        if len(ws) < a.min:
            continue
        f = (int(begins[i]), int(ends[i]))
        d = disps(oimg, f, md)
        hit = [w for w in want if d.get(w)]
        if len(hit) >= a.min:
            found.append((len(hit), f, hit))
    found.sort(key=lambda x: -x[0])
    for n, f, hit in found[:a.top]:
        new = None
        for h in OL.exact(f[0]):
            if len(OL.by_hash.get(h, ())) == 1 and NL.unique(h) is not None:
                new = NL.unique(h)
                break
        line = f'old {f[0]:#x} hits {[hex(x) for x in hit]}'
        if new is None:
            print(line, '-> no hash')
            continue
        nf = nimg.function_at(new)
        nd = disps(nimg, nf, md) if nf else {}
        near = {hex(w): sorted(hex(k) for k in nd if abs(k - w) <= 0x40) for w in want}
        print(line, f'-> new {new:#x}', near)


if __name__ == '__main__':
    main()
