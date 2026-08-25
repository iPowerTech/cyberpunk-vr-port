# -*- coding: utf-8 -*-
"""Drop components from an entity template by class, from both lists that hold them.

WHY. The port's magazine entities were authored with the geometry TWICE: the skinned set copied from
the weapon's own appearance, and a plain `entMeshComponent` set the reload module drives by name
(`applySkin` sets `meshAppearance` on `mag_mesh`, and that was written to fix a VISIBLE defect -- a
legendary weapon whose magazine was the wrong colour in the hand -- so the plain set is the one that
draws). Carried as a real ITEM the doubling shows up in the count: 14 mesh components on a
seven-piece speedloader.

BOTH LISTS, as always with a cooked template: `RootChunk.components` and
`RootChunk.compiledData.Data.Chunks` mirror each other, and editing one is how a change quietly
does not happen.

Usage:  python tools/ent/drop_components.py <entity.ent.json> <class> [name-substring]
"""
import io
import json
import os
import sys


def named(c):
    n = c.get('name')
    return (n.get('$value') if isinstance(n, dict) else n) or ''


def main(path, cls, needle):
    doc = json.load(io.open(path, encoding='utf-8'))
    root = doc['Data']['RootChunk']

    def drop(seq):
        keep, gone = [], []
        for c in seq:
            hit = c.get('$type') == cls and (not needle or needle in named(c))
            (gone if hit else keep).append(c)
        return keep, gone

    total = 0
    comps = root.get('components')
    if isinstance(comps, list):
        comps[:], gone = drop(comps)
        total += len(gone)
    chunks = (root.get('compiledData') or {}).get('Data', {}).get('Chunks')
    if isinstance(chunks, list):
        chunks[:], gone2 = drop(chunks)
        total += len(gone2)
        for c in gone2:
            print('    dropped %s' % named(c))
    if not total:
        return 'nothing matched %s%s' % (cls, (' / ' + needle) if needle else '')
    with io.open(path, 'w', encoding='utf-8') as handle:
        json.dump(doc, handle, indent=1, ensure_ascii=False)
    return 'removed %d entr(y/ies) across both lists' % total


print('%-34s %s' % (os.path.basename(sys.argv[1]),
                    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)))
