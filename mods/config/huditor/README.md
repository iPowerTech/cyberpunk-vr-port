# HUDitor for CyberpunkVRPort — the F11 binding and a VR HUD layout

HUD placement is **not** the port's job. The port shipped its own HUD mod until 2026-08-20 and it was
removed: it scaled the shared HUD root around screen centre, which is too blunt to be comfortable and
actively fought a real HUD editor. **HUDitor** (Nexus) moves
and scales each widget individually, which is what a headset needs.

The two files here are the port's HUDitor setup, saved so it is not lost and so anyone can start from a
layout that has already been tuned in VR instead of from the flat-screen default.

**Nothing here installs itself.** Both files are opt-in copies, on purpose: `persistency.json` is a
player's own layout, and dropping ours on top of it would be exactly the mistake the port made with
`UserSettings.json` (it shipped one language and imposed it on everyone).

## HUDitor.xml — the editor hotkey on F11

    copy to:  <game>\r6\input\HUDitor.xml          (REPLACES HUDitor's own file)

HUDitor binds its editor to **F7** by default. The port moves it to **F11**, because F7 sits next to
nothing in particular while the port's own overlay is already on F10 and Insert, so the two land
together on the same row of keys.

`IK_F7` appears in two places in a HUDitor install and only one of them matters:

| where | effect |
|---|---|
| `r6\input\HUDitor.xml` | **the real binding.** The game merges `r6\input\*.xml` into `r6\cache\inputUserMappings.xml` on every launch, and that merged file is what it reads. |
| `r6\scripts\HUDitor\config.reds` | a Mod Settings entry (`huditorEditor`), referenced by no other line of HUDitor. With Mod Settings absent it does nothing at all. |

So this file is the change. Editing `config.reds` as well only keeps the two from disagreeing if Mod
Settings is ever installed; it is not required and this repo does not carry a patched `config.reds`.

**Vortex will undo it.** A HUDitor install through Vortex marks `r6\input\` and `r6\scripts\HUDitor\`
with `__folder_managed_by_vortex`, so any redeploy or update of HUDitor restores F7. Re-copy this file
afterwards, or install Mod Settings and set the key in-game, where it lives outside the mod's folder.

Verified free before choosing it: F11 is bound by nothing in the game's own
`r6\config\inputUserMappings.xml`, by no other `r6\input\*.xml`, by no CET mod, and by no key binding
in `UserSettings.json`.

## persistency.json — the layout

    copy to:  <game>\bin\x64\plugins\cyber_engine_tweaks\mods\HUDitor\persistency.json

26 widgets: translation X/Y and a uniform scale each, all of them pulled inward and shrunk so nothing
sits out at the edge of a wide per-eye projection where it is uncomfortable or clipped.

Tuned at **2560x2560 per eye** (`hmd_type=3`, the square Pico/PSVR2-shaped ladder). It should carry to
other resolutions rather than needing a redo: HUDitor registers its widgets with Codeware's
`VirtualResolutionWatcher.ScaleWidget` (`r6\scripts\HUDitor\resolutionWatcher.reds`), so the stored
offsets are in virtual-resolution space, not raw pixels. That is read out of HUDitor's source, not
measured across resolutions — if a widget lands wrong on a very different aspect, nudge it in the
editor and re-save this file.

Back up whatever is already at that path before copying. HUDitor rewrites this file every time the
editor is closed, so a copy made after any session in the editor supersedes this one.
