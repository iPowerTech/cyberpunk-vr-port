"""Diaphora's diff, made to fit the whole game exe: its heuristics over its own export databases, as unique-key
joins instead of SQL cross joins.

    py -3.13 -B tools/patchday/diaphora_lite.py --old <old.sqlite> --new <new.sqlite> --out <diff.diaphora>

Diaphora's diff joins main.functions with diff.functions on one feature per heuristic; on 280k x 317k functions
a feature many small functions share (microcode small-primes product, mnemonics) becomes a temp table of
hundreds of GB ("Microcode mnemonics small primes product" filled 200 GB of D: in 2.31 -> 2.5). Here every
heuristic is a hash map from the feature to the functions still unmatched, and a pair is taken only when the
feature names exactly one function on each side, which is the answer the SQL join gives for those rows after
Diaphora drops its multimatches. The order and filters follow diaphora_heuristics.py; the address library
(CDPR's hash per symbol, the same in both builds) is the first heuristic, then the call graph and the address
order of the matched pairs carry the rest (Diaphora's callgraph pass), and every pair gets Diaphora's kind of
ratio: difflib over the cleaned assembly and the cleaned pseudo-code, the higher one. The output is a Diaphora
results database (config, results, unmatched), so diaphora_report.py and Diaphora's own GUI read it.
"""
import argparse
import bisect
import difflib
import hashlib
import os
import sqlite3
import sys
import time
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import AddressLibrary, Image, find_game_files  # noqa: E402

BASE = 0x140000000
COLS = ('id address name nodes edges instructions size pseudocode_lines cyclomatic_complexity indegree outdegree '
        'loops strongly_connected md_index kgh_hash bytes_hash function_hash microcode_spp mnemonics_spp '
        'clean_assembly clean_microcode clean_pseudo pseudocode_hash1 pseudocode_hash2 pseudocode_hash3 '
        'pseudocode_primes constants constants_count names switches mnemonics prototype2').split()
# Long text columns arrive as a 16-byte digest; only equality matters for matching.
HASHED = {'microcode_spp', 'mnemonics_spp', 'clean_assembly', 'clean_microcode', 'clean_pseudo',
          'pseudocode_primes', 'constants', 'names', 'switches', 'mnemonics', 'prototype2'}


def digest(s):
    if s is None:
        return None
    return hashlib.blake2b(s.encode('utf-8', 'surrogatepass') if isinstance(s, str) else bytes(s),
                           digest_size=16).digest()


class Func:
    __slots__ = COLS + ['rva', 'plen', 'empty_names', 'empty_switches']


def load(path):
    c = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
    c.create_function('h', 1, digest, deterministic=True)
    sel = ', '.join(f'h({k})' if k in HASHED else k for k in COLS)
    out = []
    empty_list = digest('[]')
    for row in c.execute(f'select {sel}, length(pseudocode_primes) from functions'):
        f = Func()
        for k, v in zip(COLS, row):
            setattr(f, k, v)
        f.address = int(f.address)
        f.rva = f.address - BASE
        f.plen = row[-1] or 0
        f.empty_names = f.names in (None, empty_list)
        f.empty_switches = f.switches in (None, empty_list)
        for k in ('nodes', 'edges', 'instructions', 'size', 'pseudocode_lines', 'cyclomatic_complexity',
                  'indegree', 'outdegree', 'loops', 'strongly_connected', 'constants_count'):
            setattr(f, k, int(getattr(f, k) or 0))
        out.append(f)
    return out


def not_nullsub(f):
    return not (f.name or '').startswith('nullsub')


# (name, key, filter applied to each side) in diaphora_heuristics.py order; Diaphora's own filters.
HEURISTICS = [
    ('Bytes hash', lambda f: f.bytes_hash, lambda f: f.instructions > 5),
    ('Function Hash', lambda f: f.function_hash,
     lambda f: (f.nodes > 1 and f.instructions > 5) or f.instructions > 10),
    ('Same cleaned assembly', lambda f: f.clean_assembly, lambda f: f.nodes >= 3 and not_nullsub(f)),
    ('Same cleaned microcode', lambda f: f.clean_microcode, lambda f: f.instructions > 3 and not_nullsub(f)),
    ('Same cleaned pseudo-code', lambda f: f.clean_pseudo, lambda f: f.pseudocode_lines > 5 and not_nullsub(f)),
    ('Microcode mnemonics small primes product', lambda f: f.microcode_spp,
     lambda f: f.microcode_spp != digest('1') and f.instructions > 5 and f.nodes > 2),
    ('Same KOKA hash and MD-Index', lambda f: (f.kgh_hash, f.md_index, f.nodes, f.outdegree, f.indegree),
     lambda f: f.nodes >= 4 and f.kgh_hash not in (None, '0', 0)),
    ('Same constants', lambda f: (f.constants, f.constants_count), lambda f: f.constants_count > 1),
    ('Same MD Index and constants', lambda f: (f.md_index, f.constants),
     lambda f: f.md_index not in (None, '0', 0) and f.nodes >= 3 and f.constants_count > 0),
    ('Import names hash', lambda f: (f.names, f.md_index, f.instructions), lambda f: not f.empty_names and f.nodes > 5),
    ('Mnemonics and names', lambda f: (f.mnemonics, f.instructions, f.names),
     lambda f: not f.empty_names and f.instructions > 5),
    ('Pseudo-code fuzzy hash', lambda f: (f.pseudocode_hash1, f.pseudocode_hash2, f.pseudocode_hash3),
     lambda f: f.pseudocode_hash1 is not None and f.pseudocode_lines > 5),
    ('Mnemonics small-primes-product', lambda f: (f.mnemonics_spp, f.instructions),
     lambda f: f.nodes > 1 and f.instructions > 5),
    ('Same nodes, edges, loops and strongly connected components',
     lambda f: (f.nodes, f.edges, f.strongly_connected, f.loops, f.mnemonics_spp), lambda f: f.nodes > 5),
    ('Same low complexity, prototype and names', lambda f: (f.names, f.cyclomatic_complexity, f.prototype2),
     lambda f: not f.empty_names and f.cyclomatic_complexity < 20),
    ('Switch structures', lambda f: f.switches, lambda f: not f.empty_switches and f.nodes > 5),
    ('Pseudo-code fuzzy (normal)', lambda f: f.pseudocode_hash1, lambda f: f.pseudocode_lines > 5),
    ('Pseudo-code fuzzy (mixed)', lambda f: f.pseudocode_hash3, lambda f: f.pseudocode_lines > 5),
    ('Pseudo-code fuzzy (reverse)', lambda f: f.pseudocode_hash2, lambda f: f.pseudocode_lines > 5),
    ('Pseudo-code fuzzy AST hash', lambda f: f.pseudocode_primes,
     lambda f: f.pseudocode_lines >= 3 and f.plen >= 35),
    ('Same rare KOKA hash', lambda f: f.kgh_hash, lambda f: f.kgh_hash not in (None, '0', 0) and f.nodes >= 3),
    ('Same rare MD Index', lambda f: f.md_index, lambda f: f.md_index not in (None, '0', 0) and f.nodes >= 3),
    ('Same cleaned assembly (small)', lambda f: f.clean_assembly, lambda f: f.instructions > 2 and not_nullsub(f)),
    ('Bytes hash (small)', lambda f: f.bytes_hash, lambda f: f.instructions > 2),
]


class Matcher:
    def __init__(self, old, new):
        self.old, self.new = old, new
        self.o_by_id = {f.id: f for f in old}
        self.n_by_id = {f.id: f for f in new}
        self.o_by_addr = {f.address: f for f in old}
        self.n_by_addr = {f.address: f for f in new}
        self.pairs = {}     # old id -> (new id, heuristic)
        self.taken = set()  # new ids

    def take(self, o, n, why):
        if o.id in self.pairs or n.id in self.taken:
            return False
        self.pairs[o.id] = (n.id, why)
        self.taken.add(n.id)
        return True

    def free_old(self):
        return [f for f in self.old if f.id not in self.pairs]

    def free_new(self):
        return [f for f in self.new if f.id not in self.taken]

    def unique_join(self, olds, news, key, flt, why):
        a, b = defaultdict(list), defaultdict(list)
        for f in olds:
            if flt(f):
                k = key(f)
                if k is not None:
                    a[k].append(f)
        for f in news:
            if flt(f):
                k = key(f)
                if k is not None and k in a:
                    b[k].append(f)
        n = 0
        for k, fs in b.items():
            os_ = a[k]
            if len(fs) == 1 and len(os_) == 1 and self.take(os_[0], fs[0], why):
                n += 1
        return n

    def address_library(self, old_lib, new_lib):
        n = 0
        new_starts = {f.rva: f for f in self.new}
        for f in self.old:
            for h in old_lib.exact(f.rva):
                if len(old_lib.by_hash.get(h, ())) != 1:
                    continue
                r = new_lib.unique(h)
                g = new_starts.get(r) if r is not None else None
                if g is not None and self.take(f, g, 'Address library hash'):
                    n += 1
                    break
        return n

    def callgraph(self, edges_old, edges_new):
        """Diaphora's callgraph pass: the unmatched callees and callers of a matched pair, matched among
        themselves by ever weaker keys while the key names one function on each side of that neighbourhood."""
        keys = [
            ('Callgraph: same mnemonics', lambda f: (f.mnemonics, f.instructions)),
            ('Callgraph: same graph', lambda f: (f.nodes, f.edges, f.cyclomatic_complexity)),
            ('Callgraph: same mnemonics spp', lambda f: f.mnemonics_spp),
            ('Callgraph: only candidate', lambda f: 0),
        ]
        total = 0
        changed = True
        while changed:
            changed = False
            for why, key in keys:
                n = 0
                for oid, (nid, _) in list(self.pairs.items()):
                    for kind in ('callee', 'caller'):
                        oc = [self.o_by_id[x] for x in edges_old[kind].get(oid, ()) if x not in self.pairs]
                        if not oc:
                            continue
                        nc = [self.n_by_id[x] for x in edges_new[kind].get(nid, ()) if x not in self.taken]
                        if not nc:
                            continue
                        if why.endswith('only candidate'):
                            if len(oc) == 1 and len(nc) == 1 and size_close(oc[0], nc[0]):
                                n += self.take(oc[0], nc[0], why)
                            continue
                        a, b = defaultdict(list), defaultdict(list)
                        for f in oc:
                            a[key(f)].append(f)
                        for f in nc:
                            b[key(f)].append(f)
                        for k, fs in b.items():
                            if len(fs) == 1 and len(a.get(k, ())) == 1:
                                n += self.take(a[k][0], fs[0], why)
                total += n
                print(f'  {why}: {n}', flush=True)
                if n:
                    changed = True
        return total

    def order(self):
        """Matched pairs in address order on both sides (the longest increasing chain of them) bound gaps; a gap
        with as many unmatched functions on each side, pairwise of a similar size, is matched in order."""
        ps = sorted((self.o_by_id[o].address, self.n_by_id[n].address) for o, (n, _) in self.pairs.items())
        chain = lis(ps)
        o_free = sorted(f.address for f in self.free_old())
        n_free = sorted(f.address for f in self.free_new())
        n = 0
        for (o0, n0), (o1, n1) in zip(chain, chain[1:]):
            a = o_free[bisect.bisect_right(o_free, o0):bisect.bisect_left(o_free, o1)]
            b = n_free[bisect.bisect_right(n_free, n0):bisect.bisect_left(n_free, n1)]
            if not a or len(a) != len(b) or len(a) > 64:
                continue
            fa, fb = [self.o_by_addr[x] for x in a], [self.n_by_addr[x] for x in b]
            if all(size_close(x, y) for x, y in zip(fa, fb)):
                for x, y in zip(fa, fb):
                    n += self.take(x, y, 'Address order between matches')
        return n, len(chain)


def size_close(a, b):
    sa, sb = max(a.size, 1), max(b.size, 1)
    return max(sa, sb) <= 2 * min(sa, sb) + 16


def lis(ps):
    """Longest chain of pairs increasing in both coordinates (ps sorted by the first)."""
    tails, tails_i, prev = [], [], [None] * len(ps)
    for i, (_, y) in enumerate(ps):
        k = bisect.bisect_left(tails, y)
        if k == len(tails):
            tails.append(y)
            tails_i.append(i)
        else:
            tails[k] = y
            tails_i[k] = i
        prev[i] = tails_i[k - 1] if k else None
    out, i = [], tails_i[-1] if tails_i else None
    while i is not None:
        out.append(ps[i])
        i = prev[i]
    return out[::-1]


def edges(path, by_addr):
    c = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
    out = {'callee': defaultdict(list), 'caller': defaultdict(list)}
    for fid, addr, typ in c.execute('select func_id, address, type from callgraph'):
        g = by_addr.get(int(addr))
        if g is not None and typ in out:
            out[typ][fid].append(g.id)
    return out


def texts(path, ids):
    c = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
    out = {}
    ids = sorted(ids)
    for k in range(0, len(ids), 900):
        chunk = ids[k:k + 900]
        q = f'select id, clean_assembly, clean_pseudo from functions where id in ({",".join("?" * len(chunk))})'
        for fid, asm, pseudo in c.execute(q, chunk):
            out[fid] = (asm or '', pseudo or '')
    return out


def ratio(a, b):
    if not a and not b:
        return 1.0
    if not a or not b:
        return 0.0
    la, lb = a.splitlines(), b.splitlines()
    if len(la) * len(lb) > 25_000_000:
        sm = difflib.SequenceMatcher(None, la, lb)
        return sm.quick_ratio()
    return difflib.SequenceMatcher(None, la, lb, autojunk=False).ratio()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', required=True)
    ap.add_argument('--new', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--old-game', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new-game', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    a = ap.parse_args()
    t0 = time.time()
    old, new = load(a.old), load(a.new)
    print(f'loaded {len(old)} / {len(new)} functions in {time.time() - t0:.0f} s', flush=True)
    m = Matcher(old, new)
    libs = []
    for g in (a.old_game, a.new_game):
        exe, path = find_game_files(g)
        libs.append(AddressLibrary(path, Image(exe)))
    print(f'Address library hash: {m.address_library(*libs)}', flush=True)
    for name, key, flt in HEURISTICS:
        n = m.unique_join(m.free_old(), m.free_new(), key, flt, name)
        print(f'{name}: {n}', flush=True)
    eo, en = edges(a.old, m.o_by_addr), edges(a.new, m.n_by_addr)
    print(f'callgraph: {m.callgraph(eo, en)}', flush=True)
    for name, key, flt in HEURISTICS:
        n = m.unique_join(m.free_old(), m.free_new(), key, flt, name + ' (after callgraph)')
        if n:
            print(f'{name} (after callgraph): {n}', flush=True)
    n, chain = m.order()
    print(f'address order: {n} (chain of {chain} monotonic pairs)', flush=True)
    print(f'callgraph again: {m.callgraph(eo, en)}', flush=True)

    # Ratios: identical bytes or identical cleaned code is 1.0; the rest difflib over the cleaned text.
    need = [(o, n) for o, (n, _) in m.pairs.items()
            if not (m.o_by_id[o].bytes_hash == m.n_by_id[n].bytes_hash
                    or (m.o_by_id[o].clean_assembly == m.n_by_id[n].clean_assembly
                        and m.o_by_id[o].clean_pseudo == m.n_by_id[n].clean_pseudo))]
    print(f'{len(m.pairs)} pairs, {len(need)} need a ratio', flush=True)
    to, tn = texts(a.old, [o for o, _ in need]), texts(a.new, [n for _, n in need])
    ratios = {}
    for k, (o, n) in enumerate(need):
        if k % 20000 == 0:
            print(f'  ratio {k}/{len(need)}', flush=True)
        (ao, po), (an, pn) = to.get(o, ('', '')), tn.get(n, ('', ''))
        r = ratio(ao, an)
        if po and pn and r < 1.0:
            r = max(r, ratio(po, pn))
        ratios[o] = min(r, 0.999)   # changed code never reads as identical

    if os.path.exists(a.out):
        os.remove(a.out)
    d = sqlite3.connect(a.out)
    d.execute('create table config (main_db text, diff_db text, version text, date text)')
    d.execute('insert into config values (?, ?, ?, ?)', (a.old, a.new, '3.4 (diaphora_lite)', time.asctime()))
    d.execute('create table results (type, line, address, name, address2, name2, ratio, nodes1, nodes2, description)')
    d.execute('create unique index uq_results on results(address, address2)')
    d.execute('create table unmatched (type, line, address, name)')
    rows = []
    for k, (o, (n, why)) in enumerate(sorted(m.pairs.items(), key=lambda x: m.o_by_id[x[0]].address)):
        fo, fn = m.o_by_id[o], m.n_by_id[n]
        r = ratios.get(o, 1.0)
        typ = 'best' if r == 1.0 else 'partial' if r >= 0.5 else 'unreliable'
        rows.append((typ, f'{k:05d}', f'{fo.address:x}', fo.name, f'{fn.address:x}', fn.name, r, fo.nodes, fn.nodes,
                     why))
    d.executemany('insert or ignore into results values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)', rows)
    un = [('primary', f'{k:05d}', f'{f.address:x}', f.name) for k, f in enumerate(m.free_old())]
    un += [('secondary', f'{k:05d}', f'{f.address:x}', f.name) for k, f in enumerate(m.free_new())]
    d.executemany('insert into unmatched values (?, ?, ?, ?)', un)
    d.commit()
    by = defaultdict(int)
    for r in rows:
        by[r[0]] += 1
    print(f'wrote {a.out}: {dict(by)}, unmatched old {len(m.free_old())}, new {len(m.free_new())}, '
          f'{time.time() - t0:.0f} s')


if __name__ == '__main__':
    main()
