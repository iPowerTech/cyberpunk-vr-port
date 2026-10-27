"""Every TweakDB record the port names must still exist in the game's TweakDB.

    py -3.13 -B tools/patchday/check_tweaks.py [--game <dir>]

The port's TweakXL files (mods/tweaks/**.yaml) define records of their own on top of game records
($base: Items.cigarette_i_stick, AttachmentSlots.WeaponRight ...) and edit game records
(Character.Player_Puppet_Base). Its Lua and redscript name more (item records, slots, attacks). A patch
that renames or drops one of them makes TweakXL skip the file or a script look up nothing.

This reads the record table of r6/cache/tweakdb.bin and tweakdb_ep1.bin (a TweakDBID is the CRC-32
of the name plus its length) and checks every name the port references that it does not define
itself. Offline: no game launch.
"""
import argparse
import json
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
PREFIXES = ('Items', 'AttachmentSlots', 'EquipmentArea', 'Character', 'Attacks', 'BaseStatusEffect', 'Ammo',
            'Vehicle', 'WeaponModifiers', 'Effectors', 'Perks', 'NewPerks', 'Proficiencies', 'ItemTypes',
            'Stats', 'Prereqs', 'Interactions', 'Camera', 'PlayerLocomotion', 'UIIcon', 'Quality',
            'RPGActionRewards', 'gamedataStatType', 'WeaponEvolution', 'ItemCategory', 'Inventory')
NAME = re.compile(r'\b(' + '|'.join(PREFIXES) + r')\.([A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)*)')


def tweakdb_ids(path):
    with open(path, 'rb') as f:
        b = f.read()
    magic, _, _, _, _, recs, _, _ = struct.unpack_from('<8I', b, 0)
    if magic not in (0x0BB1DB47, 0x0BB1DB57):
        raise ValueError(f'{path}: not a tweakdb.bin')
    n, = struct.unpack_from('<I', b, recs)
    ids = set()
    for i in range(n):
        v, = struct.unpack_from('<Q', b, recs + 4 + 12 * i)
        ids.add(v & 0xFFFFFFFFFF)
    return ids


def tdbid(name):
    return zlib.crc32(name.encode('utf-8')) | (len(name) << 32)


def scan_references():
    defined, refs = set(), {}
    tweak_root = os.path.join(REPO, 'mods', 'tweaks')
    for dp, _, files in os.walk(tweak_root):
        for f in files:
            if not f.endswith(('.yaml', '.yml')):
                continue
            path = os.path.join(dp, f)
            rel = os.path.relpath(path, REPO).replace('\\', '/')
            with open(path, encoding='utf-8') as fh:
                for ln, line in enumerate(fh, 1):
                    code = line.split('#', 1)[0]
                    m = re.match(r'^([A-Za-z_][\w]*\.[\w.]+):\s*$', code.rstrip())
                    if m and not code.startswith((' ', '\t')):
                        defined.add(m.group(1))
                    for n in NAME.finditer(code):
                        refs.setdefault(n.group(0), []).append(f'{rel}:{ln}')
    for sub, exts in (('mods/cet', ('.lua',)), ('mods/redscript', ('.reds',))):
        for dp, _, files in os.walk(os.path.join(REPO, sub)):
            for f in files:
                if not f.endswith(exts):
                    continue
                path = os.path.join(dp, f)
                rel = os.path.relpath(path, REPO).replace('\\', '/')
                with open(path, encoding='utf-8', errors='replace') as fh:
                    for ln, line in enumerate(fh, 1):
                        # Only quoted names (t"Items.X", 'Items.X', TweakDBID("Items.X")): an
                        # unquoted Items.X in Lua is a table access, not a record.
                        for q in re.finditer(r'["\']((?:' + '|'.join(PREFIXES) + r')\.[A-Za-z0-9_.]+)["\']', line):
                            refs.setdefault(q.group(1), []).append(f'{rel}:{ln}')
    return defined, refs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=GAME)
    ap.add_argument('--baseline', default=os.path.join(HERE, 'data', 'tweak_baseline-2.31.json'))
    ap.add_argument('--write-baseline', action='store_true')
    args = ap.parse_args()
    ids = set()
    used = []
    for name in ('tweakdb.bin', 'tweakdb_ep1.bin'):
        p = os.path.join(args.game, 'r6', 'cache', name)
        if os.path.exists(p):
            ids |= tweakdb_ids(p)
            used.append(name)
    defined, refs = scan_references()
    missing = {}
    checked = 0
    for name, where in sorted(refs.items()):
        if name in defined or any(name.startswith(d + '.') for d in defined):
            continue
        # A flat (Record.field) exists when its record does; the record table holds records only.
        parts = name.split('.')
        candidates = ['.'.join(parts[:k]) for k in range(len(parts), 1, -1)]
        checked += 1
        if not any(tdbid(c) in ids for c in candidates):
            missing[name] = where
    print(f"tweakdb: {', '.join(used)} ({len(ids)} records); port defines {len(defined)}, "
          f"references {checked} game records")
    # Names already absent on the build the port was made for (debug probes of slots that never
    # existed, a third-party mod's items): not a patch's doing, listed once and then left out.
    baseline = set()
    if os.path.exists(args.baseline):
        with open(args.baseline, encoding='utf-8') as f:
            baseline = set(json.load(f)['missing'])
    if args.write_baseline:
        with open(args.baseline, 'w', encoding='utf-8') as f:
            json.dump(dict(game=args.game, missing=sorted(missing)), f, indent=1)
        print(f'baseline: {len(missing)} names -> {args.baseline}')
        baseline = set(missing)
    new = {n: w for n, w in missing.items() if n not in baseline}
    for name, where in missing.items():
        tag = 'MISSING' if name in new else 'absent (baseline)'
        print(f'  {tag} {name}  <- {", ".join(where[:4])}')
    gone_from_baseline = sorted(baseline - set(missing))
    if gone_from_baseline:
        print('  now present (were absent):', ', '.join(gone_from_baseline))
    print('TWEAKS', 'OK' if not new else f'{len(new)} MISSING')
    sys.exit(0 if not new else 1)


if __name__ == '__main__':
    main()
