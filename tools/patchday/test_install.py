"""A test install of the staged frameworks and the port for a new game build -- and its exact undo.

    py -3.13 -B tools/patchday/test_install.py install [--stage build/patchday/stage-2.5] [--port-dll <dir>]
    py -3.13 -B tools/patchday/test_install.py restore
    py -3.13 -B tools/patchday/test_install.py status
    py -3.13 -B tools/patchday/test_install.py deploy-port      # the port's own files for good, no frameworks

The installed frameworks are Vortex hardlinks: writing through one changes Vortex's own staging copy. So a game
file a staged one replaces is RENAMED to <name>.cvr25bak (the link and Vortex's copy untouched) and the staged file
is copied in as a new file; restore deletes the copy and renames the original back, and checks it is the same file
as before (size, sha256, link count). The port's own files (plugin DLL/PDB, the archive, the redscript and CET files
the 2.5 port changed) are not Vortex's: those are copied over, the previous ones kept under
build/patchday/test-install/<stamp>/ and put back by restore. Everything is recorded in
build/patchday/test-install/journal.json, so restore works from a fresh shell. The game must be closed.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
WORK = os.path.join(REPO, 'build', 'patchday', 'test-install')
JOURNAL = os.path.join(WORK, 'journal.json')
BAK = '.cvr25bak'
# The port's files a 2.5 test needs, game-relative -> repo source. User state (vrcam.json, quickboot.json,
# db.sqlite3, ini files) is deliberately not in this list.
PORT_FILES = {
    'archive/pc/mod/cyberpunkvrport.archive': 'mods/archive/cyberpunkvrport.archive',
    'bin/x64/plugins/cyber_engine_tweaks/mods/CyberpunkVRPort_Menu/modules/cyberware_data.lua':
        'mods/cet/CyberpunkVRPort_Menu/modules/cyberware_data.lua',
    'bin/x64/plugins/cyber_engine_tweaks/mods/CyberpunkVRPort_Menu/modules/trade_data.lua':
        'mods/cet/CyberpunkVRPort_Menu/modules/trade_data.lua',
    'bin/x64/plugins/cyber_engine_tweaks/mods/CyberpunkVRPort_Reload/reload/reload.lua':
        'mods/cet/CyberpunkVRPort_Reload/reload/reload.lua',
    # the launch tools' save resume: 2.5's first-launch account pitch comes before the single-player menu
    'bin/x64/plugins/cyber_engine_tweaks/mods/CyberpunkVRPort_QuickBoot/init.lua':
        'mods/cet/CyberpunkVRPort_QuickBoot/init.lua',
}
PORT_REDS = ['CyberpunkVRPort_Cyberware', 'CyberpunkVRPort_EngineStereo', 'CyberpunkVRPort_NoAnims',
             'CyberpunkVRPort_Optics', 'CyberpunkVRPort_WorldMap']


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def links(p):
    return os.stat(p).st_nlink


def game_running():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Cyberpunk2077.exe'], capture_output=True, text=True).stdout
    return 'Cyberpunk2077.exe' in out


def port_files(a):
    """Game-relative path -> repo-relative source of every port file the new build needs."""
    files = dict(PORT_FILES)
    for mod in PORT_REDS:
        for d, _, ns in os.walk(os.path.join(REPO, 'mods', 'redscript', mod)):
            for n in ns:
                p = os.path.join(d, n)
                files['r6/scripts/' + os.path.relpath(p, os.path.join(REPO, 'mods', 'redscript')).replace('\\', '/')] = \
                    os.path.relpath(p, REPO)
    dll = a.port_dll
    for n in ('CyberpunkVR_Stereo.dll', 'CyberpunkVR_Stereo.pdb'):
        files['red4ext/plugins/CyberpunkVR_Stereo/' + n] = os.path.relpath(os.path.join(dll, n), REPO)
    # the shader twins rebuilt for the new build (tools/patchday/regen_twins.py): <folder>/<fnv>.dxil beside the
    # plugin, the scope-glass ones under the plugin's own names
    if a.twins and os.path.isdir(a.twins):
        for folder in sorted(os.listdir(a.twins)):
            full = os.path.join(a.twins, folder)
            for n in sorted(os.listdir(full)) if os.path.isdir(full) else []:
                if not n.endswith('.dxil'):
                    continue
                name = 'CyberpunkVR_ScopeGlass_' + n[len('scope_glass_'):] if folder == 'scope_glass' else folder + '/' + n
                files['red4ext/plugins/CyberpunkVR_Stereo/' + name] = os.path.relpath(os.path.join(full, n), REPO)
    return files


def deploy_port(a):
    """The port's own files for the new build, for good (the frameworks stay Vortex's: their zips are in the
    stage). What each file replaces, and the old build's twins (keyed by its shader hashes, so dead on the new
    one, but still counted as loaded), go to build/patchday/deploy/<stamp>/ with a manifest."""
    if os.path.exists(JOURNAL):
        sys.exit('a test install is active: restore first')
    if game_running():
        sys.exit('close the game first')
    stamp = time.strftime('%Y%m%d-%H%M%S')
    keep = os.path.join(REPO, 'build', 'patchday', 'deploy', stamp)
    files = port_files(a)
    rows, stale = [], []
    plugin = os.path.join(GAME, 'red4ext', 'plugins', 'CyberpunkVR_Stereo')
    folders = {rel.split('/')[3] for rel in files if rel.count('/') == 4 and rel.endswith('.dxil')}
    for folder in sorted(folders):
        d = os.path.join(plugin, folder)
        wanted = {rel.split('/')[4] for rel in files if rel.startswith('red4ext/plugins/CyberpunkVR_Stereo/%s/' % folder)}
        for n in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if n.endswith('.dxil') and n not in wanted:
                stale.append('red4ext/plugins/CyberpunkVR_Stereo/%s/%s' % (folder, n))
    for n in sorted(os.listdir(plugin)):
        rel = 'red4ext/plugins/CyberpunkVR_Stereo/' + n
        if n.startswith('CyberpunkVR_ScopeGlass_') and n.endswith('.dxil') and rel not in files:
            stale.append(rel)
    for rel in stale:
        k = os.path.join(keep, rel)
        os.makedirs(os.path.dirname(k), exist_ok=True)
        shutil.move(os.path.join(GAME, rel), k)
    for rel, src_rel in files.items():
        src, dst = os.path.join(REPO, src_rel), os.path.join(GAME, rel)
        if os.path.exists(dst) and sha(dst) == sha(src):
            continue
        row = dict(rel=rel, source=src_rel, new=sha(src), previous=None)
        if os.path.exists(dst):
            if links(dst) > 1:
                sys.exit(f'{dst} is a hardlink: not a port file, refusing to copy over it')
            k = os.path.join(keep, rel)
            os.makedirs(os.path.dirname(k), exist_ok=True)
            shutil.copy2(dst, k)
            row['previous'] = dict(sha256=sha(dst), kept=os.path.relpath(k, REPO))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        assert sha(dst) == row['new'], dst
        rows.append(row)
    os.makedirs(keep, exist_ok=True)
    json.dump(dict(stamp=stamp, port=rows, stale=stale), open(os.path.join(keep, 'deploy.json'), 'w', encoding='utf-8'),
              indent=1)
    print(f'deployed {len(rows)} port files ({len(files) - len(rows)} already current), moved {len(stale)} stale '
          f'twins; kept under {os.path.relpath(keep, REPO)}')


def install(a):
    if os.path.exists(JOURNAL):
        sys.exit('a test install is active (journal exists): restore first')
    if game_running():
        sys.exit('close the game first')
    stamp = time.strftime('%Y%m%d-%H%M%S')
    keep = os.path.join(WORK, stamp)
    os.makedirs(keep, exist_ok=True)
    journal = dict(stamp=stamp, frameworks=[], port=[])
    manifest = json.load(open(os.path.join(a.stage, 'manifest.json'), encoding='utf-8'))
    for name, m in manifest.items():
        for rel, info in m['files'].items():
            src = os.path.join(a.stage, name, rel)
            dst = os.path.join(GAME, rel)
            row = dict(framework=name, rel=rel, staged=info['sha256'], original=None)
            if os.path.exists(dst):
                if sha(dst) == info['sha256']:
                    continue        # already the staged file
                if os.path.exists(dst + BAK):
                    sys.exit(f'{dst + BAK} exists already: a previous test was not restored')
                row['original'] = dict(sha256=sha(dst), size=os.path.getsize(dst), links=links(dst))
                os.rename(dst, dst + BAK)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            assert sha(dst) == info['sha256'], dst
            journal['frameworks'].append(row)
            json.dump(journal, open(JOURNAL, 'w', encoding='utf-8'), indent=1)
    files = port_files(a)
    for rel, src_rel in files.items():
        src, dst = os.path.join(REPO, src_rel), os.path.join(GAME, rel)
        if os.path.exists(dst) and sha(dst) == sha(src):
            continue
        row = dict(rel=rel, source=src_rel, new=sha(src), previous=None)
        if os.path.exists(dst):
            if links(dst) > 1:
                sys.exit(f'{dst} is a hardlink: not a port file, refusing to copy over it')
            k = os.path.join(keep, rel)
            os.makedirs(os.path.dirname(k), exist_ok=True)
            shutil.copy2(dst, k)
            row['previous'] = dict(sha256=sha(dst), kept=os.path.relpath(k, REPO))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        journal['port'].append(row)
        json.dump(journal, open(JOURNAL, 'w', encoding='utf-8'), indent=1)
    print(f'installed: {len(journal["frameworks"])} framework files, {len(journal["port"])} port files; '
          f'journal {JOURNAL}')


def restore(a):
    if not os.path.exists(JOURNAL):
        sys.exit('no test install recorded')
    if game_running():
        sys.exit('close the game first')
    journal = json.load(open(JOURNAL, encoding='utf-8'))
    problems = 0
    for row in reversed(journal['port']):
        dst = os.path.join(GAME, row['rel'])
        if row['previous']:
            shutil.copy2(os.path.join(REPO, row['previous']['kept']), dst)
            if sha(dst) != row['previous']['sha256']:
                print('MISMATCH after restore', dst)
                problems += 1
        elif os.path.exists(dst):
            os.remove(dst)
    for row in reversed(journal['frameworks']):
        dst = os.path.join(GAME, row['rel'])
        if os.path.exists(dst):
            if sha(dst) != row['staged']:
                print('changed during the test (kept as .cvr25test):', dst)
                os.replace(dst, dst + '.cvr25test')
            else:
                os.remove(dst)
        o = row['original']
        if o:
            os.rename(dst + BAK, dst)
            if sha(dst) != o['sha256'] or os.path.getsize(dst) != o['size'] or links(dst) != o['links']:
                print('ORIGINAL DIFFERS after restore', dst)
                problems += 1
    os.replace(JOURNAL, os.path.join(WORK, journal['stamp'], 'journal.json'))
    print('restored', len(journal['frameworks']), 'framework files and', len(journal['port']), 'port files;',
          'problems:', problems)


def status(a):
    if not os.path.exists(JOURNAL):
        print('no test install active')
        return
    j = json.load(open(JOURNAL, encoding='utf-8'))
    print('active since', j['stamp'], len(j['frameworks']), 'framework files,', len(j['port']), 'port files')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['install', 'restore', 'status', 'deploy-port'])
    ap.add_argument('--stage', default=os.path.join(REPO, 'build', 'patchday', 'stage-2.5'))
    ap.add_argument('--port-dll', default=os.path.join(REPO, 'build', 'patchday', 'sdk25-build', 'bin', 'red4ext',
                                                        'plugins', 'CyberpunkVR_Stereo', 'Release'))
    ap.add_argument('--twins', default=os.path.join(REPO, 'build', 'patchday', 'shaders-2.5', 'twins'),
                    help='shader twins rebuilt for the new build (regen_twins.py); "" for none')
    a = ap.parse_args()
    {'install': install, 'restore': restore, 'status': status, 'deploy-port': deploy_port}[a.cmd](a)


if __name__ == '__main__':
    main()
