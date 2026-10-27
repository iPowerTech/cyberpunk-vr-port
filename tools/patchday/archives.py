"""Which game files the port's archive replaces, and whether a patch changed them underneath it.

    py -3.13 -B tools/patchday/archives.py toc       --game <dir> [--out <toc.json.gz>]
    py -3.13 -B tools/patchday/archives.py overrides [--toc <toc>] [--archive <our .archive>]
    py -3.13 -B tools/patchday/archives.py originals [--toc <toc>] --game <dir> --out <dir>
    py -3.13 -B tools/patchday/archives.py diff      --old-toc <toc> --new-toc <toc>

An .archive (RDAR) indexes its files by FNV-1a 64 of the lowercase path and stores a SHA-1 of each
one, so a table of contents is enough to see which base files a patch touched -- no 90 GB copy of the
old game is needed. The port's own archive (mods/archive/cyberpunkvrport.archive) replaces some base
files outright (weapon .app/.ent, animation sets, effects): those are the ones a patch can silently
undo or break. `originals` extracts their old base versions so a changed one can be merged three-way
(old base, new base, ours).
"""
import argparse
import gzip
import json
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
BASE = os.path.join(REPO, 'build', 'patchday', 'base-2.31')
OUR_ARCHIVE = os.path.join(REPO, 'mods', 'archive', 'cyberpunkvrport.archive')
ARCHIVE_DIRS = (os.path.join('archive', 'pc', 'content'), os.path.join('archive', 'pc', 'ep1'))
ENTRY = np.dtype([('hash', '<u8'), ('time', '<u8'), ('inline', '<u4'), ('seg0', '<u4'), ('seg1', '<u4'),
                  ('dep0', '<u4'), ('dep1', '<u4'), ('sha1', 'V20')])
SEGMENT = np.dtype([('offset', '<u8'), ('zsize', '<u4'), ('size', '<u4')])


def fnv1a64(text):
    h = 0xCBF29CE484222325
    for b in text.encode('utf-8'):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def path_hash(path):
    return fnv1a64(path.replace('/', '\\').lower())


def read_toc(path):
    """[(hash, sha1 hex, uncompressed size)] of one RDAR archive."""
    with open(path, 'rb') as f:
        head = f.read(40)
        magic, version, index_pos, index_size = struct.unpack_from('<4sIQI', head, 0)
        if magic != b'RDAR':
            raise ValueError(f'{path}: not an RDAR archive')
        f.seek(index_pos)
        index = f.read(index_size)
    _, _, _, n_files, n_segments, _ = struct.unpack_from('<IIQIII', index, 0)
    off = 28
    entries = np.frombuffer(index, dtype=ENTRY, count=n_files, offset=off)
    segs = np.frombuffer(index, dtype=SEGMENT, count=n_segments, offset=off + n_files * ENTRY.itemsize)
    out = []
    for e in entries:
        size = int(segs[e['seg0']]['size']) if e['seg0'] < len(segs) else 0
        out.append((int(e['hash']), bytes(e['sha1']).hex(), size))
    return out


def build_toc(game):
    toc = {}
    for d in ARCHIVE_DIRS:
        full = os.path.join(game, d)
        if not os.path.isdir(full):
            continue
        for name in sorted(os.listdir(full)):
            if not name.endswith('.archive'):
                continue
            rel = os.path.join(d, name).replace('\\', '/')
            for h, sha, size in read_toc(os.path.join(full, name)):
                # The later archive wins, as the game loads them; record the shadowed ones too.
                prev = toc.get(h)
                toc[h] = [rel, sha, size] + ([prev] if prev else [])
    return toc


def save_toc(toc, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with gzip.open(path, 'wt', encoding='utf-8') as f:
        json.dump({f'{k:016x}': v for k, v in toc.items()}, f)


def load_toc(path):
    with gzip.open(path, 'rt', encoding='utf-8') as f:
        return {int(k, 16): v for k, v in json.load(f).items()}


def list_names(archive):
    """Paths in an archive, from WolvenKit's hash list (cp77tools archive -l)."""
    r = subprocess.run(['cp77tools', 'archive', '-l', archive], capture_output=True, text=True, timeout=600)
    names = [ln.strip() for ln in r.stdout.splitlines() if ln.strip() and not ln.startswith('[')]
    return [n for n in names if '\\' in n or '/' in n or n.startswith('0x') or n.isdigit()]


def overrides(toc, archive):
    names = list_names(archive)
    ours = {h for h, _, _ in read_toc(archive)}
    named = {path_hash(n): n for n in names if not n.isdigit()}
    unnamed = ours - set(named)
    rows = []
    for h in sorted(ours):
        if h in toc:
            base = toc[h]
            rows.append(dict(hash=f'{h:016x}', path=named.get(h, f'<{h:016x}>'), archive=base[0], sha1=base[1],
                             size=base[2]))
    return rows, len(ours), len(unnamed)


def extract_originals(game, rows, out):
    """The old base version of every replaced file, archive by archive (WolvenKit unbundle --hash)."""
    by_archive = {}
    for r in rows:
        by_archive.setdefault(r['archive'], []).append(int(r['hash'], 16))
    os.makedirs(out, exist_ok=True)
    for rel, hashes in sorted(by_archive.items()):
        listing = os.path.join(out, '_hashes.txt')
        with open(listing, 'w', encoding='utf-8') as f:
            f.write('\n'.join(str(h) for h in hashes) + '\n')
        cmd = ['cp77tools', 'unbundle', os.path.join(game, rel), '-o', out, '--hash', listing]
        print('>', ' '.join(cmd), f'({len(hashes)} files)')
        subprocess.run(cmd, check=False, timeout=3600, capture_output=True)
        os.remove(listing)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    t = sub.add_parser('toc')
    t.add_argument('--game', required=True)
    t.add_argument('--out', default=None)
    o = sub.add_parser('overrides')
    o.add_argument('--toc', default=os.path.join(BASE, 'archive_toc.json.gz'))
    o.add_argument('--archive', default=OUR_ARCHIVE)
    o.add_argument('--out', default=os.path.join(BASE, 'archive_overrides.json'))
    x = sub.add_parser('originals')
    x.add_argument('--overrides', default=os.path.join(BASE, 'archive_overrides.json'))
    x.add_argument('--game', required=True)
    x.add_argument('--out', default=os.path.join(BASE, 'archive_originals'))
    d = sub.add_parser('diff')
    d.add_argument('--overrides', default=os.path.join(BASE, 'archive_overrides.json'))
    d.add_argument('--new-toc', required=True)
    d.add_argument('--out', default=None)
    args = ap.parse_args()

    if args.cmd == 'toc':
        toc = build_toc(args.game)
        out = args.out or os.path.join(args.game if os.path.abspath(args.game).startswith(REPO) else BASE,
                                       'archive_toc.json.gz')
        save_toc(toc, out)
        print(f'{len(toc)} files indexed -> {out}')
    elif args.cmd == 'overrides':
        toc = load_toc(args.toc)
        rows, total, unnamed = overrides(toc, args.archive)
        with open(args.out, 'w', encoding='utf-8') as f:
            json.dump(dict(archive=args.archive, files=total, unnamed=unnamed, overrides=rows), f, indent=1)
        print(f'{total} files in {os.path.basename(args.archive)}, {len(rows)} replace a base file '
              f'({unnamed} without a known name) -> {args.out}')
        kinds = {}
        for r in rows:
            ext = os.path.splitext(r['path'])[1]
            kinds[ext] = kinds.get(ext, 0) + 1
        print('  by type:', dict(sorted(kinds.items(), key=lambda kv: -kv[1])))
    elif args.cmd == 'originals':
        with open(args.overrides, encoding='utf-8') as f:
            rows = json.load(f)['overrides']
        extract_originals(args.game, rows, args.out)
        n = sum(len(fs) for _, _, fs in os.walk(args.out))
        print(f'{n} original files under {args.out}')
    elif args.cmd == 'diff':
        with open(args.overrides, encoding='utf-8') as f:
            rows = json.load(f)['overrides']
        new = load_toc(args.new_toc)
        changed, gone, same = [], [], 0
        for r in rows:
            n = new.get(int(r['hash'], 16))
            if n is None:
                gone.append(r)
            elif n[1] != r['sha1']:
                changed.append(dict(r, new_sha1=n[1], new_archive=n[0], new_size=n[2]))
            else:
                same += 1
        out = args.out or os.path.join(os.path.dirname(args.new_toc), 'archive_diff.json')
        with open(out, 'w', encoding='utf-8') as f:
            json.dump(dict(same=same, changed=changed, gone=gone), f, indent=1)
        print(f'replaced base files: {same} unchanged, {len(changed)} CHANGED by the patch, {len(gone)} gone')
        for r in changed:
            print('  CHANGED', r['path'], f"({r['archive']} -> {r['new_archive']})")
        for r in gone:
            print('  GONE   ', r['path'])
        print('->', out)


if __name__ == '__main__':
    main()
