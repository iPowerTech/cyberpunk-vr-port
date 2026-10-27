"""Rebuild the port's shader twins for a new game build with their own generators.

    py -3.13 -B tools/patchday/regen_twins.py [--pairs build/patchday/shaders-2.5/pairs.json]
        [--out build/patchday/shaders-2.5/twins] [--only fog ao expo shadow ssr ham particles view_dir screen_fx]
    py -3.13 -B tools/patchday/regen_twins.py --promote      # the twins in --out become src/Shaders' sets

The hash-keyed generators (tools/asym_tests/asym_*_shaders.py, tools/hidden_area_tests/ham_shaders.py) name the
2.31 originals they patch in module constants (SHADERS, TRACES, FILL, PLANAR, AO_TRACES, LUMINANCE, ADAPTATION,
RECIPES). shader_pairs.py found each original's recompile; here every such constant is rewritten old -> new (one
old original may have several tied new candidates: all of them are offered, and the generator's own structural
checks take the ones its recipe fits) and the generator's main() runs on <pairs dir>/new. The pattern-based ones
(particle facing, view direction) scan every vertex / pixel shader of the new shader_final.cache, screen_fx reads
the cache itself. Output: <out>/<plugin folder>/<new fnv>.dxil, the folder names the plugin loads from.
"""
import argparse
import contextlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
ASYM = os.path.join(REPO, 'tools', 'asym_tests')
HAM = os.path.join(REPO, 'tools', 'hidden_area_tests')

# name: (module file, plugin folder, the module constants that hold original hashes, extra argv; {out} = the
# twins root). Order matters: hidden_area\ takes the cone trace from ao's --ham-out (both fixes in one twin),
# which overwrites the hidden-area-only one ham writes first; the fog overlay is ham's alone.
KEYED = {
    'fog': (os.path.join(ASYM, 'asym_fog_shaders.py'), 'asym', ('SHADERS',), []),
    'ham': (os.path.join(HAM, 'ham_shaders.py'), 'hidden_area', ('RECIPES',), []),
    'ao': (os.path.join(ASYM, 'asym_ao_shaders.py'), 'asym_ao', ('SHADERS', 'AO_TRACES'),
           ['--ham-out', os.path.join('{out}', 'hidden_area')]),
    'expo': (os.path.join(ASYM, 'asym_expo_shaders.py'), 'asym_expo', ('LUMINANCE', 'ADAPTATION'), []),
    'shadow': (os.path.join(ASYM, 'asym_shadow_shaders.py'), 'asym_shadow', ('SHADERS',), []),
    'ssr': (os.path.join(ASYM, 'asym_ssr_shaders.py'), 'asym_ssr', ('SHADERS', 'TRACES', 'FILL', 'PLANAR'), []),
}
# The new build's constant-buffer layout where it moved a slot a generator uses (module constant -> value),
# measured on the game. 2.5: the cone AO block grew 208 -> 240 bytes and its shaders read 12.y, so the merge's
# diagnostic mode moves to 13.z (Render/SsrWideMerge.hpp kAoModeRow).
# The SSR block (CB_SSRRaymarch) went 384 -> 656 bytes: the first-person plane's projection and rotation in rows
# 16-31, the rest moved down; the merge's own rows follow the engine's at 41-43 (Render/SsrWideMerge.hpp).
LAYOUT = {
    'ao': {'MODE_ROW': 13, 'MODE_COMP': 2},
    'ssr': {'TANGENT_ROW': 33, 'PIXEL_SCALE_ROW': 32, 'PIXEL_OFFSET': (36, 0),
            'CAMERA_ROW': 41, 'STORE_ROW': 42, 'MODE_ROW': 43, 'BLOCK_BYTES': 704},
}
SCANNED = {
    'particles': (os.path.join(ASYM, 'particle_facing_shaders.py'), 'asym_particles', 'vs'),
    'view_dir': (os.path.join(ASYM, 'view_dir_shaders.py'), 'view_dir', 'ps'),
}


def load(path):
    sys.path[:0] = [os.path.dirname(path), HAM]
    spec = importlib.util.spec_from_file_location(os.path.splitext(os.path.basename(path))[0] + '_regen', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def remap(value, table, name):
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            for n in table.get(k, []):
                out[n] = v
        return out
    if isinstance(value, (set, frozenset)):
        return {n for k in value for n in table.get(k, [])}
    if isinstance(value, int):
        news = table.get(value, [])
        if len(news) != 1:
            raise SystemExit(f'{name}: {value:016x} has {len(news)} new candidates, the generator takes one')
        return news[0]
    raise SystemExit(f'{name}: cannot remap a {type(value).__name__}')


def run_main(module, argv):
    old = sys.argv
    sys.argv = [module.__file__] + argv
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            code = module.main()
    finally:
        sys.argv = old
    return code, out.getvalue()


# twins folder -> where the repository keeps that set (src/Shaders, which scripts/deploy_stereo.ps1 and
# scripts/build_dist.ps1 install from); hidden_area\ is not shipped
REPO_SETS = {'asym': 'asym_fog', 'asym_ao': 'asym_ao', 'asym_expo': 'asym_expo', 'asym_shadow': 'asym_shadow',
             'asym_ssr': 'asym_ssr', 'asym_particles': 'asym_particles', 'view_dir': 'view_dir', 'screen_fx': 'screen_fx'}


def promote(out):
    """The rebuilt twins become the repository's (the old build's stay in git history): each set's .dxil files in
    src/Shaders/<set> are replaced, the scope-glass twins go to src/Shaders/scope_glass_<hash>.dxil."""
    shaders = os.path.join(REPO, 'src', 'Shaders')
    for folder, name in REPO_SETS.items():
        src, dst = os.path.join(out, folder), os.path.join(shaders, name)
        new = sorted(n for n in os.listdir(src) if n.endswith('.dxil')) if os.path.isdir(src) else []
        if not new:
            raise SystemExit(f'promote: no twins in {src}')
        os.makedirs(dst, exist_ok=True)
        old = [n for n in os.listdir(dst) if n.endswith('.dxil')]
        for n in old:
            os.remove(os.path.join(dst, n))
        for n in new:
            shutil.copyfile(os.path.join(src, n), os.path.join(dst, n))
        print(f'promote {folder} -> src/Shaders/{name}: {len(old)} -> {len(new)} twins')
    glass = os.path.join(out, 'scope_glass')
    old = [n for n in os.listdir(shaders) if n.startswith('scope_glass_') and n.endswith('.dxil')]
    new = sorted(n for n in os.listdir(glass) if n.startswith('scope_glass_') and n.endswith('.dxil'))
    if not new:
        raise SystemExit(f'promote: no scope-glass twins in {glass}')
    for n in old:
        os.remove(os.path.join(shaders, n))
    for n in new:
        shutil.copyfile(os.path.join(glass, n), os.path.join(shaders, n))
    print(f'promote scope_glass -> src/Shaders: {old} -> {new}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--pairs', default=os.path.join(REPO, 'build', 'patchday', 'shaders-2.5', 'pairs.json'))
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'shaders-2.5', 'twins'))
    ap.add_argument('--game', default=GAME)
    ap.add_argument('--only', nargs='*')
    ap.add_argument('--promote', action='store_true',
                    help='copy the twins in --out over the sets in src/Shaders (no rebuild)')
    a = ap.parse_args()
    if a.promote:
        promote(a.out)
        return
    base = os.path.dirname(a.pairs)
    pairs = json.load(open(a.pairs, encoding='utf-8'))
    table = {int(old, 16): [int(n, 16) for n in p['new']] for old, p in pairs.items()}
    for name, (path, folder, consts, extra) in KEYED.items():
        if a.only and name not in a.only:
            continue
        module = load(path)
        for c in consts:
            setattr(module, c, remap(getattr(module, c), table, f'{name}.{c}'))
        for c, v in LAYOUT.get(name, {}).items():
            if not hasattr(module, c):
                raise SystemExit(f'{name}: no module constant {c}')
            setattr(module, c, v)
        # the hidden-area recipes the AO generator layers on top (its own `ham` import)
        helper = getattr(module, 'ham', None)
        if helper is not None and hasattr(helper, 'RECIPES') and not getattr(helper, '_regen_remapped', False):
            helper.RECIPES = remap(helper.RECIPES, table, f'{name}.ham.RECIPES')
            helper._regen_remapped = True
        out = os.path.join(a.out, folder)
        os.makedirs(out, exist_ok=True)
        extra = [x.replace('{out}', a.out) for x in extra]
        for x in extra:
            if x.startswith(a.out):
                os.makedirs(x, exist_ok=True)
        code, text = run_main(module, ['--shaders', os.path.join(base, 'new'), '--out', out] + extra)
        print(f'== {name} -> {folder}: exit {code}')
        print('   ' + text.strip().replace('\n', '\n   '))
    for name, (path, folder, stage) in SCANNED.items():
        if a.only and name not in a.only:
            continue
        pool = os.path.join(base, 'all_' + stage)
        if not os.path.isdir(pool) or not os.listdir(pool):
            subprocess.run([sys.executable, '-B', os.path.join(ASYM, 'extract_shader_cache.py'), '--cache',
                            os.path.join(a.game, 'engine', 'shader_final.cache'), '--out', pool, '--kind', stage],
                           check=True)
        out = os.path.join(a.out, folder)
        os.makedirs(out, exist_ok=True)
        code, text = run_main(load(path), ['--shaders', pool, '--out', out])
        lines = text.strip().splitlines()
        print(f'== {name} -> {folder}: exit {code}, {len(os.listdir(out))} twins')
        print('   ' + '\n   '.join([l for l in lines if not l.startswith('OK')][-25:]))
    if not a.only or 'screen_fx' in a.only:
        out = os.path.join(a.out, 'screen_fx')
        os.makedirs(out, exist_ok=True)
        # its techniques are static ones: the table at the start of staticshader_final.cache
        r = subprocess.run([sys.executable, '-B', os.path.join(ASYM, 'screen_fx_shaders.py'), '--cache',
                            os.path.join(a.game, 'engine', 'staticshader_final.cache'), '--out', out],
                           capture_output=True, text=True)
        print(f'== screen_fx: exit {r.returncode}')
        print('   ' + (r.stdout + r.stderr).strip().replace('\n', '\n   '))


if __name__ == '__main__':
    main()
