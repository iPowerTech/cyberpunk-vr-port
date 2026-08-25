# -*- coding: utf-8 -*-
"""Give an entity template the appearance list an ITEM's root entity is required to have.

ArchiveXL resolves an item in two steps, and the second one is easy to miss: `entityName` finds the
root `.ent` through the factory csv, and then the root entity is used as -- the wiki's words -- "a
glorified lookup dictionary: for any appearanceName, it will specify an .app file and the name of an
appearance in the .app file". An `.ent` with an EMPTY appearances list resolves to nothing, and the
symptom is silent: `AddItemToSlot` returns true and the slot stays empty.

Measured on the way to this (2026-08-16): the root class has to be `gameItemObject` as well (see
make_item_entity.py), but that alone was not enough -- with the class fixed and no appearances, both
of the port's factory items still refused to attach while the vanilla ones went in.

Usage:
  python tools/ent/add_entity_appearance.py <entity.ent.json> <appearanceName> <app path> [name-in-app]

`name-in-app` defaults to the same name. Idempotent: an entry with that name is left alone.
"""
import io
import json
import os
import sys


def cname(value):
    return {'$type': 'CName', '$storage': 'string', '$value': value}


def main(path, name, app, in_app):
    doc = json.load(io.open(path, encoding='utf-8'))
    root = doc['Data']['RootChunk']
    apps = root.setdefault('appearances', [])
    for a in apps:
        n = a.get('name')
        if (n.get('$value') if isinstance(n, dict) else n) == name:
            return 'appearance %r already there' % name
    apps.append({
        '$type': 'entTemplateAppearance',
        'appearanceName': cname(in_app),
        'appearanceResource': {
            'DepotPath': {'$type': 'ResourcePath', '$storage': 'string', '$value': app},
            'Flags': 'Default',
        },
        'name': cname(name),
    })
    with io.open(path, 'w', encoding='utf-8') as handle:
        json.dump(doc, handle, indent=1, ensure_ascii=False)
    return 'added %r -> %s (%s), now %d appearance(s)' % (name, os.path.basename(app), in_app, len(apps))


print(main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else sys.argv[2]))
