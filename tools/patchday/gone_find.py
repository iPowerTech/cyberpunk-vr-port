"""Where did a function the address library no longer names go: rank new-build functions by what they share with it.

    py -3.13 -B tools/patchday/gone_find.py 0x7bba48 [0x...] [--top 8] [--old ..] [--new ..]

A patch that rewrites a function drops its address-library hash, but the function usually still calls the same
helpers, uses the same strings and is called from the same places. For each old function this collects

  - its direct callees whose hash is unique in both builds (carried to the new build),
  - the C strings it references (rip-relative),
  - its direct callers, carried the same way,

and scores every new-build function: IDF-weighted per shared carried callee (1 / (1 + log2 callers)), +2 per shared
string, 3 / sqrt(callees) when a carried caller calls it. The call/reference tables of each build are built once from the raw .text (E8 rel32 and rip-relative
operands) and cached next to the build as callrefs.npz.
"""
import argparse
import collections
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import AddressLibrary, Image, find_game_files  # noqa: E402


class Build:
    def __init__(self, game):
        exe, lib = find_game_files(game)
        self.img = Image(exe)
        self.lib = AddressLibrary(lib, self.img)
        self.begins, self.ends, _ = self.img.pdata
        cache = os.path.join(game, 'callrefs.npz')
        if os.path.exists(cache) and os.path.getmtime(cache) >= os.path.getmtime(exe):
            z = np.load(cache)
            self.call_site, self.call_func, self.call_target = z['call_site'], z['call_func'], z['call_target']
            self.ref_site, self.ref_func, self.ref_target = z['ref_site'], z['ref_func'], z['ref_target']
        else:
            self._scan()
            np.savez(cache, call_site=self.call_site, call_func=self.call_func, call_target=self.call_target,
                     ref_site=self.ref_site, ref_func=self.ref_func, ref_target=self.ref_target)
        self._strings = {}

    def func_index(self, rvas):
        i = np.searchsorted(self.begins, rvas, side='right') - 1
        ok = (i >= 0) & (rvas < self.ends[np.clip(i, 0, None)])
        return np.where(ok, i, -1)

    def _scan(self):
        text = next(s for s in self.img.sections if s.name == '.text')
        code = np.frombuffer(self.img.data[text.raw:text.raw + text.rsize], dtype=np.uint8)
        n = len(code)
        # E8 rel32 calls whose target is a function start
        pos = np.nonzero(code[:n - 5] == 0xE8)[0]
        rel = code[pos + 1].astype(np.int64) | (code[pos + 2].astype(np.int64) << 8) | \
            (code[pos + 3].astype(np.int64) << 16) | (code[pos + 4].astype(np.int64) << 24)
        rel = np.where(rel >= 1 << 31, rel - (1 << 32), rel)
        site = pos + text.va
        target = site + 5 + rel
        ti = np.searchsorted(self.begins, target)
        ok = (ti < len(self.begins)) & (self.begins[np.clip(ti, 0, len(self.begins) - 1)] == target)
        site, target = site[ok], target[ok]
        fi = self.func_index(site)
        keep = fi >= 0
        self.call_site, self.call_func, self.call_target = site[keep], fi[keep], target[keep]
        # rip-relative operands: [REX] (8D|8B) modrm with mod=00 rm=101, disp32 follows
        refs = []
        for opc in (0x8D, 0x8B):
            p = np.nonzero(code[1:n - 6] == opc)[0] + 1
            modrm = code[p + 1]
            p = p[(modrm & 0xC7) == 0x05]
            rex = code[p - 1]
            p = p[(rex & 0xF0) == 0x40]
            d = code[p + 2].astype(np.int64) | (code[p + 3].astype(np.int64) << 8) | \
                (code[p + 4].astype(np.int64) << 16) | (code[p + 5].astype(np.int64) << 24)
            d = np.where(d >= 1 << 31, d - (1 << 32), d)
            s = p - 1 + text.va
            refs.append((s, s + 7 + d))
        rs = np.concatenate([r[0] for r in refs])
        rt = np.concatenate([r[1] for r in refs])
        fi = self.func_index(rs)
        keep = fi >= 0
        self.ref_site, self.ref_func, self.ref_target = rs[keep], fi[keep], rt[keep]

    def string(self, rva):
        if rva not in self._strings:
            s = None
            sec = self.img.section_of(int(rva))
            if sec is not None and sec.name == '.rdata':
                raw = self.img.read(int(rva), 160)
                if raw:
                    end = raw.find(b'\0')
                    if end >= 4 and all(32 <= c < 127 for c in raw[:end]):
                        s = raw[:end].decode('ascii')
            self._strings[rva] = s
        return self._strings[rva]

    def index_of(self, rva):
        i = int(self.func_index(np.array([rva]))[0])
        return i

    def callees(self, fi):
        return set(int(t) for t in self.call_target[self.call_func == fi])

    def callers(self, rva):
        return set(int(self.begins[f]) for f in self.call_func[self.call_target == rva])

    def strings_of(self, fi):
        out = set()
        for t in self.ref_target[self.ref_func == fi]:
            s = self.string(int(t))
            if s:
                out.add(s)
        return out

    def unique_hash(self, rva):
        for h in self.lib.exact(rva):
            if self.lib.unique(h) is not None:
                return h
        return None


def carry(old, new, rva):
    h = old.unique_hash(rva)
    return new.lib.unique(h) if h is not None else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rvas', nargs='+')
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--top', type=int, default=8)
    a = ap.parse_args()
    old, new = Build(a.old), Build(a.new)
    # new-build string -> functions referencing it
    for arg in a.rvas:
        rva = int(arg, 16)
        fi = old.index_of(rva)
        if fi < 0:
            print(f'{rva:#x}: not a function in the old build')
            continue
        start = int(old.begins[fi])
        callees = old.callees(fi)
        carried_callees = {c: carry(old, new, c) for c in callees}
        strings = old.strings_of(fi)
        callers = old.callers(start)
        carried_callers = {c: carry(old, new, c) for c in callers}
        score = collections.Counter()
        why = collections.defaultdict(list)
        for c, nc in carried_callees.items():
            if nc is None:
                continue
            users = set(int(f) for f in new.call_func[new.call_target == nc])
            # a helper called from everywhere says little: weight by how rare its callers are
            w = 1.0 / (1.0 + np.log2(max(len(users), 1)))
            for f in users:
                score[f] += w
                why[f].append(f'calls {nc:#x}')
        if strings:
            rd = next(x for x in new.img.sections if x.name == '.rdata')
            blob = bytes(new.img.data[rd.raw:rd.raw + rd.rsize])
            for s in strings:
                needle = s.encode() + b'\0'
                p = blob.find(needle)
                while p >= 0:
                    srva = rd.va + p
                    for f in set(int(x) for x in new.ref_func[new.ref_target == srva]):
                        score[f] += 2
                        why[f].append(f'"{s[:40]}"')
                    p = blob.find(needle, p + 1)
        for c, nc in carried_callers.items():
            if nc is None:
                continue
            nfi = new.index_of(nc)
            for t in new.callees(nfi):
                tf = new.index_of(t)
                if tf >= 0:
                    score[tf] += 3 / max(1, len(new.callees(nfi))) ** 0.5
                    why[tf].append(f'called by {nc:#x}')
        n_cc = sum(1 for v in carried_callees.values() if v is not None)
        n_cr = sum(1 for v in carried_callers.values() if v is not None)
        print(f'== old {start:#x} ({old.ends[fi] - start} B): {len(callees)} callees ({n_cc} carried), '
              f'{len(strings)} strings, {len(callers)} callers ({n_cr} carried)')
        for f, sc in score.most_common(a.top):
            b, e = int(new.begins[f]), int(new.ends[f])
            hs = new.lib.exact(b)
            print(f'   {b:#x} ({e - b} B) score {sc:.2f} hash {hs[0] if hs else None}: '
                  f'{"; ".join(sorted(set(why[f])))[:220]}')


if __name__ == '__main__':
    main()
