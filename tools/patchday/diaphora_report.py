"""The whole-exe Diaphora diff, read for the port: what changed, what is new, what is gone, and which of it the
port and the frameworks touch.

    py -3.13 -B tools/patchday/diaphora_report.py --diff <old_vs_new.diaphora> --old-db <old.sqlite> --new-db <new.sqlite>
                                                 [--out build/patchday/diaphora-<ver>]

Diaphora's results: best (same function), partial / unreliable (matched, changed, with a similarity ratio),
multimatch, and the unmatched functions of either side (primary = only in the old build = removed or not found,
secondary = only in the new build = added). This adds, per row: the address-library hashes of both addresses
(a matched pair whose hashes agree is confirmed; a hash present on one side only marks a renamed symbol), the
size and pseudo-code line counts from the export databases, the old IDA name (the 2.31 database carries the
port's RE names), and whether the port (rva_db) or a framework (framework_hashes.json) uses the old address.
Writes REPORT.md (totals, every changed function the port/frameworks use, the largest changes, the added
functions by size), and the full tables as TSV.
"""
import argparse
import json
import os
import sqlite3
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import AddressLibrary, Image, find_game_files  # noqa: E402

BASE = 0x140000000


def lib(game):
    exe, path = find_game_files(game)
    return AddressLibrary(path, Image(exe))


def func_info(db):
    c = sqlite3.connect(f'file:{db}?mode=ro', uri=True)
    out = {}
    for addr, name, size, plines, nodes in c.execute(
            'select address, name, size, pseudocode_lines, nodes from functions'):
        out[int(addr)] = (name, size, plines, nodes)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--diff', required=True)
    ap.add_argument('--old-db', required=True)
    ap.add_argument('--new-db', required=True)
    ap.add_argument('--old-game', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new-game', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--rva-db', default=os.path.join(HERE, 'data', 'rva_db-68af45ea.json'))
    ap.add_argument('--framework-hashes', default=os.path.join(REPO, 'build', 'patchday', 'framework_hashes.json'))
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'diaphora-2.5'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    old_lib, new_lib = lib(a.old_game), lib(a.new_game)
    old_f, new_f = func_info(a.old_db), func_info(a.new_db)
    port = {}
    if os.path.exists(a.rva_db):
        db = json.load(open(a.rva_db, encoding='utf-8'))
        for o in db['occurrences']:
            v = o.get('value')
            if isinstance(v, int):
                port.setdefault(v, o.get('file'))
    fw = {}
    if os.path.exists(a.framework_hashes):
        for r in json.load(open(a.framework_hashes, encoding='utf-8')):
            for rva in r['old']:
                fw.setdefault(rva, set()).add(r['framework'] + ':' + r['names'][0])

    def used(rva_old, size):
        tags = []
        if rva_old in fw:
            tags += sorted(fw[rva_old])
        hit = [p for p in port if rva_old <= p < rva_old + max(size or 1, 1)]
        if hit:
            tags.append('port:' + str(port[hit[0]]))
        return tags

    d = sqlite3.connect(f'file:{a.diff}?mode=ro', uri=True)
    rows = []
    for typ, line, addr, name, addr2, name2, ratio, n1, n2, desc in d.execute('select * from results'):
        ro, rn = int(addr, 16) - BASE, int(addr2, 16) - BASE
        ho, hn = old_lib.exact(ro), new_lib.exact(rn)
        same_hash = bool(set(ho) & set(hn))
        fo, fn = old_f.get(BASE + ro, (None, 0, 0, 0)), new_f.get(BASE + rn, (None, 0, 0, 0))
        rows.append(dict(type=typ, old=ro, new=rn, name=fo[0] or name, ratio=float(ratio), desc=desc,
                         hash_old=ho[:2], hash_new=hn[:2], hash_agree=same_hash, size_old=fo[1], size_new=fn[1],
                         lines_old=fo[2], lines_new=fn[2], used=used(ro, fo[1])))
    unmatched = {'primary': [], 'secondary': []}
    for typ, line, addr, name in d.execute('select * from unmatched'):
        r = int(addr, 16) - BASE
        info = (old_f if typ == 'primary' else new_f).get(BASE + r, (name, 0, 0, 0))
        unmatched[typ].append(dict(rva=r, name=info[0] or name, size=info[1], lines=info[2],
                                   hashes=(old_lib if typ == 'primary' else new_lib).exact(r)[:2],
                                   used=used(r, info[1]) if typ == 'primary' else []))
    types = Counter(r['type'] for r in rows)
    changed = [r for r in rows if r['ratio'] < 1.0]
    lines = ['# Cyberpunk2077.exe 2.31 -> 2.5, whole-binary Diaphora diff', '',
             f'- functions: old {len(old_f)}, new {len(new_f)}',
             f'- matched: ' + ', '.join(f'{k} {v}' for k, v in types.most_common()),
             f'- matched with changes (ratio < 1): {len(changed)}; identical: {len(rows) - len(changed)}',
             f'- only in 2.31 (removed or not found): {len(unmatched["primary"])}; only in 2.5 (added): '
             f'{len(unmatched["secondary"])}',
             f'- matched pairs whose address-library hash agrees: {sum(r["hash_agree"] for r in rows)}; '
             f'hash on old side only (renamed / re-signed symbol): '
             f'{sum(1 for r in rows if r["hash_old"] and not r["hash_agree"])}', '']
    use = [r for r in rows if r['used']]
    lines += [f'## Functions the port or a framework uses ({len(use)} matched, '
              f'{sum(1 for u in unmatched["primary"] if u["used"])} unmatched)', '',
              '| old | new | ratio | type | size | hash | name | used by |', '|---|---|---|---|---|---|---|---|']
    for r in sorted(use, key=lambda r: r['ratio']):
        lines.append(f'| {r["old"]:#x} | {r["new"]:#x} | {r["ratio"]:.3f} | {r["type"]} | {r["size_old"]}->{r["size_new"]} '
                     f'| {"ok" if r["hash_agree"] else ("renamed" if r["hash_old"] else "-")} | {r["name"]} | '
                     f'{"; ".join(r["used"])[:120]} |')
    for u in unmatched['primary']:
        if u['used']:
            lines.append(f'| {u["rva"]:#x} | - | - | UNMATCHED | {u["size"]} | - | {u["name"]} | {"; ".join(u["used"])[:120]} |')
    lines += ['', '## Largest changed functions (by old size, ratio < 0.9)', '',
              '| old | new | ratio | size | name |', '|---|---|---|---|---|']
    for r in sorted((r for r in changed if r['ratio'] < 0.9), key=lambda r: -r['size_old'])[:150]:
        lines.append(f'| {r["old"]:#x} | {r["new"]:#x} | {r["ratio"]:.3f} | {r["size_old"]}->{r["size_new"]} | {r["name"]} |')
    lines += ['', '## Largest added functions', '', '| new | size | lines | name |', '|---|---|---|---|']
    for u in sorted(unmatched['secondary'], key=lambda u: -(u['size'] or 0))[:150]:
        lines.append(f'| {u["rva"]:#x} | {u["size"]} | {u["lines"]} | {u["name"]} |')
    lines += ['', '## Largest removed (unmatched old) functions', '', '| old | size | name |', '|---|---|---|']
    for u in sorted(unmatched['primary'], key=lambda u: -(u['size'] or 0))[:100]:
        lines.append(f'| {u["rva"]:#x} | {u["size"]} | {u["name"]} |')
    with open(os.path.join(a.out, 'REPORT.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(a.out, 'matched.tsv'), 'w', encoding='utf-8') as f:
        f.write('type\told\tnew\tratio\tsize_old\tsize_new\thash_agree\tname\tused\tdesc\n')
        for r in rows:
            f.write(f'{r["type"]}\t{r["old"]:#x}\t{r["new"]:#x}\t{r["ratio"]:.4f}\t{r["size_old"]}\t{r["size_new"]}\t'
                    f'{int(r["hash_agree"])}\t{r["name"]}\t{";".join(r["used"])}\t{r["desc"]}\n')
    for k, v in unmatched.items():
        with open(os.path.join(a.out, f'unmatched_{"old" if k == "primary" else "new"}.tsv'), 'w', encoding='utf-8') as f:
            for u in v:
                f.write(f'{u["rva"]:#x}\t{u["size"]}\t{u["lines"]}\t{u["name"]}\t{u["hashes"]}\t{";".join(u["used"])}\n')
    print('\n'.join(lines[:10]))
    print('report:', os.path.join(a.out, 'REPORT.md'))


if __name__ == '__main__':
    main()
