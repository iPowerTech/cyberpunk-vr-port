"""Which struct offsets moved: a census of memory displacements over every function both builds name the same.

    py -3.13 -B tools/patchday/disp_drift.py census [--out build/patchday/disp_drift-2.5.json] [--min-disp 0x20]
    py -3.13 -B tools/patchday/disp_drift.py port   [--census ...]   # port literals whose value drifted

A patch that grows an engine struct moves every field behind the insertion, and nothing in the address library or
the RTTI says so for a class the RTTI does not describe. The functions that touch the field still do, though: for
each function whose address-library hash is unique in both builds, the instructions are aligned (mnemonic and
operand shape, displacements left out), and every aligned pair of memory operands with the same base register
role records old displacement -> new displacement. A field that moved shows up as one old value mapping to one
new value in many functions (2.31 -> 2.5: 0x13BC4A8 -> 0x14104A8, the render device grew by 0x54000).

`port` lists the hex literals of src/ and include/ that look like offsets (used with +, as Read/At/offset
arguments) and whose census entry says the value moved in most of the functions that use it.
"""
import argparse
import collections
import difflib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import capstone  # noqa: E402
from capstone import x86  # noqa: E402
from gone_find import Build  # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
DISP = re.compile(r'([+-]) 0x[0-9a-f]+\]')


def decode(b, f, limit=6000):
    code = bytes(b.img.read(f[0], f[1] - f[0]))
    out = []
    for i in md.disasm(code, f[0]):
        if len(out) >= limit:
            break
        disps = []
        for op in i.operands:
            if op.type == x86.X86_OP_MEM and op.mem.base not in (0, x86.X86_REG_RIP):
                disps.append(op.mem.disp)
        shape = DISP.sub(r'\1 D]', i.op_str) if disps else i.op_str
        if i.mnemonic == 'call' or i.mnemonic.startswith('j'):
            shape = 'T'
        elif 'rip' in shape:
            shape = re.sub(r'\[rip [+-] 0x[0-9a-f]+\]', '[R]', shape)
        out.append((f'{i.mnemonic} {shape}', disps))
    return out


def census(a):
    old, new = Build(a.old), Build(a.new)
    pairs = []
    for h, rvas in old.lib.by_hash.items():
        if len(rvas) != 1:
            continue
        n = new.lib.by_hash.get(h)
        if not n or len(n) != 1:
            continue
        of, nf = old.img.function_at(rvas[0]), new.img.function_at(n[0])
        if of and nf and of[0] == rvas[0] and nf[0] == n[0]:
            pairs.append((of, nf))
    pairs = sorted(set(pairs))
    print(f'{len(pairs)} function pairs', flush=True)
    moved = collections.defaultdict(collections.Counter)    # old disp -> new disp -> functions
    stay = collections.Counter()                            # old disp -> functions where it stayed
    examples = {}
    for k, (of, nf) in enumerate(pairs):
        if k % 5000 == 0:
            print(f'  {k}/{len(pairs)}', flush=True)
        if of[1] - of[0] > 40000 or nf[1] - nf[0] > 40000:
            continue
        try:
            io, inn = decode(old, of), decode(new, nf)
        except Exception:
            continue
        to, tn = [t for t, _ in io], [t for t, _ in inn]
        if to == tn:
            blocks = [(0, 0, len(to))]
        else:
            if len(to) * len(tn) > 4_000_000:
                continue
            blocks = difflib.SequenceMatcher(None, to, tn, autojunk=False).get_matching_blocks()
        seen_m, seen_s = set(), set()
        for i0, j0, size in blocks:
            for d in range(size):
                do, dn = io[i0 + d][1], inn[j0 + d][1]
                for x, y in zip(do, dn):
                    if abs(x) < a.min_disp:
                        continue
                    if x == y:
                        seen_s.add(x)
                    else:
                        seen_m.add((x, y))
                        examples.setdefault((x, y), (hex(of[0] + 0), hex(nf[0])))
        for x in seen_s:
            stay[x] += 1
        for x, y in seen_m:
            moved[x][y] += 1
    rows = {}
    for x in set(moved) | set(stay):
        m = moved.get(x, collections.Counter())
        top = m.most_common(3)
        rows[hex(x)] = dict(stay=stay.get(x, 0), moved=sum(m.values()),
                            to=[[hex(y), c, examples.get((x, y))] for y, c in top])
    json.dump(rows, open(a.out, 'w'), indent=0)
    print('wrote', a.out, len(rows), 'displacements')


LIT = re.compile(r'0x[0-9A-Fa-f]{2,8}\b')


def port(a):
    rows = json.load(open(a.census, encoding='utf-8'))
    hits = []
    for root in ('src', 'include'):
        for d, _, names in os.walk(os.path.join(REPO, root)):
            for nm in names:
                if not nm.endswith(('.cpp', '.hpp', '.h', '.inc', '.asm')):
                    continue
                p = os.path.join(d, nm)
                for ln, line in enumerate(open(p, encoding='utf-8', errors='replace'), 1):
                    code = line.split('//', 1)[0]
                    for m in LIT.finditer(code):
                        v = int(m.group(0), 16)
                        r = rows.get(hex(v))
                        if not r or r['moved'] < a.min_votes or r['moved'] <= r['stay']:
                            continue
                        hits.append((os.path.relpath(p, REPO), ln, m.group(0), r['stay'], r['moved'], r['to'][:2],
                                     code.strip()[:120]))
    for h in hits:
        print(f'{h[0]}:{h[1]} {h[2]} stay {h[3]} moved {h[4]} -> {h[5]}  | {h[6]}')
    print(len(hits), 'literals whose value moved in most functions that use it')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['census', 'port'])
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'disp_drift-2.5.json'))
    ap.add_argument('--census', default=os.path.join(REPO, 'build', 'patchday', 'disp_drift-2.5.json'))
    ap.add_argument('--min-disp', type=lambda s: int(s, 0), default=0x20)
    ap.add_argument('--min-votes', type=int, default=2)
    a = ap.parse_args()
    census(a) if a.cmd == 'census' else port(a)


if __name__ == '__main__':
    main()
