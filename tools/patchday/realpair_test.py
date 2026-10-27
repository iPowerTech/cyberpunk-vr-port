"""The patch-day tools on a REAL pair of game builds: 2.31 (the source's) and an older one from Steam.

    py -3.13 -B tools/patchday/realpair_test.py [--other build/patchday/base-2.3]

The older build's exe, address library, script bundle, TweakDB, shader caches and settings schema come
from the depot (DepotDownloader -filelist, see docs/patchday-runbook.md). Going backwards (2.31 -> 2.3) is
as real a move as forwards: every function shifted, some changed.

1. remap 2.31 -> other, applied to a scratch copy of src/ include/ (report: build/patchday/realpair/).
2. inventory of that scratch tree against the other exe, then remap other -> 2.31 applied back to it.
   The round trip must give the original tree byte for byte, apart from the addresses step 1 could not
   carry (they were left at their 2.31 values and are listed) -- the strongest check there is that the
   rewrite lands on the same code both ways.
3. the other checks against the other build: field offsets, script diff, TweakDB, settings, shaders.
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
BASE = os.path.join(REPO, 'build', 'patchday', 'base-2.31')
WORK = os.path.join(REPO, 'build', 'patchday', 'realpair')
PY = [sys.executable, '-B']


def run(args, ok_codes=(0,)):
    print('>', ' '.join(args), flush=True)
    r = subprocess.run(PY + args, cwd=REPO, capture_output=True, text=True)
    out = (r.stdout or '') + (r.stderr or '')
    for ln in out.strip().splitlines()[-12:]:
        print('   ', ln)
    if r.returncode not in ok_codes:
        print(f'   (exit {r.returncode})')
    return r.returncode, out


def tree_hashes(root):
    import hashlib
    out = {}
    for d in ('src', 'include'):
        for dp, _, files in os.walk(os.path.join(root, d)):
            for f in files:
                p = os.path.join(dp, f)
                with open(p, 'rb') as fh:
                    out[os.path.relpath(p, root).replace('\\', '/')] = hashlib.sha1(fh.read()).hexdigest()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--other', default=os.path.join(REPO, 'build', 'patchday', 'base-2.3'))
    args = ap.parse_args()
    exe = os.path.join(args.other, 'bin', 'x64', 'Cyberpunk2077.exe')
    lib = os.path.join(args.other, 'bin', 'x64', 'cyberpunk2077_addresses.json')
    if not (os.path.exists(exe) and os.path.exists(lib)):
        sys.exit(f'{args.other} needs bin/x64/Cyberpunk2077.exe and cyberpunk2077_addresses.json (DepotDownloader)')
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    scratch = os.path.join(WORK, 'repo')
    for d in ('src', 'include'):
        shutil.copytree(os.path.join(REPO, d), os.path.join(scratch, d))
    db = os.path.join(HERE, 'data', 'rva_db-68af45ea.json')

    # 1. forwards (2.31 -> other), applied to the scratch tree
    run([os.path.join(HERE, 'remap.py'), '--db', db, '--new', args.other, '--apply', '--repo', scratch,
         '--version', 'realpair', '--out', os.path.join(WORK, 'forward')], ok_codes=(0, 2))
    with open(os.path.join(WORK, 'forward', 'map.json'), encoding='utf-8') as f:
        fwd = json.load(f)
    st = collections.Counter(r['status'] for r in fwd.values())
    unmapped = {int(k, 16) for k, r in fwd.items() if r['new'] is None}

    # 2. back (other -> 2.31): inventory of the rewritten scratch tree against the other exe
    other_db = os.path.join(WORK, 'rva_db-other.json')
    run([os.path.join(HERE, 'inventory.py'), '--game', args.other, '--repo', scratch, '--out', other_db,
         '--overrides', os.path.join(WORK, 'forward', 'literal_overrides.json')])
    run([os.path.join(HERE, 'remap.py'), '--db', other_db, '--new', BASE, '--apply', '--repo', scratch,
         '--version', '2.31', '--out', os.path.join(WORK, 'back')], ok_codes=(0, 2))
    a, b = tree_hashes(REPO), tree_hashes(scratch)
    differ = sorted(k for k in a if a[k] != b.get(k))
    # A literal that became digits only on the way out (0x219E09 -> 0x219309) has lost its case;
    # coming back it takes the line's or the file's. Same value, so not a difference that matters.
    hexlit = re.compile(r'0[xX][0-9A-Fa-f]+')

    def norm(rel, root):
        with open(os.path.join(root, rel), encoding='utf-8', errors='replace') as f:
            return hexlit.sub(lambda m: m.group(0).lower(), f.read())
    case_only = [k for k in differ if norm(k, REPO) == norm(k, scratch)]
    differ = [k for k in differ if k not in case_only]
    print(f'\nforward statuses: {dict(st.most_common())}')
    print(f'round trip: {len(a) - len(differ) - len(case_only)} of {len(a)} files byte-identical, '
          f'{len(case_only)} differ only in the letter case of a hex literal, {len(differ)} differ')
    # A file may differ only where step 1 left a value alone (an unmapped address stays the 2.31 one,
    # and step 2 then reads it as an address of the other build).
    for rel in differ[:30]:
        with open(os.path.join(REPO, rel), encoding='utf-8', errors='replace') as f1, \
                open(os.path.join(scratch, rel), encoding='utf-8', errors='replace') as f2:
            l1, l2 = f1.read().split('\n'), f2.read().split('\n')
        for n, (x, y) in enumerate(zip(l1, l2), 1):
            if x != y:
                print(f'  {rel}:{n}\n     2.31: {x.strip()[:150]}\n     trip: {y.strip()[:150]}')
                break
    print(f'(forward left {len(unmapped)} addresses unmapped; their lines are the expected differences)')

    # 3. the other checks against the other build
    run([os.path.join(HERE, 'offsets.py'), 'check', '--new', args.other], ok_codes=(0, 2))
    if os.path.exists(os.path.join(args.other, 'r6', 'cache', 'final.redscripts')):
        run([os.path.join(HERE, 'reds_diff.py'), '--old', os.path.join(BASE, 'r6', 'cache', 'final.redscripts'),
             '--new', os.path.join(args.other, 'r6', 'cache', 'final.redscripts')], ok_codes=(0, 1))
    if os.path.exists(os.path.join(args.other, 'r6', 'cache', 'tweakdb.bin')):
        run([os.path.join(HERE, 'check_tweaks.py'), '--game', args.other], ok_codes=(0, 1))
    if os.path.exists(os.path.join(args.other, 'r6', 'config', 'settings', 'options.json')):
        run([os.path.join(HERE, 'check_settings.py'), '--game', args.other], ok_codes=(0, 1))
    if os.path.exists(os.path.join(args.other, 'engine', 'shader_final.cache')):
        run([os.path.join(HERE, 'check_shaders.py'), 'check', '--game', args.other], ok_codes=(0, 1))


if __name__ == '__main__':
    main()
