"""Where did an address inside a changed function go: align the old and the new function instruction by instruction.

    py -3.13 -B tools/patchday/site_find.py 0x127970 [0x...] [--new-func 0x5fb8f0] [--show 6]
    py -3.13 -B tools/patchday/site_find.py --needs-human        # every changed/ambiguous code address of the remap

remap.py carries a site when the function's masked code is unchanged or its byte window occurs once in the new
function. A patch that edits the function (an inserted call, a re-allocated register) breaks both, though the
instruction the port patches or returns to is usually still there. This decodes both functions, turns every
instruction into a token that survives relinking (mnemonic, registers, immediates, struct displacements; a RIP
operand becomes the carried address-library hash of its target, or "rip"; a call/jump to a function becomes the
carried hash of the callee, or "call"), aligns the two token lists (difflib), and reports where the old site's
instruction landed, how much of the surrounding code (+-8 instructions) matches, and both disassemblies.

The new function comes from --new-func, else the anchor's hash, else the remap map.json entry of the old function
start. A site in an unchanged-alignment block is "aligned"; in a replaced block the best local match is offered
as "replaced", to be read.
"""
import argparse
import difflib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import sigs  # noqa: E402
from gone_find import Build  # noqa: E402

RIP = re.compile(r'\[rip [+-] 0x[0-9a-f]+\]')
HEX = re.compile(r'0x[0-9a-f]+')


class Side:
    def __init__(self, build):
        self.b = build
        self.img = build.img
        self.lib = build.lib

    def func(self, rva):
        f = self.img.function_at(rva)
        return f

    def name_of(self, rva):
        for h in self.lib.exact(rva):
            if self.lib.unique(h) is not None:
                return h
        return None


def tokens(side, insns, other=None):
    out = []
    for i in insns:
        op = i.op_str
        if i.rip_target is not None:
            h = side.name_of(i.rip_target)
            op = RIP.sub(f'[H{h}]' if h is not None else '[rip]', op)
        if i.rel_target is not None:
            h = side.name_of(i.rel_target)
            if i.mnemonic == 'call' or (h is not None):
                op = f'H{h}' if h is not None else 'fn'
            else:
                op = 'loc'
        out.append(f'{i.mnemonic} {op}')
    return out


def locate(old, new, insns_o, insns_n, site):
    to, tn = tokens(old, insns_o), tokens(new, insns_n)
    idx = next((k for k, i in enumerate(insns_o) if i.rva <= site < i.rva + i.size), None)
    if idx is None:
        return None
    sm = difflib.SequenceMatcher(None, to, tn, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if i1 <= idx < i2:
            if tag == 'equal':
                j = j1 + (idx - i1)
                kind = 'aligned'
            elif tag == 'replace':
                # the best local match inside the replaced block, by the token itself and its neighbours
                best, j = -1, None
                for jj in range(j1, j2):
                    s = (to[idx] == tn[jj]) * 4 + sum(
                        1 for d in range(-3, 4) if d and 0 <= idx + d < len(to) and 0 <= jj + d < len(tn)
                        and to[idx + d] == tn[jj + d])
                    if s > best:
                        best, j = s, jj
                kind = 'replaced'
            else:
                return dict(kind='deleted', idx=idx)
            ctx = sum(1 for d in range(-8, 9) if 0 <= idx + d < len(to) and 0 <= j + d < len(tn)
                      and to[idx + d] == tn[j + d])
            off = site - insns_o[idx].rva
            return dict(kind=kind, idx=idx, j=j, new=insns_n[j].rva + off, ctx=ctx, same_token=to[idx] == tn[j],
                        ratio=round(sm.ratio(), 3), off_in_insn=off)
    return None


def disasm(insns, k, n):
    lo, hi = max(0, k - n), min(len(insns), k + n + 1)
    return [f"{'>' if j == k else ' '} {insns[j].rva:#x} {insns[j].mnemonic} {insns[j].op_str}" for j in range(lo, hi)]


def new_func_for(old, new, e, mp, site):
    a = (e or {}).get('anchor') or {}
    f_old = (e or {}).get('func')
    if a.get('kind') in ('func', 'exact', 'func-end') and f_old:
        nb = new.lib.by_hash.get(a['hash'], [])
        starts = []
        for b in nb:
            start = b - (a['rva'] - f_old[0]) if a['kind'] == 'exact' else b
            nf = new.img.function_at(start)
            if nf and nf[0] == start:
                starts.append(nf)
        if len(starts) == 1:
            return starts[0], 'anchor'
        if starts:
            return starts, 'anchor-multi'
    of = old.img.function_at(site)
    if of:
        r = mp.get(hex(of[0]))
        if r and r.get('new'):
            nf = new.img.function_at(int(r['new'], 16))
            if nf:
                return nf, 'map'
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rvas', nargs='*')
    ap.add_argument('--new-func')
    ap.add_argument('--show', type=int, default=4)
    ap.add_argument('--needs-human', action='store_true')
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--db', default=os.path.join(HERE, 'data', 'rva_db-68af45ea.json'))
    ap.add_argument('--map', default=os.path.join(REPO, 'build', 'patchday', 'check-6ad55daf', 'remap', 'map.json'))
    ap.add_argument('--json')
    a = ap.parse_args()
    old, new = Side(Build(a.old)), Side(Build(a.new))
    db = json.load(open(a.db, encoding='utf-8'))
    E = {e['rva']: e for e in db['entries']}
    mp = json.load(open(a.map, encoding='utf-8'))
    rvas = [int(x, 16) for x in a.rvas]
    if a.needs_human:
        uses = {}
        for o in db['occurrences']:
            if o['kind'] not in ('comment', 'foreign-module'):
                uses.setdefault(o['value'], []).append(o)
        rvas += [int(k, 16) for k, v in mp.items() if v['status'] in ('changed', 'ambiguous') and int(k, 16) in uses]
    results = []
    for site in sorted(set(rvas)):
        e = E.get(site)
        of = old.img.function_at(site)
        if a.new_func:
            nf, how = new.img.function_at(int(a.new_func, 16)), 'given'
        else:
            nf, how = new_func_for(old, new, e, mp, site)
        row = dict(old=hex(site), old_func=hex(of[0]) if of else None, how=how)
        if not of or not nf:
            row['result'] = 'no function'
            results.append(row)
            print(f'{site:#x}: no old/new function ({how})')
            continue
        cands = nf if isinstance(nf, list) else [nf]
        best = None
        oi = sigs.decode(old.img, of[0], of[1])
        for c in cands:
            ni = sigs.decode(new.img, c[0], c[1])
            r = locate(old, new, oi, ni, site)
            if r and (best is None or (r.get('ctx', -1), r.get('ratio', 0)) > (best[0].get('ctx', -1), best[0].get('ratio', 0))):
                best = (r, c, ni)
        if not best:
            print(f'{site:#x}: site not located')
            row['result'] = 'not located'
            results.append(row)
            continue
        r, c, ni = best
        row.update(new_func=hex(c[0]), **{k: (hex(v) if k == 'new' else v) for k, v in r.items() if k not in ('idx', 'j')})
        results.append(row)
        print(f"{site:#x} (func {of[0]:#x} +{site - of[0]:#x}) -> {r.get('new', 0):#x} in {c[0]:#x} "
              f"[{r['kind']}, context {r.get('ctx')}/17, same insn {r.get('same_token')}, func ratio {r.get('ratio')}, "
              f"via {how}{', ' + str(len(cands)) + ' candidates' if len(cands) > 1 else ''}]")
        if a.show and 'j' in r:
            for lo, ln in zip(disasm(oi, r['idx'], a.show), disasm(ni, r['j'], a.show)):
                print(f'    {lo[:60]:60} | {ln[:60]}')
    if a.json:
        json.dump(results, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
