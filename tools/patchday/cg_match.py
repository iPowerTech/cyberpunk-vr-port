"""Find a function of the old build in the new one by its neighbours in the call graph.

    py -3.13 -B tools/patchday/cg_match.py --old <old game dir> --new <new game dir> 0x50eb1c [0x... ...]
    py -3.13 -B tools/patchday/cg_match.py ... --hash 3602585178 [--hash ...]      (old address-library hash)

A function whose symbol changed (a new signature, a rename) loses its address-library hash, but its callers and
callees mostly keep theirs. The call graph of each build is read straight from .text (E8 calls and E9 jumps
whose target is a .pdata function start); the old function's callers and callees are carried to the new build
through the library (a hash naming one entry on both builds), and the new functions that sit between them are
ranked by how many of those carried neighbours they connect to, then by masked-code similarity of size.
"""
import argparse
import os
import sys
from collections import Counter, defaultdict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pe import AddressLibrary, Image, find_game_files  # noqa: E402


class Graph:
    def __init__(self, game):
        exe, lib = find_game_files(game)
        self.img = Image(exe)
        self.lib = AddressLibrary(lib, self.img)
        begins, ends, _ = self.img.pdata
        order = np.argsort(begins)
        self.begins, self.ends = begins[order].astype(np.int64), ends[order].astype(np.int64)
        self.starts = set(int(b) for b in self.begins)
        t = self.img.text
        code = np.frombuffer(self.img.text_bytes(), np.uint8) if hasattr(self.img, 'text_bytes') else None
        if code is None or len(code) == 0:
            code = np.frombuffer(self.img.read(t.va, t.vsize), np.uint8)
        n = len(code) - 5
        op = code[:n]
        idx = np.nonzero((op == 0xE8) | (op == 0xE9))[0]
        rel = (code[idx + 1].astype(np.int64) | (code[idx + 2].astype(np.int64) << 8) |
               (code[idx + 3].astype(np.int64) << 16) | (code[idx + 4].astype(np.int64) << 24))
        rel = np.where(rel >= 1 << 31, rel - (1 << 32), rel)
        src = idx.astype(np.int64) + t.va
        dst = src + 5 + rel
        starts = np.array(sorted(self.starts), np.int64)
        pos = np.searchsorted(starts, dst)
        ok = (pos < len(starts)) & (starts[np.minimum(pos, len(starts) - 1)] == dst)
        src, dst = src[ok], dst[ok]
        # the function holding each call site
        fpos = np.searchsorted(self.begins, src, side='right') - 1
        inside = (fpos >= 0) & (src < self.ends[np.maximum(fpos, 0)])
        caller = self.begins[np.maximum(fpos, 0)]
        src, dst, caller = src[inside], dst[inside], caller[inside]
        self.callees = defaultdict(set)
        self.callers = defaultdict(set)
        for c, d in zip(caller.tolist(), dst.tolist()):
            if c != d:
                self.callees[c].add(d)
                self.callers[d].add(c)

    def size(self, rva):
        i = int(np.searchsorted(self.begins, rva))
        if i < len(self.begins) and self.begins[i] == rva:
            return int(self.ends[i] - self.begins[i])
        return None

    def hashes_at(self, rva):
        return self.lib.exact(rva)


def carry(old, new, rva):
    """New rva of an old function through a hash unique on both builds."""
    for h in old.hashes_at(rva):
        if len(old.lib.by_hash.get(h, ())) == 1:
            r = new.lib.unique(h)
            if r is not None:
                return r
    return None


def match(old, new, rva, top=5):
    callers = old.callers.get(rva, set())
    callees = old.callees.get(rva, set())
    cp = [carry(old, new, c) for c in callers]
    ce = [carry(old, new, c) for c in callees]
    cp = [c for c in cp if c is not None]
    ce = [c for c in ce if c is not None]
    score = Counter()
    for p in cp:
        for d in new.callees.get(p, ()):
            score[d] += 1
    for c in ce:
        for s in new.callers.get(c, ()):
            score[s] += 1
    osize = old.size(rva) or 0
    rows = []
    for cand, s in score.most_common(40):
        nsize = new.size(cand) or 0
        known = [h for h in new.hashes_at(cand) if h in old.lib.by_hash]
        rows.append((s, -abs(nsize - osize), cand, nsize, bool(known)))
    rows.sort(reverse=True)
    return dict(callers=len(callers), callees=len(callees), carried=(len(cp), len(ce)), size=osize,
                candidates=[(s, cand, nsize, known) for s, _, cand, nsize, known in rows[:top]])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', required=True)
    ap.add_argument('--new', required=True)
    ap.add_argument('--hash', action='append', type=int, default=[])
    ap.add_argument('rvas', nargs='*')
    a = ap.parse_args()
    old, new = Graph(a.old), Graph(a.new)
    todo = [(int(r, 16), r) for r in a.rvas]
    for h in a.hash:
        for r in old.lib.by_hash.get(h, []):
            todo.append((r, f'hash {h}'))
    for rva, label in todo:
        m = match(old, new, rva)
        print(f'{label} old {rva:#x} size {m["size"]}: callers {m["callers"]} callees {m["callees"]} '
              f'carried {m["carried"]}')
        for s, cand, nsize, known in m['candidates']:
            hs = new.hashes_at(cand)
            print(f'    {cand:#x} score {s} size {nsize} hashes {hs[:3]}{" (old hash too: not renamed)" if known else ""}')


if __name__ == '__main__':
    main()
