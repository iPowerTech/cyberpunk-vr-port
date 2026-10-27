"""Lay the files assets_diff.py extracted by hash (<decimal hash>.bin) out under their real names, so WolvenKit
converts them: <out>/named/<depot path> when names.json knows the hash, else <out>/unnamed/<root class>/<hash>.<ext>
with the extension of the CR2W root class. Hard links, no copies.

    py -3.13 -B tools/patchday/assets_name.py --out build/patchday/assets-2.5
"""
import argparse
import json
import os
import struct
from collections import Counter

EXT = {
    'entEntityTemplate': 'ent', 'appearanceAppearanceResource': 'app', 'CMesh': 'mesh', 'CBitmapTexture': 'xbm',
    'animAnimSet': 'anims', 'questQuestPhaseResource': 'questphase', 'questQuestResource': 'quest',
    'scnSceneResource': 'scene', 'inkWidgetLibraryResource': 'inkwidget', 'CMaterialInstance': 'mi',
    'Multilayer_Setup': 'mlsetup', 'Multilayer_Mask': 'mlmask', 'inkTextureAtlas': 'inkatlas',
    'inkanimAnimationLibraryResource': 'inkanim', 'worldStreamingSector': 'streamingsector',
    'workWorkspotResource': 'workspot', 'MorphTargetMesh': 'morphtarget', 'inkStyleResource': 'inkstyle',
    'CMaterialTemplate': 'mt', 'worldEffect': 'effect', 'CParticleSystem': 'particle', 'CTextureArray': 'texarray',
    'envProbeResource': 'envprobe', 'CDynamicTexture': 'dtex', 'JsonResource': 'json', 'CGradient': 'gradient',
    'animRig': 'rig', 'animAnimGraph': 'animgraph', 'physicsMaterialLibraryResource': 'physmatlib',
    'C2dArray': 'csv', 'localizationPersistenceOnScreenEntries': 'json', 'CFoliageResource': 'cfoliage',
    'gameLocationResource': 'locationresource', 'CMeshParamColorChannel': 'mesh', 'audioCookedMetadataResource': 'audio',
    'gameJournalResource': 'journal', 'worldPrefab': 'prefab', 'CCubeTexture': 'cubemap',
    'CHairProfile': 'hp', 'CSkinProfile': 'skinprofile', 'entEntityParametersBuffer': 'ent',
}


def root_class(data):
    if data[:4] != b'CR2W' or len(data) < 0xA0:
        return None
    tables = [struct.unpack_from('<III', data, 0x28 + 12 * i) for i in range(10)]
    s_off, s_size, _ = tables[0]
    n_off, n_cnt, _ = tables[1]
    e_off, e_cnt, _ = tables[4]
    if not e_cnt or not n_cnt or e_off + 2 > len(data):
        return None
    cls_idx, = struct.unpack_from('<H', data, e_off)
    if cls_idx >= n_cnt:
        return None
    name_off, = struct.unpack_from('<I', data, n_off + 8 * cls_idx)
    end = data.find(b'\0', s_off + name_off)
    return data[s_off + name_off:end].decode('utf-8', 'replace')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    names = {int(k, 16): v for k, v in json.load(open(os.path.join(a.out, 'names.json'), encoding='utf-8')).items()}
    src = os.path.join(a.out, 'added')
    kinds = Counter()
    unknown_cls = Counter()
    for f in os.listdir(src):
        if not f.endswith('.bin'):
            continue
        h = int(f[:-4])
        p = os.path.join(src, f)
        data = open(p, 'rb').read(4 << 20)
        if h in names:
            dst = os.path.join(a.out, 'named', names[h])
            kinds['named'] += 1
        else:
            cls = root_class(data)
            ext = EXT.get(cls)
            if ext is None:
                unknown_cls[cls] += 1
                ext = 'cr2w' if cls else 'raw'
            dst = os.path.join(a.out, 'unnamed', cls or '_raw', f'{h:016x}.{ext}')
            kinds['unnamed'] += 1
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(dst):
            try:
                os.link(p, dst)
            except OSError:
                import shutil
                shutil.copy2(p, dst)
    print(dict(kinds))
    print('root classes without an extension:', unknown_cls.most_common(30))


if __name__ == '__main__':
    main()
