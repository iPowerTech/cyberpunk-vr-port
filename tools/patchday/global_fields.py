"""Which fields of a global object the code reads, in two builds: field-offset histograms side by side.

    py -3.13 -B tools/patchday/global_fields.py --old-global 0x342ac00 --new-global 0x3bbfb38 [--top 60]

For every `mov reg, [rip+global]` in .text the next few instructions are followed while that register keeps the
object pointer; each `[reg+disp]` it is used with counts as one access to field `disp`. A field that moved between
builds shows up as the same count at a different offset (an inserted member shifts everything after it), which
pins the insertion point of a hand-written SDK struct (CGameEngine, CBaseEngine, ...) without decompiling.
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
from pe import Image, find_game_files  # noqa: E402

BASE = 0x140000000


def histogram(game, glob, window):
    img = Image(find_game_files(game)[0])
    text = next(s for s in img.sections if s.name == '.text')
    code = img.data[text.raw:text.raw + text.rsize]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    hist = collections.Counter()
    target = BASE + glob
    # 48/4C 8B ?? [rip+disp32]: find candidate loads by scanning for the 7-byte form
    for i in range(len(code) - 7):
        if code[i] not in (0x48, 0x4C) or code[i + 1] != 0x8B or (code[i + 2] & 0xC7) != 0x05:
            continue
        disp = int.from_bytes(code[i + 3:i + 7], 'little', signed=True)
        va = BASE + text.va + i
        if va + 7 + disp != target:
            continue
        insns = list(md.disasm(code[i:i + 7 + 15 * window], va, count=window + 1))
        if not insns:
            continue
        reg = insns[0].operands[0].reg
        for ins in insns[1:]:
            stop = False
            for op in ins.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base == reg and op.mem.index == 0:
                    hist[op.mem.disp] += 1
            # the register is overwritten (first operand written and is the same reg) -> stop following
            if ins.operands and ins.operands[0].type == x86.X86_OP_REG and ins.operands[0].reg == reg and \
                    ins.mnemonic in ('mov', 'lea', 'movzx', 'xor', 'pop'):
                stop = True
            if ins.mnemonic in ('call', 'jmp', 'ret'):
                stop = True
            if stop:
                break
    return hist


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--old-global', required=True)
    ap.add_argument('--new-global', required=True)
    ap.add_argument('--window', type=int, default=6)
    ap.add_argument('--top', type=int, default=80)
    a = ap.parse_args()
    ho = histogram(a.old, int(a.old_global, 16), a.window)
    hn = histogram(a.new, int(a.new_global, 16), a.window)
    offs = sorted(set(ho) | set(hn))
    print(f'{"offset":>8} {"old":>6} {"new":>6}')
    for o in offs:
        if ho[o] + hn[o] >= 2:
            print(f'{o:#8x} {ho[o]:6} {hn[o]:6}')


if __name__ == '__main__':
    main()
