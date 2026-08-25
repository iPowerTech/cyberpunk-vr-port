# -*- coding: utf-8 -*-
"""Switch components off in an entity template, in both lists that hold them.

WHY OFF AND NOT OUT. Removing a chunk from a cooked package breaks it: the package indexes its
chunks by position (CruidDict, handle references), and dropping seven of them made the writer throw
`Index was out of range`. A disabled component keeps every index intact and does not draw, which is
the whole of what was wanted.

WHAT IT IS FOR HERE. The port's magazine entities carry the geometry TWICE -- the skinned set copied
from the weapon's own appearance, and a plain `entMeshComponent` set the reload module drives by name
(`applySkin` writes `meshAppearance` on `mag_mesh`, and that was written to fix a defect that was
VISIBLE, so the plain set is the one that draws). Carried as a real item the doubling shows in the
count: fourteen mesh components on a seven-piece speedloader.

BOTH LISTS, as always with a cooked template: `RootChunk.components` and
`RootChunk.compiledData.Data.Chunks` mirror each other, and editing one is how a change quietly does
not happen.

Usage:  python tools/ent/disable_components.py <entity.ent.json> <class> [name-substring]
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
    seen = []

    def switch(seq, report):
        n = 0
        for c in seq or []:
            if c.get('$type') == cls and (not needle or needle in named(c)):
                if c.get('isEnabled') != 0:
                    c['isEnabled'] = 0
                    n += 1
                    if report:
                        seen.append(named(c))
        return n

    total = switch(root.get('components'), False)
    total += switch((root.get('compiledData') or {}).get('Data', {}).get('Chunks'), True)
    if not total:
        return 'nothing matched %s%s' % (cls, (' / ' + needle) if needle else '')
    for s in seen:
        print('    disabled %s' % s)
    with io.open(path, 'w', encoding='utf-8') as handle:
        json.dump(doc, handle, indent=1, ensure_ascii=False)
    return 'switched off %d entr(y/ies) across both lists' % total


print('%-34s %s' % (os.path.basename(sys.argv[1]),
                    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)))
