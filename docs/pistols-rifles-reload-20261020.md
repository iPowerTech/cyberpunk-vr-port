# Physical reload: the last pistols, then the rifles (2026-10-20)

Continues `shotgun-reload-20261019.md`. Same module (`mods/cet/CyberpunkVRPort_Reload/reload/reload.lua`), same
pipeline per gun: rig signature -> vrp slots + visible magazine entity in the archive (restart) -> four takes with VRIK
off (ReloadRecorder) -> analysis (`build/shotgun-reload-20261019/tools`) -> config + finger poses -> simulator check
with close-ups.

## The hand on the magazine: the rule

"Магазин в руке на нейтральный -- это ломает позу." The earlier fix that turned the held magazine to a "natural"
hand (`harness/natural_mag.py`, previous doc) is withdrawn:

- **Crusher**: back to the take's hold (holdOffset {0.00672, 0.03450, 0.04740}, holdRotBasis
  {-0.24320, 0.00253, 0.07919, 0.96673}) and its `@mag` two-hand W line.
- **Keep the take's hold** unless the game's hand holds the magazine upside down for a VR wrist. Then borrow a
  natural hand (the Crusher's) with its finger poses: `tools/borrow_hold.py` puts the donor's hand-to-magazine
  relation on the target's box (face alignment, not a pivot turn) and writes the `@mag` W line from a seat take.
  Pozhar and Zhuo use it (the Pozhar's "natural" turn had the box hanging over the hand, 172 deg about its own Y).
- **Check by close-ups**, 5-6 cm from the face, several controller orientations: `harness/magview.py <Gun> hand
  dist=0.06 views=front,yaw+45,yaw-45,yaw-90,pitch+45,roll180`.

## A charging handle on the right

"Наклоняет ствол, чтобы магазин смотрел вправо, и дёргают затвор; снизу никто не передёргивает." The Pozhar has a
second handle grip, `side`, the hand from above with the gun rolled +90 (magazine to the right); grips are picked by
distance among the angle-ok ones. `harness/handleview.py <Gun> roll=90 from=above [with=grip|trigger]`.
Verified in the simulator: the Pozhar rolled +90 and -90, the left hand from above takes `side` (3 / 12 mm off it)
and pulls the handle its full 64 mm; the SOR-22 borrows both grips and `side` pulls 73 mm (its rifle row below).
These two are the only right-handled guns: every other handle, lever or bolt here is on the left or on top.

## The last pistols

| gun | config | magazine | moving parts |
|---|---|---|---|
| Krausser Grit (EP1) | `krausser_grit.lua` | 36 mm box, self-seats over the last 41 mm (`selfSeat`, seatAt 0.585) | slide `mechanism`, 53.5 mm, dry rack (`chamberOnRack='dry'`) |
| Darra Nova | `darra_nova.lua` | 6-round tube under the barrel, `rollSym` n=6 | none |
| Darra Quasar | `darra_quasar.lua` | 16-round disc, seated 45 deg about -X (`seatRot`) | none |
| Techtronika Burya | `techtronika_burya.lua` | 4-round block from the left, `rollSym` n=4 | top `slider` lifts 11.8 mm as a `hatch` (open while the block is out) |
| Techtronika Metel (EP1) | `techtronika_metel.lua` | 8-round box into a raked grip, self-seats (`selfSeat` 0.08 s) | upper `slider` 70 mm on the stop, released by the hand or R3; hinges ride it |

- **Riders that turn** (reload.lua, slide `riders`): `turn` degrees about `axis` once the slide is `turnAt` back,
  and `abs = true` reads the rider from HOME rather than from the stop (a grab taken off the catch drives the slide
  from `lockBase`). The Metel's `front_hinge` rides 1:1 and turns 54 deg, `back_hinge` only turns, 86 deg.
- `M.cfgOf(weapon)` names the config driving a weapon, for the sweep below.
- No TwoHandGrip ini: Grit and Metel (one-hand pistols; Nova, Quasar and Burya had theirs already).

## Verified (OpenXR Simulator)

- Grit: drop, belt, insert (self-seat), let, rack, 30, fire 29. Nova: insert, 6, fire 5. Quasar: insert, 16,
  fire 15. Burya: cover open with the block out, shut on seat, 4, fire 3. Metel: empty (upper on the stop), insert,
  hand release, 8, fire 7. Close-ups of every hand hold.
- **Every handgun and revolver record** (`harness/detect_all.py Wea_Handgun,Wea_Revolver`, result in
  `build/shotgun-reload-20261019/detect_pistols.txt`): slot found, the right config, the frame running. Exceptions:
  `Preset_Base_Slaughtomatic` (the disposable pistol -- no magazine to swap, not ours by design).

## The rifles

Fifteen guns, `index.lua` after the Metel. Rigs in `reload/rigs.lua` (and the recorder's copy, identical); vrp slots
and visible magazine entities from `build/rifles-20261020/build_assets.py` (archive backup beside it:
`cyberpunkvrport.before-rifles.archive`).

| gun | config | magazine | moving parts |
|---|---|---|---|
| Arasaka Masamune | `arasaka_masamune.lua` | box, `handPull` | `cocking_slider` 70 mm on a stop, LT release; `reload_chamber` rides x1.538 |
| Nokota Copperhead | `nokota_copperhead.lua` | box | `reload_lever` 93.8 mm, dry rack; `counter_mass` rides -0.548 (forward) |
| Darra Umbra | `darra_umbra.lua` | box (own mesh) | the Copperhead's parts (its frame borrows them) |
| Nokota Sidewinder | `nokota_sidewinder.lua` | box | lever 75 mm, `counter_mass` -0.594; cover `hatch` 32.8 deg |
| Militech Ajax / Tsunami Kyubi | `militech_ajax.lua`, `tsunami_kyubi.lua` | box | `reload_front` 85 mm; the catch turns 90 deg by the first 5 mm |
| Rostovic Kolac | `rostovic_kolac.lua` | box, push stage at 0.1 m | `cocking_handle` 150 mm, four riders, the flip 57 deg |
| Nokota Osprey | `nokota_osprey.lua` | box, hold from the wrist | bolt 128 mm, knob lifts 91 deg; `magazine_release` 35 deg as a `hatch` |
| Techtronika Grad | `techtronika_grad.lua` | box | `chamber_opener` 108 mm; cover `hatch` 12.3 deg |
| Midnight SOR-22 | `midnight_sor22.lua` | box from the left | handle on the RIGHT, the Pozhar's two grips; cover `hatch` 28.4 deg |
| Tsunami Nekomata | `tsunami_nekomata.lua` | box, hold from the wrist | bolt 80 mm, knob unfolds 90 deg |
| Tsunami Rasetsu | `tsunami_rasetsu.lua` | into the OPEN carrier, button only | five-part carrier `hatch` (cover, base, flap, up, rotator) |
| Militech Achilles | `militech_achilles.lua` | flat, on the reload chain, from behind/above | barrel halves roll 12.6 deg apart as a `hatch` |
| Tsunami Ashura | `tsunami_ashura.lua` | ONE ROUND onto the open bolt, `rollSym` n=8 | the bolt runs itself: 100 mm as a `hatch` |
| Militech Hercules | `militech_hercules.lua` | ammo box, hold at the lock | none read (see below) |

What these taught:

- **The simulator steals the pad while the game has the focus**: it turns keyboard and mouse into controller input
  every frame and overwrites a pad write within 5 ms (the Masamune's belt and rack "did nothing"). `pad.py` now
  `unfocus_game()`s on entry -- activates the terminal if the game's PID owns the foreground.
- **Config matching, frame first**: guns borrow each other's parts (Igla/Testera <- Satara, Sidewinder <- Copperhead,
  Ashura <- Nekomata, Umbra carries Masamune decals), so `cfgFor` matches the `__base` non-decal components first and
  only then everything. "Most components" broke the Igla/Testera (seven Satara parts).
- **The WeaponLeft slot moves in some banks** (Nekomata 287 mm off the hand for the whole reload, Osprey 97-170,
  Ashura 62-205): a hold measured against it is an offset from a point the VR hand never has. `tools/wristhold.py`
  measures from the wrist and maps into the BIND WeaponLeft (off {0.0873, -0.0421, -0.0209}, q {0.5064, 0.5079,
  -0.5138, 0.4707}). The Osprey's "two holds" were that slot moving: one hold from the wrist.
- **The fresh magazine can travel on the RELOAD chain** (`reload_magazine` 2 -> `mag_stdr` 4): the Achilles throws its
  show chain 700 mm away for the whole clip. `tools/reloadchain.py <take> [t0 t1] [chain=a,b] [show=a,b] [all=1]`
  prints both chains, the wrist distance, the hold from the wrist and the well axis on the chosen chain.
- **A RIFLE'S MAGAZINE IS TRACK 0, a pistol's is 1.** The rifle magazine rigs idle at (1, 0) and the game's own reload
  drops index 0 while the magazine is out; the pistols idle at (0, 1) and drop 1 (the takes' `mt`, all guns). Twelve
  rifle configs were written with 1 and kept their own magazine in the gun after a drop -- fixed, and checked by
  pictures (Copperhead, Masamune, SOR-22: gone after B, back after a VR seat).
- **Hercules**: both 10-bone passes the plugin identifies are BOX instances (two box components, one rig), and the
  frame shares the drawn box's track buffer, so the box arrives cached as rig 1 -- the recorder's frame row was a box
  too. An A/B settled it: rig 0's tracks change nothing, rig 1's track 0 takes the drum off, so `mag.which = 1`. The
  boxes never leave the slot in the takes, so the hold is the hand on the box at the `lock_ammo_box` (three takes to
  0.1 mm) and the approach is that hold carried along the wrist (`tools/rigidbox.py`).
- **Ashura**: the bolt goes back and home with both hands elsewhere -- a self-running action, so a `hatch` open while
  the round is out. The round's mesh has no `visibilityAnimationParam` and its rig no tracks: hidden by scale,
  `mag.hideScale = { bones }` (reload.lua `gunMag`), and never a track write after it.
- **A track write on a rig with no tracks killed the game** (first Ashura drop): such a rig is handed the game's shared
  empty track buffer in Cyberpunk2077.exe's read-only data (+0x3013C20), and the plugin's `mov [r14+rcx*4]`
  (CyberpunkVR_Stereo.dll+0x2CDBF) wrote into it. Fixed twice: the Lua no longer writes a track for a `hideScale`
  magazine, and `WeaponRig.cpp` (`RigTrackCount`) gives a track buffer inside the main image no tracks. Note rig+0xA0
  is not the track count (it reads 3 / 5 / 10 / 13 -- the bone counts here); the guard is the buffer's address.
- Back-carried rifles have no belt pouch, so a fresh magazine is conjured anywhere with a grip (the Masamune has one).
- **The Nekomata's magazine was invisible in the hand**, two causes, both in reload.lua:
  - the held mesh component (`vrp_hold_left`) keeps its appearance NAME across `ChangeResource`; a gun whose magazine
    has no skin of its own (all `default`, `magSkin` = nil) never set one, so it was drawn in the last gun's
    (`clean`, the SOR-22's) -- a name its mesh lacks. `holdShow` now says `default` when nothing else is asked for.
  - the Nekomata's animation set carries the WeaponLeft slot 287 mm off the wrist (live, bones 23 -> 26; every one of
    the other 42 configs reads 99 mm, idle and with the magazine out), and the carrier hangs on that slot. Every
    hold is measured against the BIND slot, so `M.slotBind` gives the bind relation whenever the live one is more
    than 25 mm / 16 deg off it: `wristToSlot` (the magnet and the seat) and `handTarget` (the world pair on the
    bind slot, the carrier's local pair turned back into the live slot) use it. On the Osprey and the Copperhead
    the live relation equals the bind one to 0.1 mm / 0.0001, so nothing else moved. Left hand only (the right
    hand's bind is not measured).

### Verified (OpenXR Simulator)

Masamune 24 -> 21 (LT off the stop), Copperhead 30 -> 28, Umbra 40 -> 38, Sidewinder 30 -> 28, Ajax 30 -> 28, Kyubi
20 -> 19, Kolac 6 -> 5, Osprey 12 -> 11, Grad 4 -> 3, SOR-22 10 -> 9 (the side grip pulls 73 mm), Nekomata 4 -> 3,
Rasetsu 3 -> 2 (carrier opens on the drop, shuts on the seat), Achilles 9 -> 0 -> 9 -> 8 (halves open while out; pull by
hand and back in keeps 8), Ashura 1 -> 0 -> 1 -> 0 (bolt open while out), Hercules 90 -> 0 -> 90 -> 88 (box off the
gun after B, back after the seat; pull and back in). Hand close-ups of all fifteen (Nekomata after the two fixes
above, then its cycle again: 4 -> 3 with the rack).
Detection sweeps after the `cfgFor` change (`build/shotgun-reload-20261019/detect_*.txt`): rifles 148 records, 4 not
driven -- `Preset_Hercules_Legendary/Military/Neon/Pimp`, which are INVISIBLE in the vanilla game too (their visual
tags have no appearance: the Hercules .app carries only `prototype_toxic`, tagged `Default`); shotguns 86, all driven
(`exit=sg:*` is the shotgun module, counted as driven); handguns and revolvers 264, 2 not driven (the Slaughtomatic,
and `Preset_Omaha_Krausser_Bennett`, which the game will not put in the hands).

## Two-hand holds for the Osprey, Rasetsu, Hercules (and the Umbra)

`CyberpunkVR_TwoHandGrip_<name>.ini` (mods/config, beside the game) is one frame of the game's own idle two-handed
stance, VRIK off (`VRTwoHandCapture`), keyed by the name the Weapon module publishes on a draw
(`CyberpunkVR_WeaponName`: the longest family in record id + `friendlyName`, from the recoil table or the fallback
token list).

- **Osprey, Rasetsu, Hercules had no name**: no RecoilKickMax family, so the name was the RECORD id and a hold would
  have belonged to one variant. They are fallback tokens now (`CyberpunkVRPort_Weapon/init.lua`, beside Ticon and
  Tamayura); recoil unchanged (they still take the reference kick). Captured: wrists 258 / 274 / 393 mm apart.
  `Preset_Tech_Sniper_Rifle_Default` (friendlyName `..._rasetsu`) gets the Rasetsu's.
- **The Umbra already had one: the Copperhead's.** Its `friendlyName` is the Copperhead's (it plays the Copperhead's
  animations), and `copperhead` is longer than `umbra`, so it is published as `copperhead` -- recoil included (0.9,
  not its own 1.05). Its own stance, captured, is the Copperhead file to 3.4 mm, same turn and fingers; no file of
  its own. (That capture overwrote copperhead.ini beside the game for a minute; restored from mods/config, and
  `twohand.py capture` now refuses when the published name is not the gun's.)
- Checked in the simulator (`harness/twohand.py <Gun> view`): left controller brought to the support point, grip,
  `GetVRTwoHandActive` true, the hand on the fore-end like the Copperhead's, held through gun yaw +-70 and roll 60
  (`shots/sheet_th_*.png`). All 52 grip files beside the game equal mods/config.
- Still without a hold (each publishes a record id): the Warden and the Borg4a (SMGs); the HMG and the Authority.

## Weapons, where they stand (2026-10-20)

Every `Items.Preset_*` firearm family in the live TweakDB against the reload configs:

- **Physical reload done**: handguns and revolvers (18 configs), shotguns (9), rifles -- assault, precision, sniper
  (15). The sweeps above cover every record of those types.
- **Not done**: the submachine guns (Pulsar, Saratoga, Guillotine, Shingen, Senkoh, Dian, Warden, Borg4a -- 82
  records), the light machine guns (Defender, MA70), the HMG, the Authority and the test rocket launcher. They
  reload the game's way.

## Harness additions

- `twohand.py <Gun> name|capture [name]|view`: the published weapon name; the two-hand hold captured off the game's
  stance (VRIK off; refuses over another gun's file); the hold closed in the simulator and pictured.

- `boxcycle.py`: `to X,Y,Z` (left hand to a head-local point, a start for `insert` where `near` loses the well),
  `r3` (needs `pad.py find`; the stick click offset was not found on this pad, so the Metel was released by hand).
  `near` and `rack`/`rackt` try several starts (they stalled in local minima on the Kolac, Osprey and Rasetsu).
- `handleview.py`: several starts as well (the SOR-22).
- `dropview.py <Gun> [roll] [noequip]`: the gun's flank to the eyes, a picture before and after B -- is the gun's own
  magazine hidden once ours is out (the track-0 fix was checked with it).
- `insprobe.py <Gun> x,y,z ...`: the module's insertion depth / miss / magnet weight at each hand position.
- `detect_all.py <types> [only=]`: every `Items.Preset_*` record of those types equipped in turn; also prints the
  magazine switch the config holds (`hide=which.track` or `scale`) beside both rigs' tracks 0 and 1.
- `record_all.py`: the fifteen rifle rows, 9 s each (rows are `item, short, takes[, seconds]`).
- `tools/seatinfo.py <take> <t_seated> hold=...`: seat rotation, origin offset and the corrected holdOffset.
- `tools/handlegrip.py <take> <bone> <t0> <t1> [avg=a,b]`: the wrist on a frame part and the thumb/index distances
  (direct children of `barrel` only: it reads parent-local positions). `tools/rifle_scan.py <short>`: the moving
  frame bones per take plus the box summary.

## Headset feedback, 17 items (2026-10-20, evening)

What changed, per gun, and how it was checked in the simulator (harness under build/shotgun-reload-20261019):

- **Magazine out on grip + trigger** (`pullTrigger = true`): Masamune, Umbra, SOR-22, Achilles, Ajax, Hercules,
  Copperhead, Osprey, Sidewinder, Kolac, Grad, Kyubi, Nekomata.
- **A case out of the port on the rack** (`slide.ejectAt` + `ejectFx = 'vrp_eject_case'`): our own effect per gun,
  the case event alone, on the gun's port slot (build/rifles-20261020/make_case_fx.py; archive ba4b2263).
  Seen leaving the Ajax and the Nekomata. The Sidewinder has no port slot: its effect sits on `eject_side`
  (bone `magazine_opener`, the LEFT side at the magazine catch) -- a slot of our own on the receiver's right would
  fix it (archive rebuild).
- **Rounds only after a rack when the magazine ran dry** (`chamberOnRack = 'dry'`; a seat puts the rounds in
  `magS.pending`, a real stroke -- `slideS.racked` -- loads them): Ajax checked 0 -> 0 (`rackz`) -> 30 (`rackt`).
- **2H fore-end hold vs the handle** (trigger = handle, grip = hold): the handle preview of a `grabWith='trigger'`
  slide only shows with the trigger touched, and a slide held by the free hand takes the magazine catch out.
  Copperhead / Umbra / Sidewinder: `twohand.py view` -> two-hand true through yaw +-70 and roll 60; the
  Copperhead's handle still racks on the trigger.
- **Grad** (`slide.manual`): a drop in the count with the magazine home is a SHOT and leaves a fired case only the
  bolt throws out; the trigger is swallowed until then; the game's own `add_cycle_round` clip is held off
  (`cycleHold` 2.8 s) and its case bone hidden by scale (`cycleHide = { 6 }`) -- held at rest it stuck in the port.
  Checked: 4 -> 3, no second shot after 3 s, the rack throws the case without spending a round, 3 -> 2.
- **Kolac handle** ("рука не там магнитится"): its handle rider is on the slide's OWN bone (6, the 57 deg flip),
  and the plugin keeps one write slot per bone -- the rider's relative write replaced the slide's absolute
  translation with the bare offset, so while gripped the handle sat at the barrel origin inside the gun and the
  grip glued to its slot dropped 8 cm onto the pistol grip. A rider on the slide's own bone now rewrites the bone
  whole, absolute (translation + `restRot * turn`); `slide.restRot` carries the handle's own 59.6 deg rest turn.
  Probe (`handleview.py ... probe=vrp_cocking_handle`): the slot at (-35, +36, +67) mm from the barrel while held.
  The handle lever is its own mesh (`..._base1_01_handle.mesh`, bone 6).
- **Kyubi**: racked from behind -- the slide is `reload_mechanism` (9), the T over the grip (48 mm), the front handle
  rides it at 1.77 and stays folded. The hand is the Grit's pinch, moved as a whole from that gun's fin to the T.
- **SOR-22**: the handle is a 17 mm knob, x 39..93 mm on a stem out to the right; the Pozhar's two borrowed hands
  missed it by 33 and 53 mm. One hand of its own over the top (fingers forward, index at the knob's end, palm on
  it), `copperhead_handle_left`. Magazine catch widened (95 mm, 160 mm along the well, gain 8): from 16 cm out and
  43 mm off the line it now catches and seats.
- **Achilles** ("через текстуры"): the takes seat the magazine NOSE FIRST with its tail 46 mm up (19-25 deg about -X
  the last 35 mm) and rock the tail down. New `mag.wellTurn = { axis, deg, to }`: the wrist target is turned about
  the magazine's mount on the path, `deg` from `to` metres out, nothing at the seat; a magazine pulled out of a
  rocking well is led out along the same path until clear (`magS.guideOut`, the plain catch radius). Checked:
  20.0 deg about (-1, 0, 0) at 125 mm (`harness/turnprobe.py`), seats with 9 rounds. The guided pull-out is not
  covered by the simulator (the harness' raw hand is 14 cm off the line once it has the magazine).
- **Ashura**: `wellBend` (a polyline well): along the bore from behind, the last 20 mm down onto the bolt.
- **Hercules**: the box in the hand is an ITEM of two parts, box + cloth cover (`Items.VRPortBoxHercules`,
  vrp_box_hercules.ent, make_box_item.py) -- the carrier drew one mesh, the hand held a bare box; magnet widened
  for a 127 x 104 x 195 mm box (140 mm, 240 mm along, gain 9). Seen in the hand with its cover after a restart.
- **Copperhead / Umbra / Sidewinder hold**: the Crusher's hand (the take's holds were crooked in the hand).
- **Masamune**: a curled grip on the handle (`copperhead_handle_left`). **Pozhar**: both grips wear
  `pozhar_handle_left_v` (the paddle between index and middle), preview radius 0.07.
- Not reproduced: Nekomata "another magazine" -- the held magazine is the seated one's mesh. Its hand pose breaks
  when the gun in the hands is equipped again (`noequip` on magview/handleview).

Harness: `handleview.py yaw=RAD probe=<slot>` (the gun's flank to the eyes; the wrist, finger pads, the gun's slots and
bones 6/14 in the weapon frame, lua/handprobe.lua), `boxcycle.py pullout X,Y,Z`, `turnprobe.py <Gun> x,y,z ...`,
`look.py ... fx=<effect>`, `rackburst.py`. Mesh vertices per bone, offline: the mesh's JSON render buffer
(stream 0: Short4N position x quantization scale + offset, UByte4 indices, UByte4N weights) -- how the Kolac lever,
the Kyubi T and the SOR-22 knob were located: build/rifles-20261020/meshverts.py <mesh.json> (per-bone bounds) and
boneprof.py <mesh.json> <bone> [axis] [step] (slices), on `cp77tools convert serialize` output.

## Second headset round (2026-10-20, late)

- **Pozhar sight "идет перезарядка" over a full magazine**: `IronsightGameController` (the Pozhar's built-in sight,
  pozharcommonfront1.inkwidget) starts its `reload` loop when the count reads 0 and stops it only when the
  crosshair state LEAVES `Reload` -- a hand reload never enters it. `CyberpunkVRPort_Optics/vrport_optics.reds` wraps
  `OnAmmoCountChanged`: a count above 0 outside a game reload ends the loop the game's way (`OnReloadEndLoop`).
  Checked on the sight's screen: full -> empty (RELOAD grid) -> seated + racked (12) -> normal.
- **Several cases per rack (Nekomata)**: make_case_fx.py put the descriptor on EVERY effect spawner of an appearance
  -- the Nekomata has five per appearance, the Grit and the Sidewinder two -- and every spawner carrying the name
  starts it. Now one per appearance (the one with `ejection_port` / `muzzle_flash`, else the first). The
  Nekomata's case also leaves from a slot of our own, `vrp_eject_port` on `barrel` at the bolt-riding `eject` slot's
  rest. One case seen. Archive 91c44d45 (backup `cyberpunkvrport.before-onespawner.archive`).
- **Hercules box appearing on the gun while held**: the 140 mm catch is ALSO the seat test's sideways tolerance, so
  the box seated while still being brought up and the next squeeze took it out. Back to the hand-sized catch, a
  little stronger: 85 mm, 150 mm along the well, gain 7 (was 75 / 128 / 6).
- **Copperhead / Umbra / Sidewinder hold**: the Crusher's hand tilted these curved magazines 26 deg further along
  their own curve. Now the take's position and finger poses with the turn squared to the wrist (flat face on the
  palm, length along the fist, depth along the fingers): holdRotBasis { -0.02080, -0.01490, 0.02230, 0.99942 } =
  bind(WeaponLeft)^-1 * those axes, 5.5 deg off the take. Its curve's chord lies 29 deg off the fist, the
  approved Ajax's 24 deg the same way.
- **Kyubi**: the M4 way -- palm over the receiver, index and middle hooked round the T's front either side of its
  stem (`kyubi_handle_left`, built: hooks 20/95/55 deg, ring/little folded), palm-down rotation, the wrist placed so
  pads 65/66 stand 6 mm in front of the T at its height. Pictured from the right and from above (`handleview.py
  pitch=1.2 h=-0.20`).
- **Grad**: bolt preview radius 0.05 -> 0.08 (the grip's wrist point is 122 mm from the handle).
