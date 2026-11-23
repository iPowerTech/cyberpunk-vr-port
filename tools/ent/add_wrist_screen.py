# -*- coding: utf-8 -*-
"""Add the WRIST SCREEN to a player entity template: the VR menu's screen (vrport\\ui\\vr_menu.ent), as two
components of the player, for the dialogue's answers on the left wrist (mods/cet/CyberpunkVRPort_Menu,
modules/dialog_wrist.lua).

WHY. A spawned entity placed by script reaches the screen a frame behind the player (tools/ent/add_hold_components.py:
21 cm at a run, measured 2026-08-16); the wrist's panel, a spawned vr_menu.ent, hopped after a walking player by
50-80 mm a frame (2026-11-22, the wrist's own route measured smooth). A component of the player is drawn where the
player is in that same frame; its local transform (the wrist, the eyes, in the player's model space) is set from
script and lags only by what the hand and the head move against the body in a frame.

WHAT IT ADDS, copied from vr_menu.ent with its values:
    vrp_wrist_display   entMeshComponent, vr_menu_plane.mesh at 1.00 x 0.50 m, disabled (switched on by script),
                        on the player's own transform (no parent binding): its local transform is in model space
    vrp_wrist_ui        WorldWidgetComponent, vr_menu.inkwidget (3200 x 1600), bound to and drawn on the display;
                        Natives/MenuSurface.cpp gives it a texture of its own by this name

Usage:  py -3.13 -B tools/ent/add_wrist_screen.py <player.ent.json> [more.json ...]
Idempotent: a file that already carries the screen is left alone.
"""
import copy
import json
import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from add_hold_components import add  # noqa: E402  (the shared writer: both component lists, the file's own format)

DISPLAY = 'vrp_wrist_display'
UI = 'vrp_wrist_ui'
SOURCE = Path(__file__).resolve().parents[2] / 'mods/archive/source/raw/vrport/ui/vr_menu.ent.json'


def cname(value):
    return {'$type': 'CName', '$storage': 'string', '$value': value}


def menu_screen():
    """vr_menu.ent's display and world widget components, and the binding records its package holds for them."""
    with io.open(SOURCE, encoding='utf-8') as f:
        root = json.load(f)['Data']['RootChunk']
    by_name = {c['name']['$value']: c for c in root['components']}
    chunks = root['compiledData']['Data']['Chunks']
    widget_chunk = next(c for c in chunks if c['$type'] == 'WorldWidgetComponent')
    return by_name['display'], by_name['vrp_menu_ui'], widget_chunk['parentTransform']['Data'], \
        widget_chunk['meshTargetBinding']['Data']


def screen(handle, base_id):
    display, widget, parent, target = (copy.deepcopy(x) for x in menu_screen())
    display['name'] = cname(DISPLAY)
    display['id'] = str(base_id)
    display['isEnabled'] = 0
    display['parentTransform'] = None
    widget['name'] = cname(UI)
    widget['id'] = str(base_id + 1)
    parent['bindName'] = cname(DISPLAY)
    target['bindName'] = cname(DISPLAY)
    widget['parentTransform'] = {'HandleId': str(handle), 'Data': parent}
    widget['meshTargetBinding'] = {'HandleId': str(handle + 1), 'Data': target}
    return [display, widget]


def patch(path):
    return add(path, (DISPLAY, UI), screen)


if __name__ == '__main__':
    for target in sys.argv[1:]:
        where, what = patch(target)
        print('%-40s %s' % (Path(where).name, what))
