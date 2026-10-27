"""Do the port's shader twins still have their originals in the game's shader cache?

    py -3.13 -B tools/patchday/check_shaders.py index [--game <old dir>]     # once, on the old build
    py -3.13 -B tools/patchday/check_shaders.py check [--game <new dir>]     # on patch day

A twin replaces one game shader and is found by the FNV-1a 64 of that shader's bytecode
(Render/HiddenAreaShaders.cpp; the file name is that hash). A patch that recompiles a shader leaves
its twin unused -- no crash, the VR fix behind it (asymmetric fog, SSR, AO, particle facing, view
direction, exposure, shadow mask, screen effects, scope glass) silently off.

`index` maps every twin to the DXBC digest of its original in the old engine\\shader_final.cache and
staticshader_final.cache (the 16 bytes the container carries at offset 4). `check` reads only the
digests of the new caches -- seconds -- and lists per twin folder which originals are gone, with the
generator that rebuilds that folder from the new cache.
"""
import argparse
import json
import mmap
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
BASE = os.path.join(REPO, 'build', 'patchday', 'base-2.31')
INDEX = os.path.join(HERE, 'data', 'shader_twins-2.31.json')
CACHES = ('shader_final.cache', 'staticshader_final.cache')
PLUGIN = os.path.join('red4ext', 'plugins', 'CyberpunkVR_Stereo')
# Folder -> what rebuilds it from a shader cache (tools/asym_tests unless stated).
GENERATORS = {
    'asym': 'tools/asym_tests/asym_fog_shaders.py',
    'asym_ssr': 'tools/asym_tests/asym_ssr_shaders.py',
    'asym_ao': 'tools/asym_tests/asym_ao_shaders.py',
    'asym_expo': 'tools/asym_tests/asym_expo_shaders.py',
    'asym_shadow': 'tools/asym_tests/asym_shadow_shaders.py',
    'asym_particles': 'tools/asym_tests/particle_facing_shaders.py',
    'view_dir': 'tools/asym_tests/view_dir_shaders.py',
    'screen_fx': 'tools/asym_tests/screen_fx_shaders.py',
    'hidden_area': 'tools/hidden_area_tests (ham_shaders.py)',
    'scope_glass': 'src/Shaders/scope_glass_<hash>.dxil (Render/PhysicalOpticsShaders.cpp)',
}


def fnv1a(data):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def containers(path, want_fnv=None):
    """(offset, size, digest[, fnv]) of every plausible DXBC container in a cache file. The FNV is
    computed only when want_fnv is given (it costs a pass over every byte)."""
    out = []
    with open(path, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        pos = 0
        n = mm.size()
        while True:
            i = mm.find(b'DXBC', pos)
            if i < 0:
                break
            size, = struct.unpack_from('<I', mm, i + 24)
            if 32 < size < 16 << 20 and i + size <= n:
                digest = bytes(mm[i + 4:i + 20]).hex()
                row = [i, size, digest]
                if want_fnv is not None:
                    row.append(fnv1a(mm[i:i + size]))
                out.append(row)
                pos = i + size
                continue
            pos = i + 4
    return out


def twins(plugin_dir):
    """{fnv: [folder, ...]} of every twin deployed (and the repo's scope-glass twins)."""
    out = {}
    for name in sorted(os.listdir(plugin_dir)):
        full = os.path.join(plugin_dir, name)
        if os.path.isdir(full) and (name.startswith('asym') or name in ('view_dir', 'screen_fx', 'hidden_area')):
            for f in os.listdir(full):
                if f.endswith('.dxil') and len(f) == 21:
                    out.setdefault(int(f[:16], 16), []).append(name)
        elif name.startswith('CyberpunkVR_ScopeGlass_') and name.endswith('.dxil'):
            out.setdefault(int(name[len('CyberpunkVR_ScopeGlass_'):-5], 16), []).append('scope_glass')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('index', 'check'))
    ap.add_argument('--game', default=None)
    ap.add_argument('--plugin', default=os.path.join(GAME, PLUGIN), help='deployed plugin folder with the twins')
    ap.add_argument('--index', default=INDEX)
    args = ap.parse_args()
    if args.cmd == 'index':
        game = args.game or BASE
        tw = twins(args.plugin)
        found = {}
        for cache in CACHES:
            for off, size, digest, h in containers(os.path.join(game, 'engine', cache), want_fnv=True):
                if h in tw and h not in found:
                    found[h] = dict(cache=cache, size=size, digest=digest, folders=tw[h])
        missing = sorted(set(tw) - set(found))
        os.makedirs(os.path.dirname(args.index), exist_ok=True)
        with open(args.index, 'w', encoding='utf-8') as f:
            json.dump(dict(game=game, twins={f'{k:016x}': v for k, v in sorted(found.items())},
                           not_in_cache=[f'{k:016x}' for k in missing]), f, indent=1)
        print(f'{len(tw)} twins, {len(found)} originals found in the cache, {len(missing)} not -> {args.index}')
        for k in missing:
            print(f'  not in cache: {k:016x} ({", ".join(tw[k])})')
        return
    game = args.game or GAME
    with open(args.index, encoding='utf-8') as f:
        idx = json.load(f)
    digests = set()
    for cache in CACHES:
        p = os.path.join(game, 'engine', cache)
        if os.path.exists(p):
            digests |= {row[2] for row in containers(p)}
    per = {}
    for h, t in idx['twins'].items():
        for folder in t['folders']:
            ok, gone = per.setdefault(folder, [0, []])
            if t['digest'] in digests:
                per[folder][0] += 1
            else:
                gone.append(h)
    bad = 0
    print(f'{len(digests)} shaders in the new caches')
    for folder, (ok, gone) in sorted(per.items()):
        state = 'OK' if not gone else f'{len(gone)} ORIGINALS CHANGED -> rebuild with {GENERATORS.get(folder, "?")}'
        print(f'  {folder:15s} {ok:4d} unchanged  {state}')
        bad += len(gone)
    print('SHADERS', 'OK' if not bad else f'{bad} twins orphaned')
    sys.exit(0 if not bad else 1)


if __name__ == '__main__':
    main()
