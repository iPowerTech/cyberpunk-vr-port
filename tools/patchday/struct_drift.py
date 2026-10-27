"""Which struct offsets of the port moved, struct by struct.

    py -3.13 -B tools/patchday/struct_drift.py [--out build/patchday/struct_drift-2.5.json] [--txt ...]

disp_drift's census counts one displacement over every struct that has a field there, so 0x1E10 reads "moved to
0x2C40" whatever struct the port means. Here the port's offsets are grouped by what they are added to (`view+0xF94`,
`ctx + 0x1E10`, the entries of one `kSomethingOffs[]` table, a named constant), and an engine register is taken to
hold the port's struct where it is used with several offsets of one group: for each function both builds name by a
unique address-library hash, the instructions are aligned (mnemonic and operand shape, registers and displacements
left out), each old register is followed from one write to the next, and every aligned memory operand records old
displacement -> new displacement under that register. A group's offsets then vote with only the registers that
touch at least two of them, so a moved field of the render view context and an unmoved field of the same value in
another struct no longer mix. Groups with one offset only get the plain census vote and are marked weak.

The table names every port line with a literal whose struct moved, the votes, and the registers' functions as
evidence; the edits themselves are made per line after review (tools/patchday/line_swap.py).
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

SUB = {}
for full, subs in {
        'rax': 'eax ax al ah', 'rbx': 'ebx bx bl bh', 'rcx': 'ecx cx cl ch', 'rdx': 'edx dx dl dh',
        'rsi': 'esi si sil', 'rdi': 'edi di dil', 'rbp': 'ebp bp bpl', 'rsp': 'esp sp spl'}.items():
    for s in subs.split():
        SUB[s] = full
for k in range(8, 16):
    for s in (f'r{k}d', f'r{k}w', f'r{k}b'):
        SUB[s] = f'r{k}'
VOLATILE = ('rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11')
NO_WRITE = {'cmp', 'test', 'push', 'bt', 'comiss', 'ucomiss', 'comisd', 'ucomisd', 'jmp', 'call', 'ret', 'nop'}
REGTOK = re.compile(r'\b(r[a-z0-9]+|e[a-z]{2}|[a-z]{2}l?|xmm\d+|ymm\d+)\b')
DISP = re.compile(r'([+-]) 0x[0-9a-f]+\]')


def full(name):
    return SUB.get(name, name)


# ---- the port side -------------------------------------------------------------------------------------------
HEX = r'0[xX][0-9A-Fa-f]{2,7}'
VAR_OFF = re.compile(r'([A-Za-z_][\w.>-]*?)\s*\)?\s*\+\s*(' + HEX + r'|[A-Z][A-Z0-9_]*_OFF\w*|k[A-Z]\w*Off\w*)\b')
CALL_OFF = re.compile(r'\b(?:At|Field|Read\w*|Write\w*|Copy\w*)\s*(?:<[^<>]*>)?\s*\(\s*([A-Za-z_]\w*)\s*,\s*(' + HEX + r')\b')
CONST_DEF = re.compile(r'constexpr\s+[\w:]+\s+([A-Za-z_]\w*)\s*=\s*(' + HEX + r')\b')
ARRAY_DEF = re.compile(r'\b([A-Za-z_]\w*)\s*\[\s*\w*\s*\]\s*=\s*\{')
SKIP_VARS = {'base', 's_exe', 'g_exe_base', 'exe', 'module', 'image', 'kBase', 'g_base', 'gameBase', 'imageBase'}


def port_groups():
    """{(file, base): [(line, literal value, literal text, def site)]} over src/ and include/."""
    files = []
    for root in ('src', 'include'):
        for d, _, names in os.walk(os.path.join(REPO, root)):
            files += [os.path.join(d, n) for n in names if n.endswith(('.cpp', '.hpp', '.h', '.inc'))]
    consts = {}
    texts = {}
    for p in files:
        lines = open(p, encoding='utf-8', errors='replace').read().split('\n')
        texts[p] = lines
        for ln, line in enumerate(lines, 1):
            code = line.split('//', 1)[0]
            for m in CONST_DEF.finditer(code):
                consts.setdefault(m.group(1), (int(m.group(2), 16), m.group(2), os.path.relpath(p, REPO), ln))
    groups = collections.defaultdict(list)
    for p, lines in texts.items():
        rel = os.path.relpath(p, REPO)
        array, depth = None, 0
        for ln, line in enumerate(lines, 1):
            code = line.split('//', 1)[0]
            if array is None:
                m = ARRAY_DEF.search(code)
                if m and ('Off' in m.group(1) or 'off' in m.group(1) or 'Fields' in m.group(1)
                          or 'Ranges' in m.group(1) or 'Copy' in m.group(1) or 'Mirror' in m.group(1)):
                    array, depth = m.group(1), 0
                    code = code[m.end() - 1:]
            if array is not None:
                for m in re.finditer(HEX, code):
                    v = int(m.group(0), 16)
                    if 0x10 <= v < 0x4000000:
                        groups[(rel, 'array:' + array)].append((ln, v, m.group(0), None))
                depth += code.count('{') - code.count('}')
                if depth <= 0 and '}' in code:
                    array = None
                continue
            for m in list(VAR_OFF.finditer(code)) + list(CALL_OFF.finditer(code)):
                var, lit = m.group(1), m.group(2)
                var = re.split(r'->|\.', var)[-1] if var not in SKIP_VARS else var
                if var in SKIP_VARS or var.isupper() and var.endswith('RVA'):
                    continue
                if lit[:2].lower() == '0x':
                    v, site = int(lit, 16), None
                elif lit in consts:
                    v, lit, f, l = consts[lit]
                    site = (f, l)
                else:
                    continue
                if 0x10 <= v < 0x4000000:
                    groups[(rel, var)].append((ln, v, lit, site))
    return groups


# ---- the engine side -----------------------------------------------------------------------------------------
def frags(b, start):
    f = b.img.function_at(start)
    return [(f[0], f[1])]


def decode(b, f, limit=6000):
    code = bytes(b.img.read(f[0], f[1] - f[0]))
    out = []
    for i in md.disasm(code, f[0]):
        if len(out) >= limit:
            break
        mems = [(full(md.reg_name(op.mem.base)), op.mem.disp) for op in i.operands
                if op.type == x86.X86_OP_MEM and op.mem.base not in (0, x86.X86_REG_RIP)]
        shape = DISP.sub(r'\1 D]', i.op_str) if mems else i.op_str
        if i.mnemonic == 'call' or i.mnemonic.startswith('j'):
            shape = 'T'
        elif 'rip' in shape:
            shape = re.sub(r'\[rip [+-] 0x[0-9a-f]+\]', '[R]', shape)
        written = ()
        if i.mnemonic == 'call':
            written = VOLATILE
        elif i.mnemonic not in NO_WRITE and i.operands and i.operands[0].type == x86.X86_OP_REG:
            written = (full(md.reg_name(i.operands[0].reg)),)
        out.append((i.mnemonic + ' ' + REGTOK.sub('r', shape), mems, written))
    return out


def engine(old, new, wanted, max_bytes=40000):
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
    records = []     # (old func, new func, reg, {old disp: Counter(new disp)})
    for k, (of, nf) in enumerate(pairs):
        if k % 10000 == 0:
            print(f'  {k}/{len(pairs)} ({len(records)} records)', flush=True)
        if of[1] - of[0] > max_bytes or nf[1] - nf[0] > max_bytes:
            continue
        try:
            io, inn = decode(old, of), decode(new, nf)
        except Exception:
            continue
        if not any(d in wanted for _, mems, _ in io for _, d in mems):
            continue
        to, tn = [t for t, _, _ in io], [t for t, _, _ in inn]
        if to == tn:
            blocks = [(0, 0, len(to))]
        else:
            if len(to) * len(tn) > 4_000_000:
                continue
            blocks = difflib.SequenceMatcher(None, to, tn, autojunk=False).get_matching_blocks()
        aligned = {}
        for i0, j0, size in blocks:
            for d in range(size):
                aligned[i0 + d] = j0 + d
        ver = collections.Counter()
        recs = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
        for i, (_, mems, written) in enumerate(io):
            j = aligned.get(i)
            if j is not None:
                for (reg, x), (_, y) in zip(mems, inn[j][1]):
                    if x in wanted and reg not in ('rsp', 'rbp'):
                        recs[(reg, ver[reg])][x][y] += 1
            for r in written:
                ver[r] += 1
        for (reg, _), m in recs.items():
            records.append((of[0], nf[0], reg, {x: c.most_common(1)[0][0] for x, c in m.items()}))
    return records


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'struct_drift-2.5.json'))
    ap.add_argument('--txt', default=os.path.join(REPO, 'build', 'patchday', 'struct_drift-2.5.txt'))
    a = ap.parse_args()
    groups = port_groups()
    wanted = {v for rows in groups.values() for _, v, _, _ in rows}
    print(f'{len(groups)} port groups, {len(wanted)} distinct offsets', flush=True)
    records = engine(Build(a.old), Build(a.new), wanted)
    print(f'{len(records)} register records', flush=True)
    by_off = collections.defaultdict(list)
    for idx, (_, _, _, m) in enumerate(records):
        for x in m:
            by_off[x].append(idx)
    result, lines = [], []
    for (rel, var), rows in sorted(groups.items()):
        offs = sorted({v for _, v, _, _ in rows})
        cand = collections.Counter()
        for o in offs:
            for idx in by_off.get(o, ()):
                cand[idx] += 1
        need = 2 if len(offs) >= 2 else 1
        strong = {idx: s for idx, s in cand.items() if s >= need}
        per = {}
        for o in offs:
            votes, ev = collections.Counter(), collections.defaultdict(list)
            for idx, s in strong.items():
                m = records[idx][3]
                if o in m:
                    votes[m[o]] += s
                    if len(ev[m[o]]) < 3:
                        ev[m[o]].append(f'{records[idx][0]:#x}->{records[idx][1]:#x} {records[idx][2]}')
            total = sum(votes.values())
            top = votes.most_common(2)
            per[o] = dict(total=total, top=[[hex(y), c] for y, c in top], evidence={hex(y): ev[y] for y, _ in top})
        moved = [o for o in offs if per[o]['top'] and int(per[o]['top'][0][0], 16) != o]
        result.append(dict(file=rel, base=var, weak=len(offs) < 2,
                           rows=[[ln, hex(v), lit, site] for ln, v, lit, site in rows], offsets=per))
        if not moved:
            continue
        lines.append(f'== {rel} [{var}] {len(offs)} offsets{" (weak: one offset)" if len(offs) < 2 else ""}')
        for o in offs:
            p = per[o]
            if not p['top']:
                lines.append(f'   {o:#x}: no evidence')
                continue
            y, c = p['top'][0]
            mark = 'same ' if int(y, 16) == o else 'MOVED'
            alt = f'  (2nd {p["top"][1][0]} x{p["top"][1][1]})' if len(p['top']) > 1 else ''
            at = sorted({ln for ln, v, _, _ in rows if v == o})
            lines.append(f'   {mark} {o:#x} -> {y} x{c}/{p["total"]}{alt}  lines {at[:8]}  e.g. {p["evidence"][y][:2]}')
    json.dump(result, open(a.out, 'w', encoding='utf-8'), indent=0)
    open(a.txt, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('wrote', a.out, a.txt, sum(1 for l in lines if l.startswith('==')), 'groups with a moved offset')


if __name__ == '__main__':
    main()
