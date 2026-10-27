"""Where did a virtual slot go: find a native class's vtable through the REDengine RTTI in two builds and map slots.

    py -3.13 -B tools/patchday/vtslots.py gameJournalManager 0x220 0x268
    py -3.13 -B tools/patchday/vtslots.py --frameworks     # every Core::RawVFunc in the psiberx checkouts

A patch that adds a virtual to a base class shifts every later slot, and nothing in the RTTI sizes shows it.
The game has no MSVC RTTI for its own classes, so the vtable comes from the native type registration: the
function that references the RTTI name string stores a TTypedClass<T> vtable into the static CClass
(`lea rX, [vtbl]; mov [cls], rX`), its ConstructCls slot (0xD8) forwards to T's constructor, and the last
`lea rX, [vtbl]; mov [this], rX` there is T's own vtable. A slot is carried by the address-library hash of the
function it holds (unique in both builds), or else by identical code bytes; the new offset is where that
function sits in the new vtable.
"""
import argparse
import os
import re
import struct
import sys

import capstone
from capstone import x86

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from gone_find import Build, carry  # noqa: E402

BASE = 0x140000000
CONSTRUCT_CLS = 0xD8

# framework class -> RTTI names of the runtime classes whose vtable the slot indexes
FRAMEWORK_CLASSES = {
    'Red::ICommunitySystem': ['gameCommunitySystem'],
    'Red::IComponent': ['entMeshComponent', 'entSkinnedMeshComponent', 'entGarmentSkinnedMeshComponent'],
    'Red::ItemObject': ['gameItemObject'],
    'Red::gameIJournalManager': ['gameJournalManager'],
    'Red::gamemappinsIMappinSystem': ['gamemappinsMappinSystem'],
    'Red::questIQuestsSystem': ['questQuestsSystem'],
    'Red::questPhaseInstance': ['questPhaseInstance'],
    'Red::worldNodeInstanceRegistry': ['worldNodeInstanceRegistry'],
}

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def section(b, name):
    return next(s for s in b.img.sections if s.name == name)


def qword(b, rva):
    raw = b.img.read(rva, 8)
    return struct.unpack('<Q', raw)[0] - BASE if raw and len(raw) == 8 else None


def is_code(b, rva):
    t = section(b, '.text')
    return rva is not None and t.va <= rva < t.va + t.vsize


def insns(b, start, limit=0x2000):
    fi = b.index_of(start)
    end = int(b.ends[fi]) if fi >= 0 and int(b.begins[fi]) == start else start + 0x80
    return list(md.disasm(bytes(b.img.read(start, min(end - start, limit))), start))


def rip_target(i, op):
    return i.address + i.size + op.mem.disp


def vtable_stores(b, start, this_only=False):
    """(vtable rva, destination) for each `lea rX, [rip+V]` stored by the next instructions."""
    out = []
    ins = insns(b, start)
    for k, i in enumerate(ins):
        if i.mnemonic != 'lea' or len(i.operands) != 2 or i.operands[1].type != x86.X86_OP_MEM or                 i.operands[1].mem.base != x86.X86_REG_RIP:
            continue
        v = rip_target(i, i.operands[1])
        reg = i.operands[0].reg
        for j in ins[k + 1:k + 4]:
            if j.mnemonic == 'mov' and len(j.operands) == 2 and j.operands[1].type == x86.X86_OP_REG and                     j.operands[1].reg == reg and j.operands[0].type == x86.X86_OP_MEM:
                m = j.operands[0].mem
                if m.base == x86.X86_REG_RIP:
                    out.append((v, ('rip', rip_target(j, j.operands[0]))))
                elif m.disp == 0 and m.index == 0:
                    out.append((v, ('this', j.reg_name(m.base))))
                break
    return out


def follow_construct(b, f, depth=0):
    """the constructor a ConstructCls slot forwards to (mov rcx, rdx; jmp/call ctor)"""
    ins = insns(b, f, 0x60)
    for i in ins[:6]:
        if i.mnemonic in ('jmp', 'call') and i.operands and i.operands[0].type == x86.X86_OP_IMM:
            return i.operands[0].imm
    return f


def vtables(b, name):
    """vtables of the native class registered under an RTTI name"""
    rd = section(b, '.rdata')
    data = bytes(b.img.data[rd.raw:rd.raw + rd.rsize])
    needle = b'\0' + name.encode() + b'\0'
    res = []
    p = data.find(needle)
    while p >= 0:
        srva = rd.va + p + 1
        users = set(int(b.begins[f]) for f in b.ref_func[b.ref_target == srva])
        for u in users:
            for v, dest in vtable_stores(b, u):
                if dest[0] != 'rip':
                    continue
                cc = qword(b, v + CONSTRUCT_CLS)
                if not is_code(b, cc):
                    continue
                ctor = follow_construct(b, cc)
                own = [w for w, d in vtable_stores(b, ctor) if d[0] == 'this']
                if own:
                    res.append(own[-1])
        p = data.find(needle, p + 1)
    return sorted(set(res))


def slots(b, vt, n=400):
    rd = section(b, '.rdata')
    text = section(b, '.text')
    out = []
    for i in range(n):
        raw = b.img.read(vt + 8 * i, 8)
        if not raw or len(raw) < 8:
            break
        v = struct.unpack('<Q', raw)[0] - BASE
        if not (text.va <= v < text.va + text.vsize):
            break
        out.append(v)
    return out


def body(b, rva, n=48):
    return bytes(b.img.read(rva, n))


def anchors(old, new, os_, ns_):
    """old slot -> new slot for functions that sit in exactly one slot of each vtable"""
    out = {}
    for i, f in enumerate(os_):
        if os_.count(f) != 1:
            continue
        nf = carry(old, new, f)
        if nf is not None and ns_.count(nf) == 1:
            out[i] = ns_.index(nf)
    return out


def map_slot(old, new, ovt, nvt, off):
    """new offset of an old slot: the slot's own function when it is unambiguous, else the shift its nearest
    unambiguous neighbours agree on"""
    os_, ns_ = slots(old, ovt), slots(new, nvt)
    i = off // 8
    if i >= len(os_):
        return None, 'slot past old vtable end'
    size = f'{len(os_)} -> {len(ns_)} slots'
    f = os_[i]
    nf = carry(old, new, f)
    if nf is None:
        cand = [j for j, g in enumerate(ns_) if body(new, g) == body(old, f)]
        if len(cand) == 1:
            return cand[0] * 8, f'bytes (old {f:#x} -> new {ns_[cand[0]]:#x}); {size}'
    elif ns_.count(nf) == 1 and os_.count(f) == 1:
        return ns_.index(nf) * 8, f'hash (old {f:#x} -> new {nf:#x}); {size}'
    an = anchors(old, new, os_, ns_)
    lo = max((j for j in an if j < i), default=None)
    hi = min((j for j in an if j > i), default=None)
    shifts = {an[j] - j for j in (lo, hi) if j is not None}
    if len(shifts) == 1:
        d = shifts.pop()
        ok = nf is None or (0 <= i + d < len(ns_) and ns_[i + d] == nf)
        return (i + d) * 8, (f'neighbours {lo and hex(lo * 8)}/{hi and hex(hi * 8)} shift {d:+d}'
                             f'{"" if ok else " (slot function differs!)"}; {size}')
    return None, f'ambiguous: neighbour shifts {sorted(an[j] - j for j in (lo, hi) if j is not None)}; {size}'


def frameworks():
    src = os.path.join(REPO, 'build', 'patchday', 'framework-src')
    rows = []
    for fw in ('cp2077-archive-xl', 'cp2077-codeware', 'cp2077-tweak-xl'):
        for d, _, names in os.walk(os.path.join(src, fw, 'src')):
            for nm in names:
                if not nm.endswith(('.hpp', '.cpp')):
                    continue
                p = os.path.join(d, nm)
                s = re.sub(r'//[^\n]*', '', open(p, encoding='utf-8', errors='replace').read())
                for m in re.finditer(r'constexpr auto (\w+) = Core::RawVFunc<\s*/\*\s*(?:offset|addr)\s*=\s*\*/\s*'
                                     r'(0x[0-9A-Fa-f]+),\s*/\*\s*type\s*=\s*\*/\s*(.+?)>\(\);', s, re.S):
                    cls = re.search(r'\(([\w:]+)::\*\)', m.group(3))
                    rows.append((fw.split('-', 1)[1], os.path.relpath(p, os.path.join(src, fw)), m.group(1),
                                 int(m.group(2), 16), cls.group(1) if cls else '?'))
    return sorted(set(rows), key=lambda r: (r[4], r[3]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('type', nargs='?')
    ap.add_argument('offsets', nargs='*')
    ap.add_argument('--frameworks', action='store_true')
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    a = ap.parse_args()
    old, new = Build(a.old), Build(a.new)
    if a.type:
        ov, nv = vtables(old, a.type), vtables(new, a.type)
        print(f'{a.type}: old vtables {[hex(x) for x in ov]} ({[len(slots(old, x)) for x in ov]} slots), '
              f'new {[hex(x) for x in nv]} ({[len(slots(new, x)) for x in nv]} slots)')
        for o in a.offsets:
            off = int(o, 16)
            for ovt, nvt in zip(ov, nv):
                print(f'  {off:#x} -> ', *map_slot(old, new, ovt, nvt, off))
    if a.frameworks:
        for fw, path, name, off, cls in frameworks():
            types = FRAMEWORK_CLASSES.get(cls, [])
            res = []
            for t in types:
                ov, nv = vtables(old, t), vtables(new, t)
                if len(ov) != 1 or len(nv) != 1:
                    res.append(f'{t}: {len(ov)}/{len(nv)} vtables')
                    continue
                noff, how = map_slot(old, new, ov[0], nv[0], off)
                res.append(f'{t}: {"SAME" if noff == off else (hex(noff) if noff is not None else "??")} {how}')
                break
            print(f'{fw:10} {name:28} {off:#6x} {cls:28} ' + ' | '.join(res))


if __name__ == '__main__':
    main()
