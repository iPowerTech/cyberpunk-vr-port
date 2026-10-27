"""2.5 compatibility archive for the user's Always_Best_Quality mod (ink atlases always at their 4K texture).

    py -3.13 -B tools/patchday/compat_atlases.py [--work build/abq-2.5] [--out build/patchday/stage-2.5] [--install]

The mod replaces 1129 base .inkatlas files with copies whose lower-resolution slots (1 = 1080p, 2 = 720p) are
cleared, so ink always takes slot 0's 4K texture. It was made on 2.31. Patch 2.5 re-packed 49 of those atlases
(item_icons0..23, atlas_common, mappin_icons, icons_keyboard, ...): the 4K textures are new and so are their
parts, and the mod's 2.31 copies cut the 2.5 textures with 2.31 rectangles -- wrong item icons in the inventory,
key icons in the binds, HUD icons (2026-10-24).

This regenerates those 49 from the 2.5 files with the mod's own rule, and refuses when the mod does anything
else: on every atlas the patch did not touch (there the 2.5 base is the 2.31 base the mod was made from) the
only difference allowed is slots 1 and 2 cleared. The result is a separate archive that sorts before the mod
(archives in archive/pc/mod load by name; the first one that has a file wins), so the mod stays as Vortex
installed it and the compat archive is removed once the mod has a 2.5 release.

Steps (cached in --work): the 2.31/2.5 tables of contents (build/patchday/base-*/archive_toc.json.gz) -> the
atlases to redo; cp77tools unbundle of the mod and of the 2.5 base files; JSON (cp77tools convert serialize);
the rule check; the 49 rebuilt (convert deserialize); cp77tools pack.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

import archives as A

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PD = os.path.join(REPO, 'build', 'patchday')
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
MOD = 'Always_Best_Quality.archive'
COMPAT = '!Always_Best_Quality-2.5-compat.archive'
CLEARED = (1, 2)


def run(cmd, timeout=7200):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if r.returncode:
        raise SystemExit(f'compat: {" ".join(cmd[:3])} failed ({r.returncode}): {r.stdout[-400:]}{r.stderr[-400:]}')
    return r.stdout


def files_under(root, suffix):
    out = []
    for d, _, ns in os.walk(root):
        out += [os.path.relpath(os.path.join(d, n), root) for n in ns if n.endswith(suffix)]
    return out


def unbundle(archive, hashes, out):
    listing = os.path.join(out, '_hashes.txt')
    os.makedirs(out, exist_ok=True)
    with open(listing, 'w') as f:
        f.write('\n'.join(map(str, hashes)) + '\n')
    try:
        run(['cp77tools', 'unbundle', archive, '-o', out, '--hash', listing])
    finally:
        os.remove(listing)


def leaves(x, path=''):
    if isinstance(x, dict):
        for k, v in x.items():
            yield from leaves(v, f'{path}/{k}')
    elif isinstance(x, list):
        for i, v in enumerate(x):
            yield from leaves(v, f'{path}[{i}]')
    else:
        yield path, x


def root_chunk(path):
    return json.load(open(path, encoding='utf-8'))['Data']['RootChunk']


def is_cleared(slot):
    return slot['texture']['DepotPath']['$value'] == '0' and not slot['parts'] and not slot['slices']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--work', default=os.path.join(REPO, 'build', 'abq-2.5'))
    ap.add_argument('--out', default=os.path.join(PD, 'stage-2.5'))
    ap.add_argument('--install', action='store_true', help='copy the archive into the game (the game must be closed)')
    a = ap.parse_args()
    mod_archive = os.path.join(GAME, 'archive', 'pc', 'mod', MOD)
    old = A.load_toc(os.path.join(PD, 'base-2.31', 'archive_toc.json.gz'))
    new = A.load_toc(os.path.join(PD, 'base-2.5', 'archive_toc.json.gz'))
    names = {A.path_hash(n): n for n in A.list_names(mod_archive) if not n.isdigit()}
    ours = [h for h, _, _ in A.read_toc(mod_archive)]
    if len(names) != len(ours) or any(h not in new for h in ours):
        raise SystemExit('compat: the mod has files without a known name or that replace no 2.5 base file')
    redo = sorted(names[h] for h in ours if old.get(h, (None, None))[1] != new[h][1])
    print(f'{MOD}: {len(ours)} atlases, {len(redo)} changed by 2.5')

    mod_dir, base_dir = os.path.join(a.work, 'mod'), os.path.join(a.work, 'base')
    if len(files_under(mod_dir, '.inkatlas')) != len(ours):
        run(['cp77tools', 'unbundle', mod_archive, '-o', mod_dir])
    by = {}
    for h in ours:
        if not os.path.isfile(os.path.join(base_dir, names[h])):
            by.setdefault(new[h][0], []).append(h)
    for rel, hashes in by.items():
        unbundle(os.path.join(GAME, rel), hashes, base_dir)
    for d in (mod_dir, base_dir):
        if len(files_under(d, '.inkatlas.json')) != len(ours):
            run(['cp77tools', 'convert', 'serialize', d])

    # The mod's rule, checked where the 2.5 base is the base it was made from.
    for rel in (names[h] for h in ours):
        if rel in redo:
            continue
        m, b = root_chunk(os.path.join(mod_dir, rel + '.json')), root_chunk(os.path.join(base_dir, rel + '.json'))
        lm, lb = dict(leaves(m)), dict(leaves(b))
        for k in set(lm) | set(lb):
            if lm.get(k) != lb.get(k) and not re.match(r'/slots/Elements\[[12]\]/', k):
                raise SystemExit(f'compat: {rel}: the mod changes {k} -- not only the low-resolution slots')

    gen = os.path.join(a.work, 'gen')
    if os.path.isdir(gen):
        shutil.rmtree(gen)
    for rel in redo:
        doc = json.load(open(os.path.join(base_dir, rel + '.json'), encoding='utf-8'))
        modded = root_chunk(os.path.join(mod_dir, rel + '.json'))['slots']['Elements']
        slots = doc['Data']['RootChunk']['slots']['Elements']
        if is_cleared(slots[0]):
            raise SystemExit(f'compat: {rel}: the 2.5 atlas has no 4K slot to keep')
        for i in CLEARED:
            if i < len(slots) and i < len(modded) and is_cleared(modded[i]):
                slots[i] = modded[i]
        path = os.path.join(gen, 'json', rel + '.json')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        json.dump(doc, open(path, 'w', encoding='utf-8'), indent=2)
    run(['cp77tools', 'convert', 'deserialize', os.path.join(gen, 'json')])
    tree = os.path.join(gen, 'tree')
    for rel in redo:
        src = os.path.join(gen, 'json', rel)
        if not os.path.isfile(src):
            raise SystemExit(f'compat: deserialize wrote no {rel}')
        os.makedirs(os.path.dirname(os.path.join(tree, rel)), exist_ok=True)
        shutil.move(src, os.path.join(tree, rel))
    run(['cp77tools', 'pack', tree, '-o', gen])
    packed = os.path.join(gen, os.path.basename(tree) + '.archive')
    staged = os.path.join(a.out, 'AlwaysBestQuality-compat', 'archive', 'pc', 'mod', COMPAT)
    os.makedirs(os.path.dirname(staged), exist_ok=True)
    shutil.copyfile(packed, staged)
    zpath = os.path.join(a.out, 'AlwaysBestQuality-compat-cp2077-2.5.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(staged, f'archive/pc/mod/{COMPAT}')
    check = {A.path_hash(r) for r in redo}
    if {h for h, _, _ in A.read_toc(staged)} != check:
        raise SystemExit('compat: the packed archive does not hold exactly the rebuilt atlases')
    print(f'{len(redo)} atlases -> {staged}\n  zip {zpath}')
    if a.install:
        dst = os.path.join(GAME, 'archive', 'pc', 'mod', COMPAT)
        if os.path.exists(dst) and os.stat(dst).st_nlink > 1:
            raise SystemExit(f'compat: {dst} is a hardlink (Vortex) -- not overwriting it')
        shutil.copyfile(staged, dst)
        print('installed', dst)


if __name__ == '__main__':
    main()
