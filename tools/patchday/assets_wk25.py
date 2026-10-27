"""Second WolvenKit pass over the 6,942 added CR2W files with a WolvenKit built for game 2.5.

    py -3.13 -B tools/patchday/assets_wk25.py --cli <WolvenKit.CLI.exe> [--out build/patchday/assets-2.5] [--compare-only]

WolvenKit 8.19 (the first pass, assets_inventory.py) predates 2.5; its maintainers say 9.1.x does not support 2.5
either and work on it in the update_2.5 branch (build/patchday/tools/wolvenkit-src). This pass converts every added
CR2W file again with such a build, parses the JSON in full and compares its Data with the 8.19 JSON of the same
file (the Header differs by design: version, paths). The 203 lipsync animsets of the voice archives are unbundled
again (the first pass kept no copy). Result: <out>/inventory/wk25.tsv, one row per file:
  identical   both versions give the same Data
  differs     both convert, Data differs (first differing path in `detail`)
  new-ok      only the 2.5 build converts it (the 32 cooked sectors), or no 8.19 JSON was kept (voice)
  failed      the 2.5 build writes no JSON
--compare-only keeps <out>/wk25 and only redoes the comparison.
<out>/inventory/wk25_diffs.tsv sums every difference over the `differs` files by path pattern (indices -> [i]):
kind only-old / only-new (a field one version reads and the other does not) or value, with an example.
"""
import argparse
import collections
import csv
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
sys.path.insert(0, HERE)
import assets_inventory as inv  # noqa: E402
import cr2w_read  # noqa: E402
from archives import path_hash  # noqa: E402


def ext_for(path):
    """The extension cp77tools needs: the file's own, or its root class's for <hash>.cr2w / <hash>.bin."""
    base = os.path.basename(path)
    ext = base.rsplit('.', 1)[-1] if '.' in base else ''
    if ext in ('cr2w', 'bin', ''):
        c = cr2w_read.Cr2w(open(path, 'rb').read())
        ext = inv.CLASS_EXT.get(c.names[c.chunks[0][0]], ext) if c.chunks else ext
    return ext


def first_difference(a, b, path='Data'):
    if type(a) is not type(b):
        return path
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                return '%s.%s' % (path, k)
            d = first_difference(a[k], b[k], '%s.%s' % (path, k))
            if d:
                return d
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return '%s[len %d/%d]' % (path, len(a), len(b))
        for i, (x, y) in enumerate(zip(a, b)):
            d = first_difference(x, y, '%s[%d]' % (path, i))
            if d:
                return d
        return None
    return None if a == b else path


def all_differences(a, b, path='Data', out=None):
    """Every difference: (path, kind, old, new); kind only-old / only-new / value."""
    out = [] if out is None else out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            p = '%s.%s' % (path, k)
            if k not in b:
                out.append((p, 'only-old', a[k], None))
            elif k not in a:
                out.append((p, 'only-new', None, b[k]))
            else:
                all_differences(a[k], b[k], p, out)
    elif isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        for i, (x, y) in enumerate(zip(a, b)):
            all_differences(x, y, '%s[%d]' % (path, i), out)
    elif a != b:
        out.append((path, 'value', a, b))
    return out


def short(v):
    return json.dumps(v, ensure_ascii=False)[:120]


def convert(cli, src, dst):
    os.makedirs(dst, exist_ok=True)
    r = subprocess.run([cli, 'convert', 'serialize', src, '-o', dst], capture_output=True, text=True,
                       errors='replace', timeout=8 * 3600)
    return r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cli', required=True)
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'assets-2.5'))
    ap.add_argument('--game', default=GAME)
    ap.add_argument('--compare-only', action='store_true')
    a = ap.parse_args()
    rows = [r for r in csv.DictReader(open(os.path.join(a.out, 'inventory', 'added_inventory.tsv'), encoding='utf-8',
                                           newline=''), delimiter='\t') if r['kind'] == 'cr2w']
    work = os.path.join(a.out, 'wk25')
    src = os.path.join(work, 'in')
    if not a.compare_only:
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(src)
    old_json = inv.json_index(a.out)
    copies, old_of = {}, {}
    voice = collections.defaultdict(list)
    for r in rows:
        s = os.path.join(a.out, r['source']) if r['source'] else ''
        if s and os.path.exists(s):
            name = '%s.%s' % (r['hash'], ext_for(s))
            if not a.compare_only:
                shutil.copyfile(s, os.path.join(src, name))
            copies[name] = r['hash']
            if os.path.normcase(s) in old_json:
                old_of[r['hash']] = old_json[os.path.normcase(s)]
        else:
            voice[r['archive']].append(r['hash'])
    # voice-archive animsets: unbundle again by hash
    vdir = os.path.join(work, 'voice')
    for rel, hashes in sorted(voice.items()) if not a.compare_only else ():
        listing = os.path.join(work, '_hashes.txt')
        with open(listing, 'w', encoding='utf-8') as f:
            f.write('\n'.join(str(int(h, 16)) for h in hashes) + '\n')
        out = os.path.join(vdir, os.path.basename(rel))
        subprocess.run([a.cli, 'unbundle', os.path.join(a.game, rel.replace('/', os.sep)), '-o', out, '--hash', listing],
                       capture_output=True, timeout=7200)
        for root, _, names in os.walk(out):
            for n in names:
                p = os.path.join(root, n)
                key = n[:-4] if n.endswith('.bin') and n[:-4].isdigit() else None
                h = '%016x' % (int(key) if key else path_hash(os.path.relpath(p, out)))
                if h in hashes:
                    name = '%s.%s' % (h, ext_for(p))
                    shutil.copyfile(p, os.path.join(src, name))
                    copies[name] = h
    if a.compare_only:
        for n in os.listdir(src):
            copies.setdefault(n, n.split('.', 1)[0])
    print('copies', len(copies), 'old JSON known', len(old_of), flush=True)
    jdir = os.path.join(work, 'json')
    if not a.compare_only:
        convert(a.cli, src, jdir)
    result, counts = [], collections.Counter()
    patterns, files_of, example = collections.Counter(), collections.defaultdict(set), {}
    for r in rows:
        h = r['hash']
        name = next((n for n, x in copies.items() if x == h), None)
        j = os.path.join(jdir, name + '.json') if name else None
        if not j or not os.path.exists(j) or os.path.getsize(j) == 0:
            status, cls, detail = 'failed', '', 'no source' if not name else 'no JSON written'
        else:
            st, cls, _, summ = inv.summarise_json(j)
            if st != 'wolvenkit-ok':
                status, detail = 'failed', '%s %s' % (st, summ)
            elif h not in old_of:
                status, detail = 'new-ok', summ
            else:
                new = json.load(open(j, encoding='utf-8'))['Data']
                old = json.load(open(old_of[h], encoding='utf-8'))['Data']
                d = first_difference(old, new)
                status, detail = ('identical', summ) if d is None else ('differs', d)
                for p, kind, x, y in all_differences(old, new) if d else ():
                    key = (cls, re.sub(r'\[\d+\]', '[i]', p), kind)
                    patterns[key] += 1
                    files_of[key].add(h)
                    example.setdefault(key, (h, short(x), short(y)))
        counts[status] += 1
        result.append({'hash': h, 'archive': r['archive'], 'path': r['path'], 'class': cls, 'wk25': status,
                       'first_pass': r['status'], 'detail': detail[:300]})
    with open(os.path.join(a.out, 'inventory', 'wk25.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        w = csv.DictWriter(f, fieldnames=list(result[0]), delimiter='\t', lineterminator='\n')
        w.writeheader()
        w.writerows(result)
    with open(os.path.join(a.out, 'inventory', 'wk25_diffs.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('class\tpath\tkind\tcount\tfiles\texample_hash\told\tnew\n')
        for key, n in sorted(patterns.items(), key=lambda kv: -len(files_of[kv[0]])):
            f.write('%s\t%s\t%s\t%d\t%d\t%s\t%s\t%s\n' % (key + (n, len(files_of[key])) + example[key]))
    print('wk25:', dict(counts), 'difference patterns', len(patterns))
    print('by first-pass status:', dict(collections.Counter((x['first_pass'], x['wk25']) for x in result)))


if __name__ == '__main__':
    main()
