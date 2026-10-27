# -*- coding: utf-8 -*-
"""Give a belt prop its own light, so it can be marked WITHOUT the engine's focus outline.

WHY. The engine outline only reaches an object on ERenderingPlane.RPl_Scene -- that is why the belt
attaches its props with the scene plane -- and a scene-plane object at the hip is in the world: it
sinks into the thigh, it is occluded by clothing, it takes world lighting. The first-person plane
draws over the body the way the gun and the hands do, which is what a thing hanging on your belt in VR
should look like, but nothing on that plane can be outlined.

A light does not care about the plane. Toggle it on when the hand comes near and the prop reads as
picked out, on either plane, with no HUD claim, no re-claim timer, no transition to restart and no
glitch intro effect to tune away.

WHAT IS ADDED. One `entLightComponent` named `vrp_glow`, copied VERBATIM from the frag grenade's own
`fx_light` (a real light in an entity of exactly this kind, so nothing here is invented), with five
values changed and the rest left as the game authored them:

    isEnabled   0        ships OFF -- the belt turns it on by proximity, nothing lights up on its own
    intensity   60       lumens; the panel writes this live, so it is a starting point, not a result
    radius      0.35     a bubble around the prop, not a lamp in the room
    color       white    the port's own marker colour, the one the game never picks for loot
    scaleVolFog 0        no fog bloom around the hip

The transform binding is taken from a plain mesh component IN THE SAME FILE. `HandleRefId` is a
per-file index -- handle 6 is a transform binding in one entity and something else entirely in the
next -- so borrowing one across files fails the import exactly as loudly as a wrong type does.

Usage:  python tools/ent/add_prop_light.py <entity.ent.json> [more.json ...]
Idempotent: a file that already carries `vrp_glow` is left alone.
"""
import io
import json
import os
import sys

TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_light_component.json")
NAME = "vrp_glow"


def val(x):
    return x.get("$value", x) if isinstance(x, dict) else x


def main(paths):
    light = json.load(io.open(TEMPLATE, encoding="utf-8"))
    for path in paths:
        doc = json.load(io.open(path, encoding="utf-8"))
        root = doc["Data"]["RootChunk"]
        comps = root.setdefault("components", [])
        if any(val(c.get("name")) == NAME for c in comps):
            print("%-34s vrp_glow already there" % os.path.basename(path))
            continue

        # A binding that is known good in THIS file: the plain mesh components were given the skinned
        # ones' transform binding, and those are the components that demonstrably draw.
        parent = None
        for c in comps:
            if c.get("$type") == "entMeshComponent" and c.get("parentTransform"):
                parent = c["parentTransform"]
                break
        if parent is None:
            for c in comps:
                if c.get("parentTransform"):
                    parent = c["parentTransform"]
                    break
        if parent is None:
            print("%-34s no transform binding to borrow -- skipped" % os.path.basename(path))
            continue

        glow = json.loads(json.dumps(light))
        glow["name"] = {"$type": "CName", "$storage": "string", "$value": NAME}
        glow["parentTransform"] = parent
        glow["isEnabled"] = 0
        glow["intensity"] = 60
        glow["radius"] = 0.34999999
        glow["color"] = {"$type": "Color", "Alpha": 255, "Blue": 255, "Green": 255, "Red": 255}
        glow["scaleVolFog"] = 0
        glow["useInFog"] = 0
        used = set(str(c.get("id")) for c in comps)
        n = 2202727315882827900
        while str(n) in used:
            n += 1
        glow["id"] = str(n)
        comps.append(glow)
        io.open(path, "w", encoding="utf-8", newline="").write(json.dumps(doc, indent=2))
        print("%-34s vrp_glow added (off, %d lm, %.2f m)" % (os.path.basename(path), 60, 0.35))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
