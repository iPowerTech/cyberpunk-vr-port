# Firearm trigger and effects — 2026-10-11

VR firearms break at right-trigger travel 0.85 and reset at 0.20. Between those
thresholds the previous state is held. XInput still publishes the raw analog
value for hand/reload animation. Menu input, vehicle throttle, melee gestures
and the left-arm launcher retain their existing mappings. Entering firearm
control while already squeezing requires a release first.

The native weapon state machine handles every shot. In SemiAuto mode only,
the per-weapon `CycleTime` check is replaced with a uniform 200 ms interval
between native shots (five shots per real second); pre-fire delay is bypassed.
The real-time clock keeps this cap independent of game slow motion.
The game action's press
count, consumed by native `ShootEvents`, permits one shot per new trigger pull
without losing a press across Shoot/CycleRound transitions. Native ammo,
damage, effects, recoil, reload and safety predicates remain active. FullAuto,
burst spacing and charge behavior retain their native timing; a sustained
trigger is a sustained input, not a sequence of synthetic taps.

Overture uses the same break/reset with either hammer state, retaining its
analog hammer animation and crane-open safety. Lua reset includes a 1e-6
tolerance because the shared Float32 value .20 arrives as .20000000298.

## Archive

`mods/archive/cyberpunkvrport.archive` contains the original 317 entries plus
276 imported effects (593 total). Every original resource was preserved
byte-for-byte and the newly packed archive was extracted and compared with
the assembly directory.

The user supplied these two local archives:

- `reducedBulletTracers.archive`, Nexus file 24088 / 1.0.5, SHA256
  `7ddf712597310ac3e441a26d8d299399d077de4eb1690e0a91e21bc1e234007c`.
  All 123 effects are included byte-for-byte. Tracers remain almost invisible,
  exactly as in the supplied mod; they are not disabled.
- `ReduceMuzzleFlash1.0.5_60.archive`, Nexus file 23672 / 1.0.5, SHA256
  `e092da53940041b0a86e2b6c34b2a495ef58a05e4eb86b0ee71b329d75e9aa1d`.
  Despite its name, the altered particle constants were 50% of native.
  The same size/emission properties are recalibrated to 30% of current native
  resources (70% reduction). All 153 muzzle-related files are included;
  `w_apl_explosion.effect` is excluded because it changes a launcher explosion.
  Light timing, materials, audio, and other properties are preserved.

`tools/weapon_fx/prepare.py` prepares, assembles and verifies these resources.
Its inputs are the extracted original port archive, supplied mod archives and
WolvenKit JSON exports of current base/EP1 effects. `mods/archive/build` keeps
the added binary resources; `mods/archive/source/raw` keeps edited muzzle
JSON. The binary roundtrip verified all 698 changed constants at 0.30 times
their original values, with the rest of each resource semantically unchanged.

Installed archive SHA256:
`774206520e7b8eb46a123ab3ab6bd542c93ddeB9c9dd1c60d759dffffa2dfa34`.

## Verification

Evidence is in `build/weapon-trigger-fx-20261011/`:

- Release DLL build and scoped SCC type-check passed; installed REDscript also
  compiled successfully in the game and CET loaded the updated Lua.
- `analog_input_tests weapon_trigger`: exact thresholds, hysteresis, 100 rapid
  presses, uninterrupted automatic hold, invalid data and re-entry.
- `tools/weapon_tests/test_revolver_trigger.py`: the production Lua block with
  Float32 inputs, cocked/uncocked hammer and crane-open blocking.
- Simulator input through OpenXR/XInput, without game focus, on the flat roof:
  Unity and Overture each fired all five requested shots, including four rapid
  pulls spaced about 125 ms apart. Native cycle stats stayed at 300/400 ms.
  Unity shot gaps in the rapid sequence were 125 ms; Overture 109–141 ms.
- Both semi-auto guns produced no shot at .84, one at .85, no extra shot when
  moving .21 -> .90, and rearmed at .20. Magazine consumption matched shots.
- Lexington FullAuto: 125–141 ms shot gaps with native CycleTime 125 ms.
  Saratoga FullAuto: 62–94 ms measured gaps with native CycleTime 66 ms (frame
  scheduling and the diagnostic millisecond clock quantize the intervals).
  Both continued firing at .21 after the break and stopped at .20. No shots
  appeared during the subsequent stopped observation; ammo matched shot count.
- Screenshots `Lexington-flash.png` and `Saratoga-flash.png` were inspected.
  Exact 70% calibration is established by asset values, not a pixel-size
  estimate from these unsynchronized frames.

The first test attempt stopped before firing because its equipment request
did not resolve an inventory instance. The corrected batch selected actual
item IDs and passed with no failures. The game was closed after each bounded
batch and the simulator settings were restored. No RenderDoc replay was open.

After headset feedback, the initially unrestricted semi-auto cadence was
limited to 5 shots/s (200 ms). This subsequent REDscript-only adjustment passed
SCC type-check and was installed with the game closed, without another game
launch. The 8 pulls/s live results above describe the preceding unrestricted
version; automatic-fire code and the 0.85/0.20 trigger thresholds are unchanged.
