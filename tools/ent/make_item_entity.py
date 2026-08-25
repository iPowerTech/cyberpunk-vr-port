# -*- coding: utf-8 -*-
"""Turn a plain entity template into one the game will carry as an ITEM.

WHY. `TransactionSystem.AddItemToSlot` returns TRUE for an item whose entity is a bare `entEntity`
and attaches nothing -- no error, no log line, an empty slot. Measured 2026-08-16 across four items:

    Items.cigarette_i_stick   root chunk gameItemObject   attaches
    Items.apparel_lighter_a   root chunk gameItemObject   attaches
    Items.VRSmokeWheel        root chunk entEntity        does NOT
    Items.VRPortLoadOverture  root chunk entEntity        does NOT

The item record, the ArchiveXL factory entry and the call site were identical in every case; the
class of the entity's root chunk is the whole difference. An item's entity has to BE an item object.

The rest of the vanilla root's fields are left to the serializer's defaults -- it fills what a
`gameItemObject` needs, and the authored values on the vanilla one (lootQuality Common, every flag
zero) are the defaults anyway.

Usage:  python tools/ent/make_item_entity.py <entity.ent.json> [more.json ...]
Idempotent: an entity that is already an item object is left alone.
"""
import io
import json
import os
import sys

FROM = 'entEntity'
TO = 'gameItemObject'


def patch(path):
    doc = json.load(io.open(path, encoding='utf-8'))
    root = doc['Data']['RootChunk']

    # TWO PLACES, AND THE FIRST ONE ALONE IS WHY THE FIRST ATTEMPT LOOKED LIKE THE TOOL COULD NOT DO
    # IT. The template carries the root entity twice: as `entity`, which is the authored handle, and
    # again as chunk 0 of the cooked package. Changing only the chunk came back byte-identical -- the
    # writer rebuilds the package from the handle, so the edit was silently undone. Both, or neither.
    targets = []
    ent = (root.get('entity') or {}).get('Data')
    if isinstance(ent, dict):
        targets.append(('entity', ent))
    chunks = (root.get('compiledData') or {}).get('Data', {}).get('Chunks')
    if chunks:
        targets.append(('compiledData chunk 0', chunks[0]))
    if not targets:
        return 'no root entity found'

    done = []
    for label, node in targets:
        was = node.get('$type')
        if was == TO:
            done.append('%s already' % label)
        elif was == FROM:
            node['$type'] = TO
            done.append('%s patched' % label)
        else:
            return '%s is %s -- not touching it' % (label, was)
    if all(d.endswith('already') for d in done):
        return 'already an item object'
    with io.open(path, 'w', encoding='utf-8') as handle:
        json.dump(doc, handle, indent=1, ensure_ascii=False)
    return '%s -> %s  (%s)' % (FROM, TO, ', '.join(done))


for target in sys.argv[1:]:
    print('%-34s %s' % (os.path.basename(target), patch(target)))
