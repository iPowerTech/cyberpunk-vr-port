# -*- coding: utf-8 -*-
"""Give every plain mesh component of a prop a TWIN that carries the lit appearance.

WHY, in the user's words: "это не обводка а она как бы прозрачная становится граната и чутка есть
заливки как в solid".

Swapping `meshAppearance` on a component REPLACES its material. With the fresnel material that means
the grenade loses its own surface and turns into a dark, half-transparent shape with a little glow --
which is not an outline, it is the grenade being replaced by the effect.

An outline is the object PLUS an edge. Two materials cannot live on one chunk, so the object and the
edge have to be two components drawing the same geometry in the same place:

    the original component   `default`, always on   -- the grenade, exactly as the game made it
    the twin                 `lit*`, toggled        -- the fresnel edge, over the top

The twin ships DISABLED, so nothing changes until the belt turns it on.

`visualScale` above 1.0 on the twin, not 1.0: two components drawing identical geometry at identical
depth z-fight, and the margin is what puts the edge outside the surface it is outlining -- so it is
also the thickness of the outline. 1.05 by default; the belt writes it live from CFG.prop_rim_scale,
so it can be tuned with the prop in front of you instead of rebuilt.

Usage:  python tools/ent/add_lit_twins.py <entity.ent.json> [more.json ...]
Idempotent: components already named vrp_lit_* are left alone and not twinned again.
"""
import copy
import io
import json
import os
import sys

V = "$value"
PREFIX = "vrp_lit_"


def val(x):
    return x.get(V, x) if isinstance(x, dict) else x


def main(paths):
    for path in paths:
        doc = json.load(io.open(path, encoding="utf-8"))
        root = doc["Data"]["RootChunk"]
        comps = root.setdefault("components", [])
        have = set(val(c.get("name")) for c in comps)
        used = set(str(c.get("id")) for c in comps)

        made = 0
        for c in list(comps):
            if c.get("$type") != "entMeshComponent":
                continue
            name = val(c.get("name")) or ""
            if name.startswith(PREFIX):
                continue
            twin_name = PREFIX + str(made)
            while twin_name in have:
                made += 1
                twin_name = PREFIX + str(made)
            twin = copy.deepcopy(c)
            twin["name"] = {"$type": "CName", "$storage": "string", "$value": twin_name}
            twin["meshAppearance"] = {"$type": "CName", "$storage": "string", "$value": "lit"}
            twin["isEnabled"] = 0
            twin["visualScale"] = {"$type": "Vector3", "X": 1.05, "Y": 1.05, "Z": 1.05}
            n = 2535843905216263000
            while str(n) in used:
                n += 1
            twin["id"] = str(n)
            used.add(str(n))
            have.add(twin_name)
            comps.append(twin)
            made += 1

        io.open(path, "w", encoding="utf-8", newline="").write(json.dumps(doc, indent=2))
        print("%-34s twins: %d" % (os.path.basename(path), made))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
