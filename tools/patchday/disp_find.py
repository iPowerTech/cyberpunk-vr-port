"""Which instructions of a function use a given displacement or immediate, in either build.

    py -3.13 -B tools/patchday/disp_find.py 0x12752c 0x4c0 0xa0 [--new 0x5fb8f0] [--fragments]

For a struct offset the port reads (director+0x4C0, an entry stride 0xA0): lists every instruction of the old
function (and of the new one with --new) whose memory displacement or immediate equals one of the values, so a
moved field shows up as the same instruction with another number.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import capstone  # noqa: E402
from capstone import x86  # noqa: E402
from gone_find import Build  # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def scan(b, f, values, fragments=False):
    ranges = [(f[0], f[1])]
    if fragments:
        bs, es, _ = b.img.pdata
        for x, y in zip(bs.tolist(), es.tolist()):
            g = b.img.function_at(x)
            if g and g[2] == f[0] and g[0] != f[0]:
                ranges.append((g[0], g[1]))
    out = []
    for lo, hi in ranges:
        for i in md.disasm(bytes(b.img.read(lo, hi - lo)), lo):
            for op in i.operands:
                v = op.mem.disp if op.type == x86.X86_OP_MEM else op.imm if op.type == x86.X86_OP_IMM else None
                if v is not None and (v in values or -v in values):
                    out.append(f'   {i.address:#x} {i.mnemonic} {i.op_str}')
                    break
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('func')
    ap.add_argument('values', nargs='+')
    ap.add_argument('--new')
    ap.add_argument('--fragments', action='store_true')
    a = ap.parse_args()
    vals = {int(v, 16) for v in a.values}
    old = Build(os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    f = old.img.function_at(int(a.func, 16))
    print(f'old {f[0]:#x}-{f[1]:#x}')
    print('\n'.join(scan(old, f, vals, a.fragments)))
    if a.new:
        new = Build(os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
        g = new.img.function_at(int(a.new, 16))
        print(f'new {g[0]:#x}-{g[1]:#x}')
        print('\n'.join(scan(new, g, vals, a.fragments)))


if __name__ == '__main__':
    main()
