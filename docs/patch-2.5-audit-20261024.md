# Patch 2.5 audit: what the port used on 2.31, checked on 2.5 (2026-10-24)

Scope: every layer of the port except single pass (disabled, excluded by request). Game 2.5 / exe 6AD55DAF.
Each layer was checked against the 2.31 build by tool, then by hand where a tool flagged something.

## Results

| layer | checked | result |
|---|---|---|
| native hooks / detours | port log vs `tools/patchday/data/log_baseline-2.31.json` (`check_logs.py`) | 37 hooks + 81 detours ok, 0 FAILED, 0 lost; the 60 skipped are single pass / timeline diagnostics, off by design |
| moved struct offsets | every MOVED entry of `build/patchday/struct_drift-2.5.txt` against the current source | 1 live bug fixed (Panzer, below); the rest carry the 2.5 value or are single pass / diagnostics |
| changed engine functions | the 439 port-used rows of `diaphora-2.5/review_used.tsv` (S/Y/B classes) against the files they name | all carried; the matches on 2.31 numbers were unrelated fields or comments |
| SDK layouts | 122 classes, 472 fields (`check_logs.py`) | 0 size or offset mismatches |
| redscript | 457 port annotations (`@wrapMethod`/`@replaceMethod`/CET Observe/Override) vs both decompiled bundles | 404 untouched; 50 bodies + 3 signatures changed, all read: 2 replacements and 6 per-hand names fixed (below) |
| CET Lua names | fields/methods 2.31 had and 2.5 removed, against every `.field` / `:Method(` in mods/cet | none used |
| weapon anim graph | every event/feature the port sends a weapon vs `base\gameplay\anim_graphs\weapon.animgraph` (2.5) | reload names fixed (below) |
| TweakDB | `check_tweaks.py` (names exist) + every port yaml that redefines a whole game record | names ok; 7 whole 2.31 dumps found, 6 removed (two-weapons bug, below) |
| settings | `check_settings.py` | every GetVar name exists; only the tool's reference UserSettings.json is stale |
| port archive | three-way merge done on patch day (`build/patchday/overrides-2.5/OVERRIDES.md`) | ok |
| other mods' archives | each archive in archive/pc/mod vs the 2.31 and 2.5 TOCs | Always_Best_Quality broke every icon: fixed (below); others listed at the end |

## Fixed in this pass

- **Two weapons drawn at once (key 1, any draw).** The port's smoking tweaks (`mods/tweaks/vrcigarette`) carried
  whole 2.31 dumps of six game records. `EquipmentArea.Weapon` had 3 slots; 2.5 has 6, the second half being each
  weapon's left-hand partner (`EquipmentSystemPlayerData.TryGetSecondaryItemID`: `items[i + size/2]`). With 3,
  slot 1's weapon became slot 0's partner: Nekomata in the right hand, the double barrel in the left. The other
  five dumps equal the 2.5 records exactly (read live with them removed), so all six were deleted; the cigarette's
  own item records stay. Checked live: weapon slot 1 now draws the Nekomata alone, left hand empty.
  `Items.cigarette_i_stick.yaml` is still a whole dump of a game record (2.31 values for every listed field); a
  2.5 change to the cigarette item would be undone by it.
- **Icons everywhere (inventory, key binds, HUD).** The user's Always_Best_Quality (2.31) replaces 1129 base atlases
  with copies whose 1080p/720p slots are cleared. 2.5 re-packed 49 of them (item_icons0..23, atlas_common,
  mappin_icons, icons_keyboard, ...): item_icons0 went from 135 to 128 parts, so 2.31 rectangles cut the 2.5
  textures. `tools/patchday/compat_atlases.py` checks the mod's rule on the 1080 atlases the patch left alone
  (the only difference allowed: slots 1 and 2 cleared), applies it to the 49 2.5 files and packs
  `!Always_Best_Quality-2.5-compat.archive`, which sorts before the mod and wins. Installed; the inventory is right
  again (checked in game). Remove it when the mod gets a 2.5 release.
- **Panzer cannon aim (vehicle weapon slots).** The vehicle's weapon array moved 0x20: +0xB00/+0xB0C/+0xB14 ->
  +0xB20/+0xB2C/+0xB34 (sub_1409E6B1C walks +0xB20 by +0xB2C, sub_1407E351C writes the target at +0xB34). With
  the 2.31 offsets every launch failed the slot check and stayed native (`src/Hooks/PanzerWeaponAim.cpp`).
- **NoAmmoDecisions.ToReload** (`manualReload.reds`, a replacement): 2.5 added "no reload in shoot states 4-6" and
  "none on the frame QuickMelee is pressed"; both merged.
- **2.5 dual wielding split the weapon state machine's names per hand.** The port still used the 2.31 names, which
  nothing writes or reads any more:
  - `ControlLowerAndHolsterWeapons.reds`: TurnOffPublicSafeTimeStamp -> RightTurnOffPublicSafeTimeStamp (the game
    lowered the weapon on its own timer again and a manual lower never reached it);
  - `vrport_no_anims.reds`: anim feature WeaponHandlingData -> Right/LeftWeaponHandlingData (recoil 0 had no effect);
  - `vrport_reload_anim.reds`: ReloadLogicalDuration / EmptyReload -> the state's getters (finish time was -1);
  - `vrport_weapon_trigger.reds`: LastChargePressCount -> the state's getter (semi-auto press count);
  - `reload.lua` (and `shotgun.lua` through it): the weapon graph has no `Reload` / `InterruptReload` events or
    `ReloadData` feature any more (`Reload` is only a state name) but `RightReload` / `InterruptRightReload` /
    `RightReloadData` and Left; the hand is the weapon's (`IsEquippedInLeftHand`, checked live: false for the
    right-hand Nekomata).

  `check_reds.py --all`: REDSCRIPT OK with every installed mod.

## Open

- `src/Stereo/FrameGraph.cpp` writes the RTT camera component's +0xA00 when `xrBdCamDirty` is set (default 0, VRCAM
  mode only). The component grew 0xA10 -> 0xA70 and the field sits in an unnamed block in both builds; one function
  pair suggests +0xA10. Not changed; verify before turning the flag on under 2.5.
- Not exercised live in this pass: the Panzer cannon, a physical reload, lowering/raising the weapon. The fixes
  follow the 2.5 code and graph; a session with each is still due.

## Other mods (the user's)

| mod | what | effect on 2.5 |
|---|---|---|
| BlurBegoneNative 2.4.0 (RED4ext plugin) | refused by RED4ext: "incompatible with the current patch" | not loaded: its blur removal is off |
| EffectsRenderingFix | 1540 of its 1542 files are base effects/particles 2.5 changed | the 2.31 copies win over 2.5's |
| Enable_Advert_Animations | all 95 of its advert .inkwidget files changed in 2.5 | same |
| 0SandstormFlickeringFix, FixFlickeringParticles | 4/4 and 1/1 files changed in 2.5 | same |
| GeneralShadowsFix | 7 meshes changed in 2.5 (its script has the compat patch) | same |
| HD Reworked Project | 10 of 252 textures changed in 2.5 | texture replacements, harmless |

The 2.31 base versions of those files are not kept (only their tables of contents), so they cannot be merged the
way the port's archive was; a 2.5 release of each mod, or disabling it, is the way out.

Tools of this pass (scratch, `build/abq-2.5/`): `drift_check.py`, `review_check.py`, `script_audit.py` (+
`script_audit.txt`, every changed body old vs new), `dual_names.py`, `lua_gone.py`, `anim_names.py`.
