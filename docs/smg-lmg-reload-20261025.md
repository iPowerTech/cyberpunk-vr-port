# Physical reload: the held magazine closed on the mesh, then the SMGs and LMGs (2026-10-25)

Continues `pistols-rifles-reload-20261020.md`. Same module and pipeline; work dir `build/smg-20261025`, tools in
`build/shotgun-reload-20261019/tools`.

## 1. The magazine in the hand ("магазин неправильно в руке сидит")

Measured, not guessed: the game's own hand on Copperhead/Umbra/Sidewinder hooks the fingers 13-25 mm in front of
the magazine with the thumb 18-19 mm inside it; the Pozhar's (the Crusher's hand) has the thumb 12 mm inside.

- `harness/handlook.py <Gun> probe` -- every left-hand joint in the held magazine's frame (`lua/magframe.lua`,
  the carrier `vrp_hold_left` composed with bone 26). The live snapshot is the carry pose exactly.
- `tools/graspfit.py <mesh.json> <frame> <name> poseout= json= png=` -- the FK hand from the probe, a small magazine
  move (+-50 mm, +-20 deg) and per-joint flex/spread so palm, fingertips and thumb touch and nothing is inside.
  Writes `<name>_mag_left_{carry,seat}.lua` and the new holdOffset / holdRotBasis. ~4 s per gun.
- The distance table is `tools/sdfgrid.exe` (C++, exact triangle distance; `build_sdfgrid.cmd`). **Sign by flood
  fill from outside** (signMode 1, the default): the Shingen's and Warden's magazines are wound both ways (47/60,
  34/60 faces agree) and their pseudonormals read fingers 30 mm off the box as 45 mm inside it.
- Check a deployed fit with `graspfit.py <mesh> <live frame> x norigid noflex` (gaps per group). `handmag.py`'s
  "surf" is from the finger axis: ~9 mm there is touching.

Every gun below, and Copperhead/Umbra/Sidewinder/Pozhar, carries its own fit (the Copperhead's grip on the Umbra
put the thumb 5 mm in, on the Sidewinder the fingers 3 mm in).

## 2. Rig signatures: 48 was the cap

`include/Anim/VrikState.hpp` VRRIG_SIG_MAX 48 -> 128. `reload/rigs.lua` reached 58 with this batch and the last
ten were refused silently; their first takes recorded the previous gun's rig. `harness/rigs.py` prints
"registered N, rejected M" -- run it after adding signatures.

## 3. The guns

All seven SMGs and both LMGs pass `harness/boxcycle.py <Gun> empty drop pull near insert rackt [M] fire fire` in
the simulator (the magazine seats, the rounds wait for the rack where the gun has one, the gun fires).

| gun | config | magazine | moving parts |
|---|---|---|---|
| Arasaka Shingen | `arasaka_shingen.lua` | 58 x 220 x 67 mm on TOP at the back, in from above | `cocking_device` on the left, slide stop at 75 mm (travel 80: 5 mm past it for the release nudge) |
| Krausser Warden (EP1) | `krausser_warden.lua` | the Shingen's layout, fresh one on the reload chain | the same handle and stop |
| Militech Saratoga | `militech_saratoga.lua` | 36 x 116 x 209 mm box | front handle 53.4 mm (the game's 20 deg notch turn left out), `chamberOnRack='dry'` |
| Budget Arms Guillotine | `budget_guillotine.lua` | 46 x 140 x 290 mm box, the Saratoga's animation | front handle 57.1 mm, 'dry' |
| Darra Pulsar | `darra_pulsar.lua` | 28 x 129 x 198 mm box, straight up the last 40 mm | top `cocking_handle` 53.5 mm, 'dry' |
| Arasaka Senkoh LX | `arasaka_senkoh.lua` | 24 x 94 x 138 mm box, rocked in | none: no `slide`, chambers on the seat. Its WeaponLeft wanders 130 mm, so the hold is from the wrist |
| Kang Tao Dian | `kangtao_dian.lua` | 38 x 95 x 246 mm box, up FROM BELOW (see 6: the slot was turned) | all automatic, a `hatch`: handle 33 mm forward + muzzle 62 mm out, cover 28 deg, rail 31 mm down; chambers on the seat |
| Constitutional Defender | `constitutional_defender.lua` | box drum 113 x 385 x 142 mm on the left | `cocking_handle` on the right, 158 mm, `cocking_middle` rides 0.54; 'dry' |
| Midnight MA70 HB | `midnight_ma70.lua` | its own box (113 x 137 x 147 mm) on the Defender's rigs | the Defender's handle, 158.9 mm |

**The LMG drum** is two components on one 23-bone rig with no tracks: `Magazine` (the belt) and `MagazineReload`
(the box, `mag_stdr_01`). The gun's drum is hidden by scale on `magazine_base` (`hideScale = { 1 }`, every drum bone
hangs under it); the held one is `vrp_mag_{defender,ma70}.ent` = the belt's visible copy plus the box
(`build/smg-20261025/drumbox.py` -- `chr(92)` for the paths: a heredoc ate the backslashes and turned `\f` into a
form feed once). The hold is the rig root moved by `magazine_base` (`tools/drumhold.py`), not boxmag's mag_std
formula.

Not done, by the user: **Borg4a** and **Authority** are not in the game (files only); the HMG has no reload. The
Authority's apps have no slot component at all -- `build/smg-20261025/inject_slotcomp.py` makes one (round-trip
checked) should it ever be needed.

## 4. Tools added

- `tools/gunsum.py <short> <rig.json> <mag mesh.json> [handle=] [anims=] [wellmin= wellmax= wellres=]` -- the whole
  config summary off the four takes (moving bones, hold, well axis, tracks, handle, drop block, sounds).
- `build/smg-20261025/mkcfg.py <short>...` writes the configs (picks up `fit/<short>.json`); `applyfit.py` for
  hand-written ones.
- `harness/boxcycle.py`: `insertto` after `near` no longer glides away (the step's own index); `rackt` for
  `grabWith='trigger'` guns; a magazine that goes in from above needs `near` before `insert`; `shot NAME` is two
  arguments.

## 5. Afterwards: Achilles cover, Ashura round

- **Achilles**: the cover over the well (`magazine_open`, 33.5 deg up about +X), its lock riding round the same hinge
  and `transform_lock` (5.2 mm) live on the shared precision receiver rig `w_rifle_precision__rcv1` (9 bones), which
  had no signature: never recognised, never recorded, never moved. Signature added (which = 2), takes re-recorded,
  the parts joined the barrel halves in the `hatch` (open 0.30 s, shut 0.13 s). Checked with VRRigWriteDiag: all
  five receiver/frame writes applied every frame while the hatch is open.
- **Ashura**: the round goes in on the take's own diagonal again (from behind and above, 45 deg, one straight line;
  the `wellBend` is gone), and the magnet bites later: `seatSnapRadius` 0.050 and `magnetDepth` 0.060 (the rifle
  defaults caught it 128 mm out, 75 mm off the line).

## 6. Second round of fixes (user notes)

- **More than one way in, `wellAxes`** (reload.lua): extra lines through the same seat; each frame the line the
  magazine is nearest (out of the gun) is the well, and once the magnet has it that line stays until it lets go.
  `wellBend` / `wellTurn` belong to the first line only. dbg shows the pick as `ins[... well=N]` (boxcycle parses it).
  Shingen and Warden: from above OR slid forward along the bore from behind (`{ 0, -1, 0 }`; verified with the gun
  yawed 1.3 rad: caught on line 1, seated, released, fired).
- **Magnet strength per gun**: `seatSnapRadius` (how far off the line), `magnetDepth` (how far out along it;
  default 1.6 x insertRun), `insertGain` (weight = (1 - off/R) x gain, clamped). Dian 0.110 / 0.200 / 9, Defender and
  MA70 0.100 / 0.160 / 8, Ashura 0.050 / 0.060 (later).
- **Senkoh**: the well's 11 deg lean to the right taken out (`{ 0, -0.3688, -0.9295 }`).
- **Dian: the magazine goes in FROM BELOW**, not along the bore. Our `vrp_mag_slot` sits on the frame's `barrel` at
  the receiver `mag_slot`'s place, and `barrel`'s model-space axes are X->-X, Y->+Z, Z->+Y (the bore bone), while
  `mag_slot` has the model's own. With no turn of its own the slot put every direction read off it 90 deg round: the
  takes' "up from below" read as "-Y, along the bore", and the table above ("lies along the receiver, slides forward
  on a rail") came from that. `build/smg-20261025/dian_slotrot.py` gives the slot the inverse turn (180 deg about
  (0, 1, 1)); the well is now `{ 0, 0, -1 }`. The rail's 31 mm in the hatch is DOWN (its parent-local Y is the
  model's Z). Checked: caught below the receiver, seated (45), fired. **Any other slot on a bone with turned axes
  needs the same** -- compare the bone's model-space axes with the slot it stands in for.
- **Warden two-hand hold**: the Weapon mod had no family for it (record id key); `warden` token added
  (mods/cet/CyberpunkVRPort_Weapon/init.lua), captured with `harness/twohand.py Warden capture`
  (mods/config/CyberpunkVR_TwoHandGrip_warden.ini), held in the simulator from four sides. The MA70 already resolves
  to `defender` and uses that hold.
- **Racked cases**: the five SMG/LMGs with a port (Saratoga, Guillotine, Pulsar, Defender, MA70) got the flying case
  (`make_case_fx_smg.py`, the rifles' recipe: cal_9mm / cal_7_68mm) and every racked-out case or round is now heard
  landing 0.45 s later -- `w_casings_{small,medium,large}_hit_floor` from the game's bank, by weapon type or
  `slide.caseSnd`. The Shingen and Warden have no port (caseless).

## 7. The support hand on the magazine, every rifle (2026-10-26)

"Для всех винтовок у которых торчит магазин -- 2h grip на магазине, держаться просто грипом, а грип + триггер будет
вынимать его. Как у pozhar". The thirteen rifles that already took the magazine out by grip + trigger (`handPull`,
`pullTrigger`) got the Pozhar's second hold: `CyberpunkVR_TwoHandGrip_<name>@mag.ini` and `mag.holdAlt = 1` (taken
away while the magazine is out of the well). Achilles, Ajax, Copperhead + Umbra (one file: the Umbra is published as
`copperhead`; the two hands are 9.7 mm apart and the file has the mean), Grad, Hercules, Kolac, Kyubi, Masamune,
Nekomata, Osprey, Sidewinder, SOR-22.

`build/maghold-20261026/mag_holds.py` makes them: the gun's own fitted carry (`holdOffset` / `holdRotBasis`, closed on
the mesh) round the SEATED magazine, as the left wrist in the right wrist's frame. Three things had to be right:

- **the idle frames** of `<gun>_one_a.lua`: those whose left wrist sits where the plain hold puts it (0.0 mm on all
  thirteen -- the stance the plain hold was captured from). A take's first frame is the switch from VRIK (hundreds
  of mm off); a fixed 0..0.4 s window took it in.
- **the BIND WeaponLeft** (`BIND_O` / `BIND_Q`, tools/wristhold.py), not the take's slot: the Nekomata's animation
  carries that slot 287 mm from the hand, and its hold came out 54 cm from the right wrist until this.
- **the module's seat turn**: the weapon's orientation (times `mag.seatRot`), as `seatedWorld()` turns it -- not the
  mount slot's, which on the SOR-22 is 54 deg off the weapon's.

`py mag_holds.py check -0.24320 0.00253 0.07919 0.96673` recomputes the Pozhar's @mag from its pre-fit hold: equal to
the file to 0.1 mm. Checked with `harness/twohand.py <Gun> viewmag`: hold 1 on with the grip, pictures, the module's
catch distance with the hand there (`snapD`), the trigger takes the magazine into the hand and the hold lets go.

**Measured in the game** (`harness/maghold_live.py <Gun> <name> [write]`): the reload module's own wrist target for the
seated magazine (`M.magHoldW()`, magWristTarget in the right wrist bone's frame, left hand away -- a closed hold steers
the gun). Against it the take-made files agree to 0.1 mm on ten rifles; the **Nekomata**'s take holds the gun 147 mm
away from VR's (its hand came out at the scope and grip + trigger found nothing) and the **SOR-22**'s was 8 mm / 4.8 deg
off -- both written from the game (`mag_holds.py` keeps them, `LIVE`). The **Kolac**'s live read took the magazine the
other way round (the module picks it by where the hand is); the file's is the config's own and its catch works.

The **Pozhar**'s @mag predated the mesh fit and sat 36.6 mm / 8.5 deg off the fitted carry: W rewritten from the game,
fingers from `pozhar_mag_left_seat.lua`. The Crusher's agrees (0.7 mm).

A pulled magazine in these checks is dropped; with no magazine in the well hold 1 is taken away (right), so put one
back (`boxcycle.py <Gun> pull near insert`) before checking that gun again.

Bullpups (Grad, Kolac, Osprey): the magazine is behind the grip, and so is this hold. The SOR-22's magazine is on the
left, its hold 77 mm from the fore-end's: an open hand is offered the nearer.


**The SMGs** (same day, "сделай так же для smg с торчащим магазином"): Saratoga, Guillotine, Pulsar, Senkoh LX, Dian --
`holdAlt` through `mkcfg.py` (spec `holdAlt=1, holdAltName=`), so a regenerated config keeps it. Live against file:
Saratoga, Senkoh, Dian 0.1-0.2 mm; Guillotine and Pulsar 5.7 mm along the bore, written from the game (`LIVE`). The
Pulsar's magazine goes up through the pistol grip, so its hold is under the right hand. Not the Shingen / Warden: their
magazine lies along the top of the receiver.
