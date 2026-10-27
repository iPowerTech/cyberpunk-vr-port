# Physical shotgun reload (2026-10-19)

`mods/cet/CyberpunkVRPort_Reload/reload/shotgun.lua`, dispatched from `reload.lua` `M.frame` for any weapon
config with a `shotgun` table. The module is kept on `M.sg` and talks to the rest of the reload through one
helper table `M.sgH` (poses, grips, sounds, trigger mode, magazine count, belt, carrier, spawner, ray casts),
so `M.frame` stays at 67 upvalues (CET's LuaJIT accepts it; stock LuaJIT stops at 60).

Every number in the configs (`reload/weapons/`) was read off ReloadRecorder takes of the native animation
with VRIK off -- an empty gun and after one shot, three takes for the pumps -- and the anim graph was checked
for every clip the weapon uses.

## Break-action: Satara, Igla, Testera, Palica

- **Open**: R3. The holder hand's thumb runs the recorded keyframes onto the top lever (`sg_break_thumb_over`,
  `sg_break_thumb_press`), the lever turns (`lever`), the hinge drops with constant angular acceleration and a
  bounce (`hinge`: Satara 44.6 deg, Palica 68 deg, Palica's latch rides along).
- **Cases**: each chamber is live / spent / empty. The extractor lifts a spent case 8 mm; after that it slides
  by Overture-like physics (kinetic/static friction 0.30/0.40, inertia from the chamber's own acceleration),
  out through the breech only. Past its length it becomes a world body (exEntitySpawner, `vrp_sg_case_*.ent`):
  ground ray casts, bounce, friction, lies down, despawns after 7 s, 12 at most.
- **Shells**: the left grip at the belt's shell row takes N = the chambers that are not live (1 or 2) --
  `Items.VRPortShellsSatara` / `...Palica` in WeaponLeft, parts `shell_a` / `shell_b`, held in the recorded
  carry (`sg_*_carry1/2`). The magnet takes one shell per chamber: a depth from the mouth (-L) to seated (0),
  perpendicular distance and angle; swing-only alignment, the wrist pinned from the RAW wrist; seated at
  80 % depth -> ammo +1, that part hidden.
- **Close**: a flick -- the gun's pitch-up rate about its own X >= 240 deg/s and <= 2000 deg/s for two frames
  running, 0.35 s after opening -- or R3. The trigger is held off while the gun is open.

## Pumps: Carnage, Tactician

- **Fore-end** (`pump`): written ABSOLUTELY from the rig's rest (`restLocal`), riders included (Tactician's
  ejection-port cover, 0.887 of the stroke). A relative write latched onto the game's own cycling clip.
- **Rack**: owed after a shot or when nothing is chambered. Taken by a grip edge in reach, or -- the hand already
  on it under the two-hand grip -- by pulling back 12 mm. Past 85 % of the stroke the hull (or a live round)
  is thrown out of the ejection port as a body; home below 8 % chambers the next round. Our own spring
  (180 ms, zeta 0.75) brings the hand onto the fore-end.
- **Loading**: one shell at a time off the belt's row, along the recorded tube line through the loading port
  (the gate flap / lifter pulses), or -- dry, action open -- along the port line straight into the chamber.
- **Trigger**: live only with a round chambered and the action shut (s <= 3 mm).

## Belt

`CyberpunkVRPort_TacticalBelt`: shell-row reach (`shellDist`, `shellRadius`, `shellTaken`), Igla / Testera
use the Satara row. Fixed: `EnumInt` returns a 64-bit cdata, so `WEAPON_CLASS[EnumInt(...)]` never matched --
now `tonumber(...)` (it also broke the `weapon=<class>` holders).

## Native

- **Simulator grip** (`OpenXRManager::ApplySimulatorGripFix`): the OpenXR Simulator gives its aim pose as the
  grip, so every wrist bent ~90 deg down there. On a runtime whose name contains "Simulator" the grip is
  turned about its X by `CyberpunkVR_SimGripPitchDeg` (75), and `CyberpunkVR_SimGripRollDeg[2]` (0, 0) adds the
  roll the simulator cannot give a controller -- tests turn a pump on its side with it. No effect on headsets.
- **Scanner gesture** (`XInput.cpp`, `cvr::input::ScannerSqueeze`): only a squeeze that BEGINS at the ear opens
  the scanner. A hand already squeezing -- a shell or a magazine on its way from the belt to a gun held close
  -- went through the 22 cm zone and opened it. `tools/scanner_input_tests` covers it.

## Assets

`build/shotgun-reload-20261019/tools/make_sg_entities.py`: case / round bodies from `vrp_case_overture`
(mesh swapped), the two shell items from `vrp_gren_emp`; `assemble.py` with `assets/sg_map.json` into
`cyberpunkvrport.archive`; TweakXL `mods/tweaks/vrport/Items.VRPortShells{Satara,Palica}.yaml`.

## Verified (OpenXR Simulator, harness in `build/shotgun-reload-20261019/harness`)

| gun | sequence | result |
|---|---|---|
| Satara | 2 shots, R3, tilt, belt, magnet, flick, fire | 2 cases out, 2 shells in, closes at 452 deg/s, fires |
| Satara | 1 shot, ... | 1 case out, 1 shell taken and seated, am 2, fires twice |
| Palica | 2 shots, ... | opens 68 deg, 2 cases out, 2 in, flick 374 deg/s, fires |
| Testera | 2 shots, ... | 2 cases out, 2 in, flick 363 deg/s; one pull spends both (its NumShotsToFire is 2) |
| Igla | empty gun, R3, belt, ... | 2 taken, seated one by one, flick 463 deg/s, fires one barrel |

Found by these runs and fixed: shells let go while the magnet held one left `B.magnet` behind, the gun stayed
"busy" and no flick could shut it (R3 still did) -- `bHeldDrop` now clears the catch.
| Carnage | shot, rack, tube | hull out, chambered, am 3 -> 4 |
| Carnage | dry: last shot, rack open, port | am 0 -> 1 into the chamber, home, fires |
| Tactician | shot, rack, tube; dry port | 87 mm stroke, hull out, am 7 -> 8; am 0 -> 1, fires |

`pumpcycle.py` (fire / rack / load / dry / home / ammo, one pad gate): racks with the gun straight and
close (z -0.05) and, to load, SOLVES the gun's pose from the gun itself so the opening sits 16 cm in front of
the eyes facing up-left. `breakcycle.py` (fire / open / tilt / load / flick / close) does the same for the
break-actions. A pad gate must stay open for the whole sequence: the game drains pad input only while it is
held, a second gate fails (assert 9090), and closing it drops the grips.

## 2026-10-20: pumps under the two-hand grip, grip reach, belt, LOD, hot reload

- **The fore-end moves only on GRIP + TRIGGER** of the hand on it ("затвор передвигался только при зажатии грип +
  триггер"). The grip alone is the two-hand hold and racks nothing. The stroke is the hand's travel along the barrel
  MEASURED AGAINST THE GUN (it was the absolute hand position, so pulling the whole gun in racked it).
- **The two-hand hold stays closed through the rack.** Engaging under the hold no longer claims the hand's fingers
  (`g_VRReloadFingerActive` turned the hold off and spent its squeeze). The module publishes the stroke to the plugin
  (`SetVRTwoHandSlide`, a model-space offset of the support point with a 300 ms deadline); `TwoHandGrip.cpp` draws
  the welded hand that far back and measures steer and break radius with the stroke taken out, so the barrel holds
  its aim (0.2 deg Carnage, 1.9 deg Tactician through a full rack). Under the hold the published left wrist IS the
  weld, so the stroke is read off the controller itself (`GetVRTwoHandLeftCtrl`). With the hand free (the hold not
  closed) the module pins it to the fore-end as before.
- **The left trigger is kept from the game** while that hand grips the fore-end (`SetVRLeftTriggerBlock`, 300 ms
  deadline, applied after the LT merge in `XInput.cpp`): the same squeeze would aim down the sights. Off the gun LT
  still aims (UpperBody 6).
- **Grip reach** (`CyberpunkVR_GripReach`, CharacterRig.cpp): for a hand in a grip (the hold's support hand, or a
  wrist the reload locked) the girdle protracts further (`CyberpunkVR_GripProtract` 8.5 cm, clavicle up to 23 deg,
  3 cm glide, blade share 0.8, part of it round the ribs toward the middle), and the left wrist's remaining shortfall
  after the arm's caps drives a thorax turn of the top two spine bones (about -up with a third as much forward lean,
  up to `CyberpunkVR_GripTorsoMaxDeg` 20) on the next solve. The LEFT shoulder's anchor is carried by the exact turn
  (`VRIK_TorsoCarry`); the right one is not, so the gun does not move. The joint handed back for the hand target is
  the unturned one. The turned spine bones join the replay cache. Measured (Carnage, hold closed, gun pushed out):
  shortfall 74 -> -10 mm at 15 cm, 190 -> 50 mm at 28 cm, 292 -> 144 mm at 40 cm; diagnostics
  `CyberpunkVR_DebugGripShortMm[2]`, `CyberpunkVR_DebugGripTorsoDeg`, `CyberpunkVR_DebugGripReach[6]`.
  The first attempt measured the turn's shift off the ANIMATED shoulder, which a rifle pose holds 18 cm forward of
  the spine at the midline -- the anchor moved sideways and the reach did not change.
- **Belt**: the shell row is `weapon=pump` (class from the record / mag key `pump`); a break-action shows only its own
  clip (Palica's square cells, Satara's pair). The weapon in hand is identified from its `__mag_` components first:
  the Tactician carries an Ashura decal (`w_rifle_sniper__tsunami_ashura__base1_01_decal_01_d`) and the Palica the
  Satara's receiver (`..._rostovic_satara__rcv1_01`), and the first name to match hung the wrong ammunition on the
  belt. Shell row 5 cm forward on the female body (`offsets_female.ucbeltammo_shell1`). Test export `shownAmmo()`.
- **LOD**: the Satara's shell mesh switches to its hexagonal LOD1 at 0.21; the shell items, the case/round bodies
  (`forceLODLevel = 0` in make_sg_entities.py) and the hand carriers `vrp_hold_*` (runtime) are forced to LOD 0.
- **Hot reload** for development: `GetMod('CyberpunkVRPort_Reload').hot()` reads reload/ again off disk -- each file
  compiled with `local require = ...;` prepended on its first line (CET hides `package`, `setfenv` and `load`), the old
  module released first (`M.release`). `deploy.py cetlive <Mod>` copies without closing the game and hot-reloads the
  Reload mod. CET has no `Game.Reload()` (checked: "not a GameInstance member and is not a global"); its only reload is
  the overlay's "Reload all mods" button.

- **No hull on the shot** ("у помповых сама гильза вылетает после выстрела"): neither pump throws one from an
  animation (their anim sets carry sound events only); it is a second particle track of the FIRST-PERSON
  `muzzle_flash` effect, `w_shotgun_<gun>_eject.particle`, at 0 s (Carnage) / 0.3 s (Tactician) -- the native
  cycle's hull, played with every shot. The port already overrode both effects (weapon-trigger-fx 2026-10-11,
  emission and size 0.3), so the hull was smaller but still there. `tools/make_pump_fx.py` drops the eject event,
  its track item and its embedded particle from `mods/archive/source/raw` and cooks `mods/archive/build`; the
  `_tpp` effects stay (NPCs' guns still cycle themselves). The hull now leaves only on the rack (`pEject`).
  Checked: CR2W round trip equal to the source but for handle/buffer ids; frame bursts of one shot
  (`ejectshot.py`, the gun rolled port-up) show nothing leaving the port.

Harness additions: `pumphold.py` (grip-only / rack-under-hold / LT checks), `gripreach.py` (reach off/on),
`exports.py` (live read/write of any export), `beltshot.py`, `shellface.py`, `ejectshot.py`. Run them with
`py -3.13` (`python` is 3.14 now and lacks `capstone`, which the launcher's pad gate imports).

## 2026-10-20: box-magazine shotguns -- Crusher, Zhuo, Ba Xing Chong

These two are reload.lua's, like the pistols (configs `reload/weapons/militech_crusher.lua`, `kangtao_zhuo.lua`;
the Ba Xing Chong `Items.Preset_Zhuo_Eight_Star` is the Zhuo with the `star_cannon` appearance, capacity 2). Read off
four ReloadRecorder takes each with VRIK off (`takes/{crusher,zhuo}_{one_a,one_b,empty_a,empty_b}`; tools
`boxmag.py`, `boxsum.py`, `parts.py`, `timeline.py`, poses by `pose_from_take.py`).

- **Assets**: `vrp_*` slots on both appearances (`tools/add_vrp_slots.py`, now with `name=bone@x;y;z` for a slot at
  an offset -- the Zhuo's frame has no `mag_slot`, its receiver's is carried over), the hand magazines
  `base\vrport\vrp_mag_{crusher,zhuo}.ent` (`make_mag_entity.py` from `entities\w_..._mag_std_01.ent`), all through
  `assets/box_map.json` + `assemble.py`. Rig signatures in rigs.lua: Crusher frame 14, Zhuo frame 21 + receiver 19.
- **Crusher**: magazine at `mag_slot`, rigid hold measured to 0.4 mm across all takes, wellAxis fitted on the last
  60 mm. Its magazine rig hides the gun's magazine on float track **1** (`showTrack = 1`; 0 left it drawn after a
  drop). The handle (`reload_handle`, 70 mm, no stop) is worked only in the EMPTY clip, hence
  `chamberOnRack = 'dry'`: a magazine swapped with rounds left fires at once, one swapped into a dry gun waits for the
  handle (`magS.dry`, set by both the B drop and the hand pull).
  **Controls (user, 2026-10-20)**: a rifle magazine -- GRIP on it is the two-hand hold, GRIP + TRIGGER takes it out
  (`handPull`, `pullTrigger`); the handle is worked on the LEFT TRIGGER (`slide.grabWith = 'trigger'`, the grip lock
  does not bind it); the left trigger is kept from the game while the hand is on the magazine or the handle. The
  two-hand hold was moved onto the magazine (`mods/config/CyberpunkVR_TwoHandGrip_crusher.ini`, from the take with
  `tools/hold_from_take.py`; the native capture, which held the front of the well, is kept as
  `build/shotgun-reload-20261019/CyberpunkVR_TwoHandGrip_crusher.native.ini`), and with no magazine in the well the
  hold is off (`mag.holdIsMag` -> `SetVRTwoHandSuppress`, owned by this module) -- no grip on air.
  The same magazine pulled out and put back keeps its own rounds (`magS.carried`): measured 200 -> 3 shots -> 197
  (9 in the gun), pulled 188 + 9 in the hand, reseated 197 / 9. A magazine let fall loses its rounds; a dry one
  pulled out is spent and the next seats full (the pistols' rule).
- **Zhuo**: no handle and no chamber. The magazine sits between holders on the receiver: `hatch.parts` (new,
  `M.hatchParts`) drives the whole opening off the take -- the barrel block 38.5 mm forward, the pin, both holders
  spreading 16.5 mm, eight clips turning, each over its own stretch of the opening (open 0.44 s, close 0.54 s, sounds
  `mag_open` / `move`). The hand lets go 90 mm short (`seatAt 0.40` of 150 mm) and the holders pull it home
  (`selfSeat 0.18 s`, `shutOnGlide`). No belt pouch by design: a fresh magazine comes from anywhere off the gun.
  Its two-hand hold is on the body (z +126 mm), not the magazine, so it stays as captured.
- **Fixes on the way**: `slotModel(nil)` threw outside its pcall (a gun without a slide, the Zhuo -- the frame never
  ran); `slideS.locked` was only ever cleared on guns with a stop, so a pistol put away dry left it set and a
  magazine seated into the Chao/Zhuo/Crusher afterwards waited as `pending` forever (now cleared where there is no
  stop, and on teardown).

Verified in the simulator (`harness/boxcycle.py <Gun|Items.X> steps...`, one pad gate; steps empty/fire/drop/pull/
hold/air/belt/near/insert/rack/rackt/roll/shot/wait): Crusher B drop -> belt pouch -> magnet -> dry wait -> LT rack
-> 12 -> fire 11; grip alone on the handle does nothing; grip on the magazine = two-hand on, magazine stays; grip +
trigger = magazine in hand; partial magazine reseated keeps 9; no magazine -> no two-hand at 3 mm from the support
point; LT block only on the magazine / handle. Zhuo and Ba Xing Chong: B drop opens the holders, seat at 87 mm, glide,
holders shut, 4 / 2 rounds, fire. Regression: Unity (drop/belt/insert, R3 release -> 12), Chao (21, cover shut, burst).
The ReloadRecorder was removed from the game afterwards.

## 2026-10-20 (later): Pozhar, two Crusher holds, the cycle hull, pump magnet, flick, every variant

- **Pump hull after the shot, the real source**: not an effect. The game's own `cycle_round` (1.83 s, every shot)
  shows the magazine rig's reload shell -- track `showMagazineReload` (1) -- and throws it out of the port ~0.8 s
  (Carnage) / ~0.45 s (Tactician) after the shot, past the end of a 64-frame burst (64 frames = 0.72 s here). Found
  with `ejectshot.py <Gun> <tag> -75 plain <delay> [trk0|trk1|nocycle]` (burst started `delay` s after the shot)
  and a pixel-motion count: holding track 1 at 0 took it away, track 0 did not. `shotgun.lua` holds `cycleTrack`
  (pump configs: 1) at 0 while a pump is in VR hands (`M.cycleFree` lets go on every reset and during the game's
  own reload). Stripping the `_tpp` muzzle hull was tried first and changed nothing for the player; it was
  reverted (NPCs keep theirs). Verified: Carnage 501 -> 5 px of motion at 0.8 s, Tactician 467 -> noise at 0.4 s.
- **Pump magnet**: radius 65 mm (was 50), reach 1.3 line lengths before the mouth (0.8), 75 deg (65), gain 5 (3),
  clicks at 75 %; then `s.push` carries the shell on to 125 % of the line in 0.1 s by itself, the hand with it
  (`pushTo`, `pushPort`, `pushTime`), and only then counts it in.
- **Break-action flick**: 320 deg/s over three frames (was 240 / two) -- it closed on the way back up from tipping
  the cases out.
- **Several two-hand holds per weapon** (TwoHandGrip.cpp): `CyberpunkVR_TwoHandGrip_<weapon>@<where>.ini` rows are
  candidates next to the plain one; an open hand is offered the nearest, the closed one keeps it.
  `SetVRTwoHandSuppress` is a mask now (bit 0 all, bit k+1 hold k), owned by reload.lua (`M.thMask`,
  `mag.holdAlt`): the magazine hold is taken away while the magazine is out, the fore-end stays.
  `CyberpunkVR_DebugTwoHandHoldIdx` says which is in use. Crusher and Pozhar: fore-end (the game's capture) +
  `@mag`.
- **Magazine turned in the hand** ("в руке при удержании просто развернуть магазин"): the takes' holds are the
  game's hand, and the magnet turned a VR wrist at the well by the difference -- Pozhar 172.5 deg, Zhuo 106.8,
  Crusher 25.2. `harness/natural_mag.py` measures, with the left hand at the well in a natural pose (controller
  along the gun), the seated magazine in that hand's WeaponLeft slot (`holdRotBasis`), turns `holdOffset` about
  the grip by the same correction, and rebuilds the `@mag` hold's W line for that natural hand on the seated
  magazine -- so neither the insertion nor the pull out of the hold turns the wrist.
- **Techtronika Pozhar** (`techtronika_pozhar.lua`; assets under rifle_precision): box magazine (206 mm), charging
  handle `cocking_device` on the RIGHT, 64 mm, only in the empty reload (`chamberOnRack = 'dry'`), worked by the
  left hand over the top -- on the LEFT TRIGGER like the Crusher, grip on the magazine = two-hand, grip + trigger
  takes it out. vrp slots on both apps (`add_vrp_slots.py`), `vrp_mag_pozhar.ent`, rig signature (16 bones), four
  takes, three poses. Verified: 12 -> 10 -> pulled -> reseated 10 (same magazine) -> fire 9; empty -> B -> belt pouch
  -> seat (pending) -> LT rack -> 12 -> fire 11; two holds (no magazine -> fore-end only).
- **Every shotgun record detected** (`harness/lua/detect.lua` per equip): 19 pump, 20 break-action, 9 Crusher, 6 Zhuo
  variants -- slot found, the right config (Igla/Testera iconics on their own, not the Satara's), the module's frame
  running on that instance. `_Left_Hand` Crusher variants are NPC-only and cannot be equipped.
