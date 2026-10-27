"""Bytes and disassembly at an RVA of either build.

    py -3.13 -B tools/patchday/peek.py 0x5fbe21 [0x...] [--old] [-n 8] [--bytes 20] [--before 3]

Prints the containing function, the raw bytes (what a code guard holds) and the instructions from the address on;
call/jump targets and RIP operands are annotated with the address-library hashes that name them.
"""
import argparse
import os
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from gone_find import Build  # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def names(b, rva):
    return ','.join(str(h) for h in b.lib.exact(rva)) or '-'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rvas', nargs='+')
    ap.add_argument('--old', action='store_true')
    ap.add_argument('-n', type=int, default=8)
    ap.add_argument('--bytes', type=int, default=20)
    ap.add_argument('--before', type=int, default=0)
    a = ap.parse_args()
    b = Build(os.path.join(REPO, 'build', 'patchday', 'base-2.31' if a.old else 'base-2.5'))
    for s in a.rvas:
        rva = int(s, 16)
        f = b.img.function_at(rva)
        print(f"== {rva:#x} in {f[0]:#x}+{rva - f[0]:#x} (func names {names(b, f[0])})" if f else f'== {rva:#x}')
        print('   bytes', bytes(b.img.read(rva, a.bytes)).hex())
        start = rva
        if a.before and f:
            ins = list(md.disasm(bytes(b.img.read(f[0], rva - f[0])), f[0]))
            start = ins[-a.before].address if len(ins) >= a.before else f[0]
        for k, i in enumerate(md.disasm(bytes(b.img.read(start, 16 * (a.n + a.before))), start)):
            if k >= a.n + a.before:
                break
            note = ''
            if i.mnemonic in ('call', 'jmp') or i.mnemonic.startswith('j'):
                op = i.operands[0] if i.operands else None
                if op is not None and op.type == capstone.x86.X86_OP_IMM:
                    note = f'  ; -> {names(b, op.imm)}'
            for op in i.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    t = i.address + i.size + op.mem.disp
                    note += f'  ; [{t:#x}] {names(b, t)}'
            print(f"   {'>' if i.address == rva else ' '} {i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}{note}")


if __name__ == '__main__':
    main()
