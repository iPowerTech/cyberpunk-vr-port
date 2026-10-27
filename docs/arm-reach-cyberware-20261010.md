# Arm reach and cyberware deformation

This is a chronological investigation log. The final behavior is recorded in
the last section; earlier thresholds, launcher axes and build hashes describe
the revisions tested at those points.

The shoulder anchor was measured from the neck but still subtracted the old
17 cm head-to-shoulder offset. Neck anchors now use the rig's own neck/shoulder
height and depth. The camera/head fallback retains its head-relative offset.
Extended overhead reaches elevate the clavicle; downward reaches can depress
it. The sternum pivot and existing upper-arm/forearm stretch limits are retained.

A captured-rig regression reproduced a 36.26 mm shortfall in the overhead
workspace before the change. The overhead, forward, floor, ladder, passenger,
shoulder-reference and weapon-stance checks cover the changed geometry. Targets
outside the uncalibrated avatar's arm span remain bounded; a deliberately extreme
simulator target 45 cm above and 30 cm ahead of the HMD was still out of reach.
This is not a claim of unlimited reach or automatic user arm-length calibration.
The final live shelf pose (controllers 25 cm above / 30 cm ahead of the HMD)
settled at 1.87 mm wrist-target error on the current uncalibrated Gorilla rig.

## Rebinding

The game-thread pump checks the root AnimatedObject, meta-rig identity, bone and
reference arrays, counts and track buffers each tick. A validated bucket index
avoids walking all animation-system buckets in the stable case. Metadata changes
trigger the existing locked rebind immediately, including growth on the same
track pointer. The ten-stalled-frame heartbeat remains a recovery fallback.

Publication still occurs only after metadata is complete and its identity is
rechecked. Pose jobs retain the lock and descriptor-capacity check. No bounds or
ownership guards were removed.

The first live trace (`build/arm-cyberware-20261010/after/telemetry`) covered
768/620/766/714/720-bone equipment transitions. From the observed rebind to the
next matched pose: 28–51 ms. Total matched-pose gaps, including native equipment
work, were 56–104 ms. These are sampled intervals, not an exact engine-event
latency; the earlier documented implementation waited about 0.2–0.3 seconds.

## Cyberware: native deformation pipeline

The earlier manual `_setup` alias transport and its visual acceptance were
invalidated by the user's headset captures. That implementation is removed.
The pose hook at RVA `0x17DDB4` is a per-graph mapping copier, not the final pose
callback. Replaying the full VRIK cache after every mapping overwrote the native
cyberware, FPP deformation and shadow results with stale transforms.

For an active cyberware rig, solve/replay VRIK only after `player_base.animgraph`
(path hash `0x7c8ef3ac7af95509`). Let the following graph consumers run without
writing the cached pose over them. Their SharedMetaPose inputs now see the solved
primary pose, and the game retains its own suspension constraints, finger grips,
wire spline and launcher deployment. Generic empty-hand/carry finger overrides
and manual forearm helper distribution are bypassed for these rigs.

The native dispatcher at RVA `0x328934` verifies the graph resource pointer at
instance +0x20 and its path hash at resource +0x30. Live diagnostic counters saw
`melee_cyberware` (`0xa3e7441ba96a1b41`), male FPP deformations
(`0x16320cecc15a325c`) and male shadow (`0xebb9897efd33f895`) after the producer.
The original native mapping always runs, and the existing player ownership,
capacity and lock guards remain in place. Ordinary weapon rigs retain their
previous replay path. Rig replacement resets the cyberware family.

## Cyberware combat and corrected mechanics

The earlier forearm-frame deployment is superseded. It used different attack
phases for the two hands and moved blades separately from their suspension.
Mantis now uses a Hand-origin anatomical frame: +Y follows ForeArm-to-Hand and
+Z is the projected dorsal hand normal from the rig's index/pinky bind points.
One native left deployed pose is reflected through bind space for the right.
The complete 54-bone suspension, rail, hinges, muscles and skin helpers move
together. The shared skin/index bind anchor is still welded after native maps.
The ordinary 25% / 45-degree forearm twist policy remains unchanged.

The cutting segment is blade02-to-blade05, not wrist-to-tip. Mantis gets a
10 cm allowance, Gorilla a short wrist/knuckle segment with an 8 cm allowance.
Each hand has its own tracking-speed history and swing episode. Both actual
geometry movement and raw tracking must move, preventing an animated finger
from generating another swing. Time is measured from the native monotonic
clock, not the game's slowed time. Threshold: 2.5 m/s; whoosh: 3 m/s.

Cyberware selects the weapon's native MeleeComboAttack / MeleeStrongAttack
records and creates an Attack_GameEffect with source=weapon, instigator=player.
Stamina cost comes from that record. Geometry finds candidate contacts; the
native effect owns physics hits, damage, elemental effects, reactions and perks.
No fixed damage or physical-only reinjection is used for cyberware. Ordinary
melee's existing damage workaround explicitly excludes cyberware. Confirmed
feedback comes from ScriptedPuppet.OnDamageReceived, correlated with target,
weapon and the queued contact. A queued sweep is not reported as a confirmed hit.

Artificial muscles use compact native VRIK-off samples, reproduced from
tools/vrik_tests/fixtures/cyberware-muscles.json. Mantis uses muscle-joint deltas;
Gorilla uses the authored transverse thickness channels. Only these channels
pulse, independently for each hand; no full player Attack animation, aim snap
or movement impulse is issued. Right-trigger input remains available as the
strong modifier but cannot also start a second vanilla attack in controller VR.

## Physical monowire

Each hand owns a separate free-ended, 46-point line, with the rig's authored
5 m length. Grip pays it out; release retracts it. The emitter control is an
independent input, never the simulated first joint fed back into itself.
World-space gravity/inertia and length constraints advance once per pose epoch.
The same points drive the rendered wire bones and swept attack contacts.
Native monowire_idle provides its authored visual effect.

Static-world contact planes come from native ray queries. Bullet/Camera can
miss the test roof; Sight Blocker hits it, so the successful preset is cached.
CCD starts outside the previous contact plane to recover constraint penetration.
The final settle/motion test measured zero floor penetration. Dynamic vehicles
and props are not rope obstacles in this implementation; NPC contact damage
still uses the native attack. No claim of full dynamic rope wrapping is made.

Wire contact requires motion, with a lower 0.8 m/s threshold. Target contact
memory survives swing rearming, so a settling line cannot continuously damage
the same body without separating. Grip belongs to the wire while selected,
excluding the scanner/holster/grab consumers. Raw grips remain available to the
native simulation; left grip155 is read from input, not the 127-float pose cache.

## Launcher selection and aim

The installed launcher occupies the native cyberware position in the weapon
cycle. Its own restrictions are checked because the ordinary cycle validator
rejects gadgets lacking RangedWeapon/MeleeWeapon tags. Native equipment still
owns its left-hand slot; no item categories or inventory records are spoofed.
Selection enters vrLauncherReady after equip, without a gadget-release shot.
Cancellation during equip cannot fall through to an automatic quick action.

TweakXL registers that state using the local name in the machine's state list
and explicit array<String> transitions. It stays deployed while selected and
leaves through native cleanup on weapon selection, cancel or gameplay restriction.
Charges and actual firing states remain native. The launcher is not covered by
the motion-melee trigger suppression.

ProjectileLauncher's native spawner receives a static provider at the rendered
fire marker and the ammo bone's +Y direction. Its four fins lie behind local Y,
confirming the projectile axis. The tiny marker-to-ammo positional difference
is not a direction. The native OnShoot event's localToWorld.Y, rather than the
entity's not-yet-updated orientation, verifies the actual launch direction.

## Verification

Captured-rig checks cover dorsal placement, <1 mm left/right symmetry, >40 cm
extension, cutting-edge alignment, wrist flexion and repeated-pass stability.
Wire tests cover gravity, deployment, retraction, length bounds, floor planes,
teleport resets and invalid tracking. Muscle samples return to identity and
retain the measured native bounds. Launcher-axis testing gives forearm dot
0.995158 for the native deployed fixture. Final REDscript type-check passes.

Live evidence is under build/cyberware-combat-20261010:

- combat3: actual Gorilla controller sweep, 613.90 -> 332.32 HP; native damage
  281.58. Left muscle scale reaches 1.90, right stays 1.00 during a left pulse.
- combat4: actual Mantis sweep, 613.90 -> 423.33 HP; native damage 190.57.
  Native wire contact/damage and launcher cycle/trigger/cleanup also observed.
- wire5: both 46-node lines, movement, settle, right-only and retracted states;
  native floor queries measured zero penetration in each active capture.
- final6: three VRIK-off references and twelve calibrated palm views, visually
  reviewed. Its original entity-orientation assertion was invalidated: OnShoot
  precedes that entity transform update, so shot7 reads the event matrix instead.
- shot7: native launch direction dot 0.99999997, muzzle/start discrepancy 3.28 mm.

No save was written. Test NPCs and scoped invulnerability were cleaned up;
every batch closed its exact game PID and restored simulator settings. Appearance
coverage is the current male character. All perk/element/body-mod combinations
were not exhaustively exercised; their native attack processing is retained.

## Installation lifecycle and the black stretched geometry

The black triangles were additional Mantis meshes left on the player after an
implant swap. Component isolation removed only those meshes and eliminated the
triangles; hiding clothing, the body, seamfix, wrist HUD or shadow components did
not. No mesh visibility workaround is included in the fix.

The old helper queued UnequipAll and immediately called EquipItem. Even replacing
that direct call with the ripperdoc's UnequipRequest + gameEquipRequest does not
work while the previous melee cyberware is still drawn. On a fresh load, the
native request experiment grew the Mantis component count from 14 to 64, and
both Gorilla and launcher screenshots still showed the triangles.

The user's suggestion to follow normal installation exposed the missing step:
complete the holster first. In the controlled native sequence, Mantis goes from
14 drawn components to 8 holstered components; after installing Gorilla or the
launcher it has 0 components. Returning to Mantis yields the expected 14. This
was verified without calling the custom helper and without hiding any meshes.
Evidence is in `build/cyberware-holster-lifecycle-20261010`; the failing native
request comparison is in `build/cyberware-native-equip-20261010-2`.

The corrected console helper now waits for both WeaponRight and WeaponLeft to
be empty, IsEquipPending to clear, and the RightArm holstered visual to attach.
After a short stable interval it queues the native uninstall/install requests,
waits for the requested ArmsCW item to become active, then draws melee cyberware.
It uses the actual seeded inventory ID and exact record matching. A pending
operation cannot be overwritten; changing player or timing out cancels further
work. Errors are contained so the ordinary VRIK update pump keeps running.

The final installed helper completed all four types, with zero remaining Mantis
components on Gorilla, monowire and launcher. All four reference views and four
palm orientations per type were visually reviewed again. The game was closed by
the capture wrapper immediately after restoring temporary input and equipment.
The installed helper SHA256 is
`f0c474ac6d00e46abbcd54df39f4827872fd73e81bd425929e1c904bbae612e0`.

The installation sequence is grounded in the local decompiled scripts:
`C:/Users/dariulone/Desktop/cyberpunk/cyberpunk/UI/fullscreen/ripperdoc/ripperdoc.swift`
(EquipCyberware / UnequipCyberware) and
`C:/Users/dariulone/Desktop/cyberpunk/cyberpunk/systems/equipmentSystem.swift`
(OnEquipRequest / ProcessEquipRequest). This test did not operate the ripperdoc
menu itself; it exercised those native request types after actual holster completion.
Supporting native controller inspection was exported to
`render_camera_RE/cyberware_appearance`, including
`cp2077_02e682fa_1403879b8_sub_1403879B8_ce93c9762a06.c` and its adjacent `.json`
metadata. The runtime lifecycle comparison, not an inferred binary patch,
established the fix; the game executable and controller code are unchanged.

QuickBoot is the only launcher. `--no-focus --simulator <exact DLL>` uses simulator
pad input for Continue and retains the native world/HUD readiness checks. A
private simulator build adds controller roll. Capture runs temporarily use
1280x1280 per eye; settings, input mapping and the original implant are restored,
and the game is stopped immediately when the capture batch ends. No save is
written by the test. The D: copy is untouched.

## Installed build and console commands

Installed/source DLL SHA256:
`79ea78b650bbd8504ca0ec051e7e36ed53541aba04fa9ae22c656cad92acf6f0`.

The mod exposes persistent CET commands (no undefined global VRCyber helper):

```lua
GetMod("CyberpunkVRPort_VRIK").EquipCyberware("gorilla")
GetMod("CyberpunkVRPort_VRIK").EquipCyberware("mantis")
GetMod("CyberpunkVRPort_VRIK").EquipCyberware("monowire")
GetMod("CyberpunkVRPort_VRIK").EquipCyberware("launcher")
```

The helper uses an actual inventory ItemID, including its seed, adding the
implant only if absent. It completes the native holster before installation, and
melee cyberware is drawn only after the equipment change;
the launcher is installed and can now be selected through normal weapon cycling
as well as deployed with the gadget button. Selecting it through weapon cycling
keeps it ready until switching away; the trigger fires through the native action.
All four commands were exercised during the capture runs. The delayed work is
part of the existing onUpdate handler; it does not replace the VRIK update pump.

The gallery in `cyberware images` contains the final Mantis/Gorilla/launcher
references and four palm views, independent-wire deployment/motion/retraction,
and the native combat and projectile measurements. The monowire vanilla
reference is labeled as an earlier capture. Superseded galleries remain under
build/cyberware-combat-20261010/superseded-gallery and earlier build folders.

## Follow-up: wire handling, combat elbows and false repeat punches

The wire now uses the native outlet-light binding from
`base/characters/cyberware/player/a0_002_ma__monowire_whip.ent`:
Left/RightInHandThumb plus the authored FixedPoint offset. The previous
nanoWire01_off control is inside the forearm and is not the housing opening.
Both chains share their authored length; native animation scales on the wire
controls/joints no longer shorten one rendered side. In the final geometry
capture the two arcs measured 5.001245 and 5.001586 m. Rendered first-joint
positions agree with the native outlet binding within 0.029 mm on both sides.

Wire integration uses 240 Hz substeps, tension-only long-range constraints,
low air damping and time-scaled sliding friction. The former 0.9 velocity
multiplier on every floor-contact substep made the distal line stick to the
ground. The short first link follows the emitter's direction so wrist flicks
can transfer motion to the free line. The floor/flick regression moved the tip
1.29941 m; maximum segment error in the movement regression was 0.000893 m.
Gravity and static-world collision remain; no rigid attachment of the whole
line to the controller was introduced.

Only Gorilla/Mantis controller VRIK uses the combat elbow policy. Wrist aim
chooses the bend plane on the shoulder/wrist IK circle; relaxed-arm forward and
height restrictions are bypassed. The wrist remains pinned to its target.
The bilateral hook regression allows 0.220 m outward elbow displacement with
0.035 m drop. Wrist roll still contributes approximately one quarter to the
forearm, and normal-arm/overhead/Mantis wrist checks pass.

Combat uses the current swept interval instead of waiting for a 100 ms average.
Contacts survive swing rearming until separation, and non-wire rearming needs
80 ms quiet or a real reversal. Crucially, velocity is gated by actual OpenXR
tracking coordinates, not the rendered model-space wrist: body/camera motion
and native hit reactions had been generating new swings with a stationary
controller. VRCyberwareFrame keeps its original header/geometry layout and
appends two tracking-space positions (W=origin serial+1, zero when unavailable)
and two tracking quaternions. The Lua reader rejects absent/invalid tracking
and recenter discontinuities. Tests cover 30/60/90/120 Hz short punches,
stationary controllers during model motion and tracking teleports.

The final live test (`build/cyberware-combat-20261010/handling10`) recorded one
physical swing, one native attack and one confirmed melee hit (39.0625 ms).
Those counters stayed unchanged for a further second with a stationary hand.
Subsequent damage was identified by its native records:
`Attacks.PhysicalDamageOverTime` and `Attacks.BreachEMPExplosion` (the game's
cyberware-breach weakpoint explosion; see core/components/cwBreachComponents.swift).
They remain native effects and no longer match melee feedback tickets.
Feedback now requires a matching melee attack record within 350 ms.

Wire geometry/outlet captures are in handling8; corrected controller-orientation
hook captures are in handling9. handling9 also exposed the model-motion false
repeat, which handling10 verifies is fixed. All bounded runs closed the game
and restored simulator settings and temporary equipment; no save or D: copy
was updated. New screenshots and measurements are under
`cyberware images/Handling fixes`, linked first from the gallery index.

## 2026-10-11: shorter supported wire and shared swing thresholds

The native wire meshes use separate ExtensionFloatTrackId values (left 0,
right 1), bound to leftWireSlideFactor/rightWireSlideFactor. Equal joint arcs
alone did not guarantee equal rendered lengths: the vanilla graph independently
retracts these material inputs. Arming now resolves those track names in the
current meta-rig. The pose hook updates only the two named live tracks, bounded
by each invocation's float-track count, and explicitly skips read-only reference
tracks. Physical geometry owns payout; the material sees the complete current
curve while deployed. This is reapplied after downstream cyberware graph passes.

Both wires are capped at 4 m, down from the previous 5 m. Tapered shape-memory
support holds the proximal section ahead of the hand and fades toward a free
tip. With a horizontal emitter at height 1.5 m, the deterministic resting test
measured the midpoint at forward 2.043 m / height 1.462 m and the tip at forward
2.821 m / height 0.165 m. Collision, inertia and independent grip release remain.

melee_tuning.lua is shared by ordinary melee and cyberware. Ordinary/Mantis/wire
use 2.5 m/s attack and 3 m/s whoosh thresholds. At the user's explicit request,
Gorilla uses 2 m/s for both. Sound uses the ordinary melee 90 ms window, minimum
40 ms span, 250 ms gap, 1 m/s rearm and 800 ms equip mute. Physical controller
motion gates wire swing starts and sound; residual line motion cannot start an
attack/sound by itself. A real wire swing retains a short inertial contact window.

Build and deterministic tests passed, including shader-track write bounds,
reference-track protection, resting shape, mirrored motion and silent slow
controller/oscillating-line scenarios at 30/60/90/120 Hz. The Gorilla 1.8/2.2 m/s
boundary cases pass without lowering other weapons' thresholds.

The requested live check was cancelled when the user said they would test in
the headset themselves. The already-started launch was stopped, the game closed,
and simulator settings restored. No new live visual verification is claimed for
this revision; older gallery captures predate these changes. Do not launch the
game again unless the user authorizes it. The revised DLL and all three Weapon
Lua files are installed.

## 2026-10-11: five-metre swing arc, earlier emission and Mantis base joint

The user restored the desired wire length to 5 m. The supported resting shape
remains, but filtered three-dimensional wrist translation/direction change now
adds tension during a swing. Tension extends to the tip and fades gradually
afterward. A smooth trailing bow peaks along the middle of the line and has
zero end tangents; segment constraints and collision still determine the actual
rendered/contact curve. Left/right/up/down 3.6 m/s sweeps measured 4.85–4.87 m
forward extension, a 0.49–0.51 m trailing bow and tip-axis dot products above
0.995. Static support, mirroring, independent retraction, floor contacts and
finite/teleport handling continue to pass. No swing thresholds were changed:
Gorilla stays at 2 m/s for attack/sound; the other families use ordinary melee
settings, with sound gated by real controller motion.

`vrport_cyberware_visuals.archive` contains one native material override:
`base/characters/cyberware/player/a0_002__monowire_whip/w_monowire.mi`.
StartGradient changes from 0.186 to 0.03; GradientWidth from 0.06 to 0.04.
Color/elemental material overrides are preserved. The edited source is
`mods/resources/cyberware/w_monowire.mi.json`. Rebuild with WolvenKit
json_to_cr2w into the same depot-relative path, then pack_archive on its root
folder named vrport_cyberware_visuals. build_dist enumerates the new archive;
sync_assets now includes it explicitly. A CR2W round-trip diff found exactly
those two changed fields, and the packed archive was read back successfully.
Archive SHA256: `ca4356112a53f80589aa96d05ad5367c19a95dbd52ab604e7cfdc7e2d33b99a4`.

The user clarified that the Mantis issue is the round blade-base hinge. The
native innerWrist/socket local offset stays fixed in the deployed reference,
but the previous controller weld moved only the hand, leaving a 42–69 mm
separation in sampled poses. The complete mechanical profile is now translated
around the tracked mating pivot rather than the canonical Hand origin. Its
shape, blade orientation/length and skin/index weld are preserved; the joint
is not hidden or deleted. The generator stores the native wrist anchor beside
the mirrored 54-bone profile. The wrist test covers nine genuinely mirrored
targets (the old captured attack had asymmetric hand orientations), measures
symmetry about the mating socket, bounds the blade against the arm, verifies
the native 0.571 m cutting edge and checks socket continuity within 0.05 mm
per axis plus 20-pass idempotence.

Release build, targeted C++ cases and the Lua swing regressions passed. DLL and
material archive were installed and byte-verified. No game launch or in-headset
visual verification was performed, per the user's instruction. Offline GLB
inspection in build is diagnostic only; it does not reproduce the game's
downstream skin-deformation mapper and is not a replacement for game screenshots.

## 2026-10-11: elbow freedom by arm type

Controller VRIK now blends the existing relaxed elbow policy toward the full
Mantis/Gorilla combat policy by 50% for monowire and 25% for ordinary hands.
Mantis/Gorilla remain at 100%. The interpolation is a signed swivel angle around
the shoulder-to-wrist axis, so the elbow stays on its two-bone IK circle and
neither the wrist target nor segment lengths are scaled. The relaxed candidate
includes its original near-shoulder/height limits before partial blending;
shared outward separation is retained. Smoothing time/rate interpolate with the
same factor, and changing profile resets the old smoothing state. Ladder reach
and non-controller modes retain the unblended relaxed policy.

The end-to-end elbow_blend test uses both sides, two heights and repeated
profile switches on the same rig. Measured angular fractions are 0.500000–
0.500001 and 0.249999–0.250000, with unchanged wrist targets and segment lengths.
The full-combat, forearm, overhead, Mantis wrist and ladder reach checks pass.
The Release DLL was deployed with matching hashes; no game was launched.

## 2026-10-11: final combat, gestures, launcher and world indicators

Ordinary melee, Mantis and monowire use 3 m/s contact and 3.5 m/s sound
thresholds. Gorilla retains 2 m/s for both. A fast swing adds a 1.5 multiplier
at 6 m/s (4 m/s for Gorilla); a two-handed katana/club adds a 2 multiplier.
The target-specific combo rises from 1 to 2 over ten hits, resetting on a
target change or five seconds without a hit. These multipliers feed the native
attack, retaining the weapon's damage, effects and reactions. Ordinary weapon
sound uses controller translation before visual filtering; wrist-only rotation
does not trigger it. Contact remains tied to the moving blade and swing episode.

Free-hand grip curls middle/ring/pinky, trigger curls index, and both close the
fist. Right-stick click with grip gives thumbs-up on each gripped free hand.
Held objects, reload and smoking retain finger ownership. Monowire uses the
captured left/right RestGrip while retracted, closing through 35–65% deployment.
Canonical gesture rotations reach Gorilla's setup fingers and Mantis' actual
skinned finger chains. An empty right trigger no longer draws a weapon.

The launcher fires through native states with the left trigger. Its muzzle axis
is launcher_fragment_fire local +Z; the animating cartridge's +Y was incorrect.
The visible launcher wrist uses reference bindings for LeftHand_setup and
l_Wrist_2_JNT_setup, which are siblings of the tracked wrist rather than its
children. Live tests measured 45-degree flexion, 35-degree deviation and a
100-degree roll, preserving the binding within 0.000025 degrees. The native
projectile spawn point matched the rendered muzzle within 0.008 mm.

Breach indicators use their native world anchor and an absolute 0.5 scale.
Nameplates register the exact native screen projection; mappins follow the
presentation widget selected by the game's position setter. Offscreen UI
effects carry the same widget identity through their generated texture quads,
with parallax applied only at final HUD composition. Mixed owners are rejected,
without matching screen rectangles. RenderDoc device interception also follows
the game's actual imported serializer so captures include the UI shader fix.

The final simulator A/B measured 15.5 pixels of breach disparity and 23.5 pixels
for the enemy marker, versus 0 and 0.5 with correction disabled; the right-eye
bounds changed by at most one pixel. Release build, REDscript type checking,
targeted VRIK tests and world-marker tests passed. Evidence is in
build/launcher-gestures-20261011/verification.json and screenshots in
cyberware images/gestures-20261011. Game, GPU replay and IDA were closed after
verification; the user subsequently accepted the headset result.

## 2026-10-11: baked user finger calibration

The right-hand export is preserved in
`tools/vrik_tests/fixtures/gesture-calibration.json` and compiled into
`HandGestureCalibration.hpp`. Six nonzero local corrections adjust the
trigger-only pose on both hands and thumbs-up on the right. The canonical
finger bind frames were checked on ordinary, Gorilla, Mantis and launcher
fixtures: identical local quaternion deltas produce mirrored motion, with
maximum axis error below 0.000001. Corrections blend with gesture input and
leave monowire's separate grip policy intact.

R3 plus right grip now affects only the right hand. No left-hand thumbs-up is
bound; L3 and its native ability are unchanged. The temporary Gesture Editor,
its native authoring APIs and CET binding are removed after preserving the
export. The two-hand break radius defaults to 0.60 m and the installed setting
is updated accordingly; acquisition radius and release timing are unchanged.
