# Physical crouch

Implemented against the native crouch PSM. The game owns its movement modifiers,
collision capsule, stealth state and locomotion animation. The port does not scale
speed or write those state/physics fields itself.

## Fork review

Reviewed `C:\Users\dariulone\Downloads\cyberpunk-vr-port-main\cyberpunk-vr-port-main`.
Its `XInput.cpp` detects shared-slot height and sends a 150 ms R3 pulse on both
height-latch edges. This does not distinguish measured bending from crouching,
depends on toggle parity and cached slot data, and can emit its release-edge pulse
after entering a context where R3 has another meaning. That implementation was
not imported.

## Posture and gameplay ownership

- `PhysicalCrouch.hpp` subtracts only the geometric hinge height explained by the
  measured horizontal bend displacement. Downward gaze / inferred floor reach
  cannot explain away a vertical squat.
- Default entry depth is 0.25 m, release depth is 60% of that (0.15 m). An 80 ms
  dwell and hysteresis reject threshold noise. Recenter, player changes, invalid
  samples and long sampling gaps reset the latch.
- A crouch takes precedence over gaze-only bend inference. Real measured lean and
  its roomscale displacement are retained, so combined lean/crouch is supported.
- A fresh native head sample and live CET bridge are required. Menus, vehicles,
  device cameras, swimming and ladder contexts cannot acquire physical crouch.
- CET applies/removes only `VRPort.PhysicalCrouch`, a separate record derived from
  `GameplayRestriction.ForceCrouch`. The existing game status listener enables the
  normal crouch transition; native eligibility/ground checks still apply.
- Manual crouch and another source's ForceCrouch record are not removed. ForceStand
  and KeepState suppress our request. A held request is idempotent, not an R3 pulse.
- Sprint detent does not override the physical crouch. Actual stance is observed
  through `GetPuppetPS().IsCrouch()`; the old generic locomotion blackboard read is
  not trusted as proof that a stance was entered.

F10 Controls exposes **Physical crouch** and **Physical crouch depth**. INI keys:
`xr_physical_crouch=1`, `xr_physical_crouch_depth=0.450` (default since 2026-10-17; was 0.250) (metres, clamped to 0.15–0.55).

## Camera source

Native crouch originally lowered the camera by another 0.56 m and moved its mount
forward: a real 0.40 m squat moved the view down about 0.96 m.

`vrport_physical_crouch_camera.archive` adds a conditional, camera-only model-space
position node at the common output of `player_base.animgraph`, after the weapon
layers. It targets the actual camera binding bone, `Torso_fppCamera_Aim_JNT`.
The native crouch scalar, state machine and body/leg poses remain intact. The
node receives the native camera mount captured before applying our effect;
physical HMD translation then supplies the height change exactly once.

The camera flag is set on CrouchEvents.OnEnter and replaced on the next entry,
including manual crouch. A release envelope uses the native locomotion graph's
authored crouch spring; clamping its amplified value retains camera ownership
through the stand-up tail. No camera transform is repeatedly overwritten to
counter the animation.

The archive adds two input declarations to the same graph. It no longer replaces
`player_locomotion.animgraph`: the first version's local correction was overwritten
when a weapon was drawn. `build_physical_crouch_graph.py` reproduces the patch from
exported native JSON and rejects already-patched input. The resources were
round-tripped through WolvenKit. Original animation nodes are unchanged except
for the root output link; camera position is the only modified bone channel.

The DLL, CET module, TweakXL record and archive must ship together. CET checks the
new graph input before enabling the bridge; a DLL-only installation stays inactive
instead of reintroducing double camera descent. The existing package script
enumerates and includes these files.

## Verification

Evidence is in `build/physical-crouch-20261010/`.

- 32 C++ posture, combined bend/squat, gaze, noise and lifecycle scenarios passed.
- Existing bend/noise and rig/bend/body-orbit regressions passed.
- Lua checks cover held requests, manual stance, native restrictions, graph camera
  flag handoff, record loss, shutdown and preserving unrelated effects.
- Live5 used the normal OpenXR Simulator through QuickBoot. The player was
  teleported to a collision-verified 10×4 m strip near
  `(-666.8932, 850.5168, 64.0299)`; nine upward rays confirmed 2.2 m headroom.
  Walking samples additionally rejected more than 7 cm vertical change or leaving
  native crouch during the crouch-speed measurement. A saved manual crouch was
  cleared with the normal simulator pad action before the comparison.

| Measurement | Result |
| --- | ---: |
| Standing speed, same stick input | 3.85049 m/s |
| Crouching speed | 1.87503 m/s |
| Speed ratio | 0.48696 |
| Commanded physical head descent | 0.40000 m |
| Measured camera descent | 0.399986 m |
| Native camera base during crouch/stand transitions | 1.599998 m, unchanged |

Native stance entered/exited correctly, looking down did not steal a crouch,
pure bending did not acquire one, and disabling the feature released its effect.
All 694 recorded stationary transition samples retained the same native camera
height before HMD displacement. The game was stopped after the pass; test inputs,
the temporary background-input gate and settings were restored.

Live5 DLL: `b2441ab5c6af02d66a656732ea8974b027be1ae4318b4ea21bbfd61ce90157e8`.
Final installed DLL: `0666a6773ef7d64e89c33eae832f307168612d3938e0892eb256718d51726486`.
The final delta adds a raw OpenXR POSITION/ORIENTATION_TRACKED check so cached view
poses cannot perpetuate a gameplay crouch during tracking loss. It was built and
deployed after the live pass; no additional game launch was made for this guard.
First archive SHA-256: `ceb53e6c8fb995fde29f836cc549f1643e349f6c2e486fcdd4c871b8aa851e4e`.

The first movement run left a roof edge and is not accepted as a movement test.
Other startup/source-inspection runs are not counted as successful validation.
No real-headset comfort study or exhaustive traversal/saved-game matrix was run.

## Weapon equip correction

The reported weapon shift was reproduced on the verified flat/headroom-clear
strip. The old archive moved the native camera base from `(0, 0.20, 1.60)`
unarmed to `(0, 0.05, 1.04)` with the handgun: 56 cm down and 15 cm backward.
Re-publishing the physical camera flag did not fix it; entering crouch while
already armed also reproduced it. The correction's location in the locomotion
subgraph and its use of the camera's parent bone were the wrong boundaries.

The revised archive applies to the final shared pose and the actual bound camera
bone. Its release envelope also avoids a short camera drop when native stance
has exited but the crouch animation is still blending.

Evidence: `build/crouch-weapon-20261010/verified/analysis.json` and its raw trace.
The tested slots contained a Silverhand handgun, the Psycho double-barrel shotgun
and a katana. Drawing/switching those weapons while physically crouched left the
native camera base and settled wrist positions unchanged at recorded precision.
Standing up with the katana had at most **0.0153 mm** native camera-base deviation;
re-entering crouch also passed. The normal manually crouched camera retained its
1.04 m base before the fixture was reset via native pad input.

The bounded recorder expired after these captured phases; the late request to
holster was not executed and is not claimed as verified. The phase analyzer passed
all seven captured scenarios, including their transition frames. The watchdog
restored test inputs/settings and stopped the game. A separate earlier launch
closed normally before measurements and is not counted as a successful test.

Current installed archive SHA-256:
`060afe43a2cfc4f7dd039bb79c62d4d8f868cbb008d4fba70aaaef57534b12e3`.
The DLL remains `0666a6773ef7d64e89c33eae832f307168612d3938e0892eb256718d51726486`.
