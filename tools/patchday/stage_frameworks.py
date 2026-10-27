"""Stage the frameworks built here for a new game build: game-layout trees and Vortex zips.

    py -3.13 -B tools/patchday/stage_frameworks.py [--game-version 2.5] [--out build/patchday/stage-2.5]

For each framework the official release staged by frameworks.py (build/patchday/frameworks/<name>-<tag>/files)
is the base -- licenses, data, bundles, config the build does not produce -- and what was rebuilt for the new
game build replaces its counterpart: the DLLs from build/patchday/framework-src/<repo>, the psiberx Scripts
folders composed from the checkout's scripts/ the way their release does (compose_reds.py), redscript's
patched scc_lib next to the preview's scc. CET is the official release as published.

Writes <out>/<Name>/ (the files in the game's layout, so a test install can copy them over the game folder),
<out>/<Name>-<version>-cp2077-<game>.zip (the same tree, for Vortex: drag onto the Mods page) and
<out>/manifest.json (every file with its sha256 and where it came from).

The compatibility patches of the user's other mods (compat_mods.py) are staged the same way, after the frameworks.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile

import compat_mods

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PD = os.path.join(REPO, 'build', 'patchday')
SRC = os.path.join(PD, 'framework-src')
REL = os.path.join(PD, 'frameworks')

# name, release folder, version label, {game-relative path: built file}, scripts (checkout, plugin name)
FRAMEWORKS = [
    ('RED4ext', 'red4ext-v1.30.0', '1.30.0', {
        'bin/x64/winmm.dll': 'RED4ext/build/bin/winmm.dll',
        'red4ext/RED4ext.dll': 'RED4ext/build/bin/RED4ext.dll'}, None),
    ('ArchiveXL', 'archivexl-v1.27.5', '1.27.5', {
        'red4ext/plugins/ArchiveXL/ArchiveXL.dll': 'cp2077-archive-xl/build/windows/x64/release/ArchiveXL.dll'},
     ('cp2077-archive-xl', 'ArchiveXL')),
    ('TweakXL', 'tweakxl-v1.11.5', '1.11.5', {
        'red4ext/plugins/TweakXL/TweakXL.dll': 'cp2077-tweak-xl/build/windows/x64/release/TweakXL.dll'},
     ('cp2077-tweak-xl', 'TweakXL')),
    ('Codeware', 'codeware-v1.20.6', '1.20.6', {
        'red4ext/plugins/Codeware/Codeware.dll': 'cp2077-codeware/build/windows/x64/release/Codeware.dll'},
     ('cp2077-codeware', 'Codeware')),
    ('redscript', 'redscript-v1.0.0-preview.22', '1.0.0-preview.22', {
        'engine/tools/scc_lib.dll': 'redscript/target/release/scc_lib.dll'}, None),
    ('CyberEngineTweaks', 'cet-v1.38.0', '1.38.0', {}, None),
]


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game-version', default='2.5')
    ap.add_argument('--out', default=os.path.join(PD, 'stage-2.5'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    manifest = {}
    for name, rel, version, built, scripts in FRAMEWORKS:
        base = os.path.join(REL, rel, 'files')
        dst = os.path.join(a.out, name)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(base, dst)
        origin = {os.path.relpath(os.path.join(d, n), dst).replace('\\', '/'): 'release ' + rel
                  for d, _, ns in os.walk(dst) for n in ns}
        for game_rel, src_rel in built.items():
            src = os.path.join(SRC, src_rel)
            if not os.path.isfile(src):
                sys.exit(f'{name}: missing build output {src}')
            shutil.copy2(src, os.path.join(dst, game_rel))
            origin[game_rel] = 'built ' + src_rel
        if scripts:
            checkout, plugin = scripts
            out = os.path.join(dst, 'red4ext', 'plugins', plugin, 'Scripts')
            shutil.rmtree(out)
            subprocess.run([sys.executable, '-B', os.path.join(HERE, 'compose_reds.py'), os.path.join(SRC, checkout),
                            out, '--name', plugin, '--version', f'{version}-cp2077-{a.game_version}'],
                           check=True, stdout=subprocess.DEVNULL)
            for k in [k for k in origin if k.startswith(f'red4ext/plugins/{plugin}/Scripts/')]:
                del origin[k]
            for n in os.listdir(out):
                origin[f'red4ext/plugins/{plugin}/Scripts/{n}'] = 'composed ' + checkout
        files = {}
        for d, _, ns in os.walk(dst):
            for n in ns:
                p = os.path.join(d, n)
                r = os.path.relpath(p, dst).replace('\\', '/')
                files[r] = dict(sha256=sha(p), size=os.path.getsize(p), origin=origin.get(r, '?'))
        zpath = os.path.join(a.out, f'{name}-{version}-cp2077-{a.game_version}.zip')
        with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
            for r in sorted(files):
                z.write(os.path.join(dst, r), r)
        manifest[name] = dict(version=version, release=rel, zip=os.path.basename(zpath), files=files)
        print(f'{name}: {len(files)} files, {sum(1 for f in files.values() if not f["origin"].startswith("release"))} '
              f'rebuilt -> {zpath}')
    for name, version, folder, rel, expected, edits in compat_mods.PATCHES:
        src, data = compat_mods.patched(folder, rel, expected, edits, sha)
        dst = os.path.join(a.out, name)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        os.makedirs(os.path.dirname(os.path.join(dst, rel)))
        with open(os.path.join(dst, rel), 'wb') as f:
            f.write(data)
        files = {rel: dict(sha256=sha(os.path.join(dst, rel)), size=len(data), origin='patched ' + src)}
        zpath = os.path.join(a.out, f'{name}-{version}-cp2077-{a.game_version}.zip')
        with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
            z.write(os.path.join(dst, rel), rel)
        manifest[name] = dict(version=version, release='compat ' + folder, zip=os.path.basename(zpath), files=files)
        print(f'{name}: compat patch of {rel} -> {zpath}')
    json.dump(manifest, open(os.path.join(a.out, 'manifest.json'), 'w', encoding='utf-8'), indent=1)


if __name__ == '__main__':
    main()
