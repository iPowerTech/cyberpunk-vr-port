"""What a patch added, removed and changed in the game's archives, with names, and the new files extracted.

    py -3.13 -B tools/patchday/assets_diff.py all [--game <dir>] [--old-toc <toc.json.gz>] [--new-toc <toc.json.gz>]
                                                  [--out build/patchday/assets-<ver>]
    (steps: names -> extract -> harvest -> report; each can be run alone)

The table of contents of every .archive (archives.py toc) has the FNV-1a 64 path hash and SHA-1 of each file,
so the old build's archives are not needed. Names: WolvenKit's hash list (cp77tools archive -l, per archive),
then every depot path found in the extracted new files (CR2W name / import tables, plain-text files) and in the
new script bundle's resource table -- a new file is mostly named by the new files that point at it. Writes
<out>/names.json (hash -> path), <out>/added/ (the new files, cp77tools unbundle), <out>/REPORT.md (counts by
extension and folder, the full lists in <out>/lists/).
"""
import argparse
import collections
import gzip
import json
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from archives import path_hash  # noqa: E402

GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
PATH_RE = re.compile(rb'(?:base|ep1|engine|user)[\\/][\x20-\x7e]{3,250}?\.[a-z0-9_]{2,12}(?=[\x00"\'\s,;<>|]|$)', re.I)


def load_toc(path):
    with gzip.open(path, 'rt', encoding='utf-8') as f:
        return {int(k, 16): v for k, v in json.load(f).items()}


def sets(old, new):
    added = sorted(h for h in new if h not in old)
    removed = sorted(h for h in old if h not in new)
    changed = sorted(h for h in new if h in old and new[h][1] != old[h][1])
    moved = sorted(h for h in new if h in old and new[h][1] == old[h][1] and new[h][0] != old[h][0])
    return added, removed, changed, moved


def step_names(a, new):
    cache = os.path.join(a.out, 'listings')
    os.makedirs(cache, exist_ok=True)
    names = {}
    for rel in sorted({v[0] for v in new.values()}):
        lst = os.path.join(cache, rel.replace('/', '_') + '.txt')
        if not os.path.exists(lst):
            r = subprocess.run(['cp77tools', 'archive', '-l', os.path.join(a.game, rel)], capture_output=True,
                               text=True, timeout=1800, encoding='utf-8', errors='replace')
            with open(lst, 'w', encoding='utf-8') as f:
                f.write(r.stdout)
            print('listed', rel, flush=True)
        for ln in open(lst, encoding='utf-8'):
            ln = ln.strip()
            if ln and not ln.startswith('[') and ('\\' in ln or '/' in ln):
                names[path_hash(ln)] = ln
    return names


def step_extract(a, new, added):
    out = os.path.join(a.out, 'added')
    os.makedirs(out, exist_ok=True)
    by_archive = collections.defaultdict(list)
    for h in added:
        by_archive[new[h][0]].append(h)
    for rel, hashes in sorted(by_archive.items()):
        if 'lang_' in rel and '_voice' in rel:
            continue  # voice-over .wem: counted, not extracted (5 GB of audio)
        listing = os.path.join(a.out, '_hashes.txt')
        with open(listing, 'w', encoding='utf-8') as f:
            f.write('\n'.join(str(h) for h in hashes) + '\n')
        print(f'unbundle {rel}: {len(hashes)} files', flush=True)
        subprocess.run(['cp77tools', 'unbundle', os.path.join(a.game, rel), '-o', out, '--hash', listing],
                       check=False, timeout=7200, capture_output=True)
        os.remove(listing)


def cr2w_strings(data):
    """Every string of a CR2W file's string table (names, import paths)."""
    if data[:4] != b'CR2W' or len(data) < 0xA8:
        return []
    # header: magic, version, flags, timestamp(8), build, objects_end, buffers_end, crc, num_chunks, then 10 tables
    tables = [struct.unpack_from('<III', data, 0x28 + 12 * i) for i in range(10)]
    off, size, _ = tables[0]
    blob = data[off:off + size]
    return [s for s in blob.split(b'\0') if s]


def step_harvest(a, names, unknown):
    found = {}
    root = os.path.join(a.out, 'added')
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            try:
                data = open(p, 'rb').read()
            except OSError:
                continue
            cands = cr2w_strings(data) if data[:4] == b'CR2W' else []
            cands += PATH_RE.findall(data[:4 << 20])
            for c in cands:
                try:
                    s = c.decode('utf-8')
                except UnicodeDecodeError:
                    continue
                if '\\' not in s and '/' not in s:
                    continue
                h = path_hash(s)
                if h in unknown and h not in found:
                    found[h] = s.replace('/', '\\')
    # The new script bundle names some resources directly.
    bundle = os.path.join(a.game, 'r6', 'cache', 'final.redscripts')
    if os.path.exists(bundle):
        for c in PATH_RE.findall(open(bundle, 'rb').read()):
            s = c.decode('utf-8', 'replace')
            h = path_hash(s)
            if h in unknown and h not in found:
                found[h] = s.replace('/', '\\')
    names.update(found)
    return len(found)


def ext_of(name):
    return os.path.splitext(name)[1].lower() if name else '<unnamed>'


def top(name, depth=3):
    if not name:
        return '<unnamed>'
    return '\\'.join(name.split('\\')[:depth])


def step_report(a, old, new, added, removed, changed, moved, names):
    lists = os.path.join(a.out, 'lists')
    os.makedirs(lists, exist_ok=True)
    lines = [f'# Archive changes: {os.path.basename(a.old_toc)} -> {a.new_toc}', '',
             f'- files: {len(old)} -> {len(new)}; added {len(added)}, removed {len(removed)}, changed {len(changed)}, '
             f'moved to another archive {len(moved)}',
             f'- added bytes {sum(new[h][2] for h in added) / 1e6:.1f} MB; named '
             f'{sum(1 for h in added if h in names)} of {len(added)} added, '
             f'{sum(1 for h in changed if h in names)} of {len(changed)} changed, '
             f'{sum(1 for h in removed if h in names)} of {len(removed)} removed', '']
    for title, hs, toc in (('added', added, new), ('removed', removed, old), ('changed', changed, new),
                           ('moved', moved, new)):
        with open(os.path.join(lists, f'{title}.tsv'), 'w', encoding='utf-8') as f:
            for h in sorted(hs, key=lambda h: names.get(h, '~' + f'{h:016x}')):
                f.write(f'{h:016x}\t{toc[h][0]}\t{toc[h][2]}\t{names.get(h, "")}\n')
        by_ext = collections.Counter(ext_of(names.get(h)) for h in hs)
        by_dir = collections.Counter(top(names.get(h)) for h in hs)
        lines += [f'## {title} ({len(hs)})', '', '| extension | files |', '|---|---|']
        lines += [f'| {e} | {n} |' for e, n in by_ext.most_common(40)]
        lines += ['', '| folder | files |', '|---|---|']
        lines += [f'| {d} | {n} |' for d, n in by_dir.most_common(60)]
        lines.append('')
    with open(os.path.join(a.out, 'REPORT.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('report:', os.path.join(a.out, 'REPORT.md'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('step', choices=['all', 'names', 'extract', 'harvest', 'report'])
    ap.add_argument('--game', default=GAME)
    ap.add_argument('--old-toc', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31', 'archive_toc.json.gz'))
    ap.add_argument('--new-toc', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    old, new = load_toc(a.old_toc), load_toc(a.new_toc)
    added, removed, changed, moved = sets(old, new)
    names_path = os.path.join(a.out, 'names.json')
    names = {}
    if os.path.exists(names_path):
        names = {int(k, 16): v for k, v in json.load(open(names_path, encoding='utf-8')).items()}
    if a.step in ('all', 'names'):
        names.update(step_names(a, new))
    if a.step in ('all', 'extract'):
        step_extract(a, new, added)
    if a.step in ('all', 'harvest'):
        unknown = {h for h in added + changed + removed if h not in names}
        n = step_harvest(a, names, unknown)
        print(f'harvest: {n} more names', flush=True)
    with open(names_path, 'w', encoding='utf-8') as f:
        json.dump({f'{h:016x}': v for h, v in names.items()}, f)
    if a.step in ('all', 'report'):
        step_report(a, old, new, added, removed, changed, moved, names)


if __name__ == '__main__':
    main()
