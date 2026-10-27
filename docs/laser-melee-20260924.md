# Melee laser-dot visibility

The live Gorilla Arms report was reproduced on PID29948. The active item was
`Items.AdvancedStrongArmsLegendary`, item type `Cyb_StrongArms`;
`WeaponObject.IsMelee(itemID)` returned true. The native weapon class was0,
the equipped flag was1 and the barrel-dot timestamp was being published.
Read-only native evidence is in `build/laser-melee-29948/before.json` with symbols
resolved from the exact loaded DLL/PDB.

The Lua classifier now uses the game's melee predicate in addition to the
existing item-type fallback. This covers Gorilla Arms, Mantis Blades, Monowire,
fists and modded melee types without a list of localized weapon names. It runs
with the existing per-weapon classification, not as another per-frame query.
These weapons also correctly use the existing melee hand-filter class.

`DrawBarrelCrosshair` rejects melee class5. It clears the previous dot timestamp
before its early returns, so the mirror/second-eye consumers cannot retain a gun's
dot for their250ms stale-data interval after switching to melee or holstering.
The generic equipped flag still represents all equipped weapons.

Validation: Release plugin builds. `tools/weapon_tests/classification.lua` runs
the production weapon module's update callback with lexical game/native mocks
through WolvenKit, without modifying live game state. The previous Lua source
fails on Gorilla Arms (expected5, actual0); the corrected source passes22 cases,
including cyberware, conventional melee, unknown melee records, all firearm
classes, a non-melee launcher, predicate fallback and firearm selection after
melee. Physical headset validation remains for the next user launch.

Installed DLL SHA256:
`a90606f2514ebec72cc567c024a1f45173a6e79336fba37bd817250d55bf1a9d`.
Installed Weapon Lua SHA256:
`dd189ab6cd6c32ca15a3e292fdca435e6f8c69f1598ba993d4f62c949a8a036a`.
Backup/deployment report:
`build/roomscale-plugin-deploy-20260924-005453/deployment.json`.
Only the DLL and Weapon/init.lua were replaced. INI and calibration preserved;
the game was closed for installation and was not launched.

## Physical throwable melee — 2026-10-11

Left trigger still enters the native `MeleeTargeting` state. A fresh right-grip
press followed by release queues one physical throw for the currently held
`Throwable` weapon. Raw RT cannot throw it. The native throw state executes its
usual projectile creation, visibility, perks and recovery operations at physical
release instead of waiting for the flat animation's release frame. Knife/axe
`MeleeProjectile` damage, collision, pickup and recovery remain native.

`VRMeleeThrowFrame` publishes timestamped, unfiltered tracking-space movement,
the tracking-to-world yaw and grip/trigger inputs. Release samples 20–65 ms
motion segments ending within the last 100 ms. Retaining 200 ms of samples keeps
the beginning of those segments at low frame rates. Repeated samples, tracking
origin changes, teleports, pause and weapon changes cannot create a release.
The sample's velocity is mapped to world space only after it has been measured;
camera and native body rotation do not add throwing speed.

The initial speed is `clamp(2 + handSpeed * 2.2, 4, 24)` m/s. A stationary/very
slow release gently tosses along the controller. Acceleration is -6.5 m/s².
Assistance selects a visible, active, non-friendly NPC target part up to 25 m
away inside a seven-degree cone, solves the low ballistic arc and corrects the
release by at most five degrees (85% of the error for closer directions).
There is no in-flight steering or view snap. While targeting and while the
thrown item is hidden, ordinary physical melee contacts are suppressed.

### Casual throw — 2026-10-18

The hand's velocity at release points well below the intended line (an overhand
stroke is already coming down when the grip opens), so throws mostly went into the
floor. `VRMeleeThrowFrame` now also carries the head's forward. The throw's pitch is
the head's plus 20 % of the stroke's difference from it; its heading moves 65 % of
the way to the gaze within 30 degrees and is the stroke's own past 60 degrees. The
launch is `clamp(10 + handSpeed * 5, 10, 45)` m/s at -1.5 m/s² (the flat game's
knife: 110 m/s at -20), about 0.2 m below the line over 15 m.

Assistance takes the enemy part nearest to the throw or to the stroke's own heading
(so a target the head is not looking at is still found) inside fifteen degrees, up
to 30 m, behind no geometry. The knife then leaves straight at it and flies the flat
game's homing curve (`CurvedLaunchToTarget`, the knife's own curve parameters) at
the player's speed.

While a throwable is held in native targeting the holster mod leaves the right grip
to the throw: an overhand wind-up starts at the shoulder, inside the back-strap zone,
and that squeeze used to put the knife away.

Live (simulator, `build/knife-casual-20261018/`): a stroke 30 degrees down with a
level gaze left 6 degrees down at 29.95 m/s, 0.18 m below the line at 14.6 m. An NPC
10 degrees off the gaze and one straight along the stroke with the head turned 40
degrees away were both hit (`KnifeAttacks.KnifeThrowAttack`, curved launch). Without
LT the same shoulder squeeze still holsters the knife. `test_physical_throw.py` adds
45 gaze aim cases.

Implementation: `physical_throw.lua`, `vrport_physical_throw.reds` and the native
frame bridge in `src/Natives/Cyberware.cpp`. The pending launch is bound to its
weapon and consumed once by its native projectile, with bounded expiry.

Validation in `build/physical-throw-20261011/`: Release build and scoped SCC
type-check passed; the installed scripts also compiled and loaded in CET.
`tools/vrik_tests/test_physical_throw.py` passes 60 cases at 20/30/60/90/120 Hz.
Live simulator checks used real LT/grip inputs on the flat roof. Neither an
unprimed grip release, RT alone, nor holding the grip launched a projectile.
Knife: 4.053 m/s hand speed -> 10.917 m/s launch. Assisted knife: 3.964 ->
10.720 m/s, one native `KnifeAttacks.KnifeThrowAttack` dealt 152.319 damage to
the test NPC. Tomahawk: 5.831 -> 14.828 m/s. Native spawn positions matched
release positions within 0.000008 m; directions matched the release vector.
Knife recovery returned to native targeting. Both weapons followed a falling
trajectory. Game closed immediately after the batch; simulator settings restored.
The subsequent 20 Hz history-retention boundary fix was replay-tested offline
and installed without another game launch.
