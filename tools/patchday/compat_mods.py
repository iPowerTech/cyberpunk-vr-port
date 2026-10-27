"""Compatibility patches for the user's OTHER mods that a new game build broke -- staged like the frameworks.

A failed redscript compile takes every script mod down, and in the game it waits on a message box until the
engine watchdog kills the process. So a third-party script that no longer compiles is patched here, as little as
the new build forces, from the mod's own file in Vortex's staging folder (checked by sha256, so an update of the
mod stops this instead of patching something else). stage_frameworks.py writes each patch as <out>/<Name>/ (game
layout) + a Vortex zip + a manifest entry, so test_install.py installs it the same way (the hardlink renamed, the
patched file copied in) and restore undoes it. In Vortex: install the zip, let it load after the original mod.
"""
import glob
import os

VORTEX_MODS = os.path.join(os.environ.get('APPDATA', ''), 'Vortex', 'cyberpunk2077', 'mods')

GSF_INSTANCED = '''\t\t\tcase n"worldInstancedMeshNode":
\t\t\t\tlet instancedMeshNode = node as worldInstancedMeshNode;
\t\t\t\tlet hash = ResourceAsyncRef.GetHash(instancedMeshNode.mesh);
\t\t\t\tif(!this.inst.generic.KeyExist(hash))
\t\t\t\t{
\t\t\t\t\tif(this.inst.raster.KeyExist(hash))
\t\t\t\t\t{
\t\t\t\t\t\tinstancedMeshNode.castLocalShadows = shadowsShadowCastingMode.Default;
\t\t\t\t\t}\t
\t\t\t\t\tinstancedMeshNode.castShadows = shadowsShadowCastingMode.Default;
\t\t\t\t}
\t\t\t\tbreak;
'''
GSF_INSTANCED_25 = '''\t\t\t// 2.5 compat patch: worldInstancedMeshNode is a cooked node now (worldCookedNode, only
\t\t\t// cookedWorldTransformsBuffer + version in RTTI): no mesh or shadow-casting fields left to set, and the
\t\t\t// 2.31 case no longer compiles. Instanced nodes are skipped, the other cases are unchanged.
'''

# name, version label, Vortex staging folder (glob), game-relative file, sha256 of the original, [(old, new)]
PATCHES = [
    ('GeneralShadowsFix-compat', '0.94', 'General Shadows Fix-20405-0-94-*',
     'r6/scripts/General Shadows Fix/GeneralShadowsFixes.reds',
     'de95bde4acd3bdc577964e56b88fef80ff5dcbdb782f52d4f7e7a6358cdb5b6a',
     [(GSF_INSTANCED, GSF_INSTANCED_25)]),
]


def patched(folder_glob, rel, expected_sha, edits, sha):
    """The patched file's bytes, from the mod's file in Vortex's staging folder."""
    hits = glob.glob(os.path.join(VORTEX_MODS, folder_glob))
    if len(hits) != 1:
        raise SystemExit(f'compat: {folder_glob} matches {len(hits)} folders under {VORTEX_MODS}')
    src = os.path.join(hits[0], rel)
    if sha(src) != expected_sha:
        raise SystemExit(f'compat: {src} is not the file the patch was made for (mod updated?)')
    text = open(src, 'rb').read().decode('utf-8')
    for old, new in edits:
        if text.count(old) != 1:
            raise SystemExit(f'compat: {rel}: the patched block occurs {text.count(old)} times')
        text = text.replace(old, new)
    return src, text.encode('utf-8')
