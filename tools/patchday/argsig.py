"""Which arguments does a function read: compare the register/stack argument use of the same function in two builds.

    py -3.13 -B tools/patchday/argsig.py --frameworks            # every psiberx Library.hpp hash, old vs new
    py -3.13 -B tools/patchday/argsig.py 0x210a360 [--build new]   # one function
    py -3.13 -B tools/patchday/argsig.py --map build/patchday/check-6ad55daf/remap/map.json
                                                                   # every function the port names (remap's map)

A patch that adds a parameter keeps the hash's name but breaks every hook declared with the old signature: the
detour forwards only the declared arguments, so the original reads garbage in the new one (2.5: TweakDB::Load got
a bool in rdx, TweakDB::TryLoad a fifth stack argument). Decompiler prototypes are noisy, so this walks the
function's code (its pdata range, plus chunks that chain back to it) in address order and records

  - rcx, rdx, r8, r9 / xmm0-xmm3 read before the first write (any width),
  - stack arguments: loads from [rsp + x] / [rbp + x] that land at or above the entry rsp + 0x28,

tracking rsp through push / sub rsp / mov rax, rsp / lea rbp, [rsp|rax + x]. Branches are not followed, so a
register written on one path and read on another counts as written; the result is a lower bound on the argument
count and a difference between builds is a lead, not a verdict.
"""
import argparse
import os
import re
import sys

import capstone
from capstone import x86

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from gone_find import Build  # noqa: E402

ARGS = ['rcx', 'rdx', 'r8', 'r9']
FAMILY = {
    'rcx': 'rcx', 'ecx': 'rcx', 'cx': 'rcx', 'cl': 'rcx', 'ch': 'rcx',
    'rdx': 'rdx', 'edx': 'rdx', 'dx': 'rdx', 'dl': 'rdx', 'dh': 'rdx',
    'r8': 'r8', 'r8d': 'r8', 'r8w': 'r8', 'r8b': 'r8',
    'r9': 'r9', 'r9d': 'r9', 'r9w': 'r9', 'r9b': 'r9',
    'xmm0': 'xmm0', 'xmm1': 'xmm1', 'xmm2': 'xmm2', 'xmm3': 'xmm3',
}

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def chunks_of(b, start):
    """pdata ranges belonging to the function at start: its own and the chained-unwind fragments."""
    out = [(start, int(b.ends[b.index_of(start)]))]
    return out


def analyse(b, start, limit=4000):
    fi = b.index_of(start)
    if fi < 0:
        return None
    begin, end = int(b.begins[fi]), int(b.ends[fi])
    code = bytes(b.img.read(begin, end - begin))
    written, read = set(), set()
    stack_args = set()
    delta = 0          # entry rsp - current rsp
    rbp_base = None    # rbp = entry rsp + rbp_base
    rax_entry = False  # rax holds the entry rsp
    for n, ins in enumerate(md.disasm(code, begin)):
        if n > limit:
            break
        mn = ins.mnemonic
        try:
            regs_r, regs_w = ins.regs_access()
        except capstone.CsError:
            regs_r, regs_w = [], []
        rn = [ins.reg_name(r) for r in regs_r]
        wn = [ins.reg_name(r) for r in regs_w]
        # xor reg, reg and similar idioms write without a meaningful read
        zero_idiom = mn in ('xor', 'xorps', 'xorpd', 'pxor', 'sub') and len(ins.operands) == 2 and \
            ins.operands[0].type == x86.X86_OP_REG and ins.operands[1].type == x86.X86_OP_REG and \
            ins.operands[0].reg == ins.operands[1].reg
        if not zero_idiom:
            for r in rn:
                f = FAMILY.get(r)
                if f and f not in written:
                    read.add(f)
        for r in wn:
            f = FAMILY.get(r)
            if f:
                written.add(f)
        # stack tracking
        ops = ins.operands
        if mn == 'push':
            delta += 8
        elif mn == 'pop':
            delta -= 8
        elif mn == 'sub' and ops[0].type == x86.X86_OP_REG and ins.reg_name(ops[0].reg) == 'rsp' and ops[1].type == x86.X86_OP_IMM:
            delta += ops[1].imm
        elif mn == 'add' and ops[0].type == x86.X86_OP_REG and ins.reg_name(ops[0].reg) == 'rsp' and ops[1].type == x86.X86_OP_IMM:
            delta -= ops[1].imm
        elif mn == 'mov' and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG and \
                ins.reg_name(ops[0].reg) == 'rax' and ins.reg_name(ops[1].reg) == 'rsp' and delta == 0:
            rax_entry = True
        elif mn == 'lea' and ops[0].type == x86.X86_OP_REG and ins.reg_name(ops[0].reg) == 'rbp':
            m = ops[1].mem
            base = ins.reg_name(m.base) if m.base else None
            if base == 'rsp':
                rbp_base = m.disp - delta
            elif base == 'rax' and rax_entry:
                rbp_base = m.disp
        elif mn == 'mov' and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG and \
                ins.reg_name(ops[0].reg) == 'rbp' and ins.reg_name(ops[1].reg) == 'rsp':
            rbp_base = -delta
        if 'rax' in wn and not (mn == 'mov' and len(ops) == 2 and ops[1].type == x86.X86_OP_REG and
                                ins.reg_name(ops[1].reg) == 'rsp'):
            rax_entry = False
        # stack argument loads (a store to the home area is a spill, a load above +0x20 is an argument)
        for i, op in enumerate(ops):
            if op.type != x86.X86_OP_MEM:
                continue
            is_load = i > 0 or mn in ('cmp', 'test', 'push') or (mn.startswith('mov') and i == 1)
            if i == 0 and mn not in ('cmp', 'test', 'push', 'movss', 'movsd'):
                is_load = mn in ('cmp', 'test')
            if not is_load:
                continue
            m = op.mem
            base = ins.reg_name(m.base) if m.base else None
            off = None
            if base == 'rsp':
                off = m.disp - delta
            elif base == 'rbp' and rbp_base is not None:
                off = m.disp + rbp_base
            elif base == 'rax' and rax_entry:
                off = m.disp
            if off is not None and off >= 0x28 and off < 0x28 + 8 * 16 and (off - 0x28) % 8 == 0 and mn != 'lea':
                stack_args.add((off - 0x28) // 8 + 5)
    return {'regs': [r for r in ARGS if r in read], 'xmm': sorted(r for r in read if r.startswith('xmm')),
            'stack': sorted(stack_args), 'size': end - begin}


def fmt(a):
    if a is None:
        return 'n/a'
    return f"{','.join(a['regs']) or '-'} {','.join(a['xmm'])} stack{a['stack'] or '-'} ({a['size']} B)"


def frameworks(old, new):
    src = os.path.join(REPO, 'build', 'patchday', 'framework-src')
    seen = {}
    for fw in ('cp2077-archive-xl', 'cp2077-tweak-xl', 'cp2077-codeware'):
        p = os.path.join(src, fw, 'src', 'Red', 'Addresses', 'Library.hpp')
        txt = open(p, encoding='utf-8').read()
        for name, h in re.findall(r'constexpr uint32_t (\w+) = (\d+);', txt):
            seen.setdefault((name, int(h)), []).append(fw.split('-', 1)[1])
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rvas', nargs='*')
    ap.add_argument('--frameworks', action='store_true')
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--map', default=None, help="remap's map.json: compare every mapped function start")
    ap.add_argument('--all', action='store_true', help='also print functions whose argument use did not change')
    a = ap.parse_args()
    old, new = Build(a.old), Build(a.new)
    for r in a.rvas:
        rva = int(r, 16)
        print(f'{rva:#x}: old {fmt(analyse(old, rva))} | new {fmt(analyse(new, rva))}')
    if a.map:
        import json
        m = json.load(open(a.map, encoding='utf-8'))
        changed = compared = 0
        for k, v in sorted(m.items(), key=lambda kv: int(kv[0], 16)):
            o = int(k, 16)
            n = int(v['new'], 16) if v.get('new') else None
            # function starts only: the port detours or calls those; addresses inside a function are sites
            if n is None or old.index_of(o) < 0 or int(old.begins[old.index_of(o)]) != o or \
                    new.index_of(n) < 0 or int(new.begins[new.index_of(n)]) != n:
                continue
            ao, an = analyse(old, o), analyse(new, n)
            compared += 1
            same = ao['regs'] == an['regs'] and ao['xmm'] == an['xmm'] and ao['stack'] == an['stack']
            changed += not same
            if same and not a.all:
                continue
            print(f"{'  ' if same else '!!'} old {o:#x} -> new {n:#x} [{v['status']}] old {fmt(ao)} | new {fmt(an)}")
        print(f'{compared} mapped function starts compared, {changed} with a different argument use')
    if a.frameworks:
        # the 2.5 hashes in the checkouts; the 2.31 function comes from the hash the 2.31 release used, so
        # pair by name through the old checkout copies when the hash changed
        old_hashes = {}
        pre = os.path.join(REPO, 'build', 'patchday', 'framework_hashes_v2.json')
        if os.path.exists(pre):
            import json
            for row in json.load(open(pre, encoding='utf-8')):
                for nm in row.get('names', []):
                    if row.get('old'):
                        old_hashes.setdefault(nm, row['old'][0])
        for (name, h), fws in sorted(frameworks(old, new).items()):
            nr = new.lib.unique(h)
            orv = old.lib.unique(h)
            if orv is None and old_hashes.get(name):
                orv = int(old_hashes[name])
            ao = analyse(old, orv) if orv is not None else None
            an = analyse(new, nr) if nr is not None else None
            same = ao is not None and an is not None and ao['regs'] == an['regs'] and ao['xmm'] == an['xmm'] and \
                ao['stack'] == an['stack']
            if same and not a.all:
                continue
            mark = '  ' if same else '!!'
            print(f"{mark} {name} [{'/'.join(fws)}] old {orv and hex(orv)} {fmt(ao)} | new {nr and hex(nr)} {fmt(an)}")


if __name__ == '__main__':
    main()
