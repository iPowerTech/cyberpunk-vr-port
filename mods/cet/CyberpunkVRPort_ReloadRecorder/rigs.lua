-- KNOWN WEAPON RIG SIGNATURES, as data. A rig is recognised by its bone COUNT plus named bones at their indices;
-- the plugin hashes the names itself (FNV1a64 = the CName hash).
--
-- This lives in Lua rather than in the plugin because of a chicken-and-egg: a rig must be identified before its
-- bones can be RECORDED, and a weapon's reload config is written FROM that recording -- so identification cannot
-- wait for the config. It is still data, in a file anyone can extend, and the recorder and the reload module both
-- register the same list.
--
-- which 0 = the magazine rig, 1 = the frame rig. Indices are rig bone indices (the GLB's, minus the Armature node).
return {
    -- Malorian Silverhand: 16-bone frame, two sliders
    { which = 1, bones = 16, names = { { 5, 'front_slider' }, { 6, 'back_slider' }, { 8, 'mag_slot' } } },
    -- Constitutional Unity: 11-bone frame, ONE `slide`
    { which = 1, bones = 11, names = { { 5, 'slide' }, { 8, 'mag_slot' }, { 6, 'hammer' } } },
    -- Arasaka Kenshin: a TECH pistol, and the first rig in this set that is not the 11-bone family -- 10 bones,
    -- no `slider_down`, so `mag_slot` sits at 7 rather than 8. Registered before any recording of it, because a
    -- rig has to be identified for its bones to be recorded at all.
    { which = 1, bones = 10, names = { { 5, 'slide' }, { 7, 'mag_slot' }, { 6, 'hammer' } } },
    -- Militech Omaha: TEN bones like the Kenshin's and a different rig again -- `barrel_front` (3) is the piece the
    -- hand works, 51.0 mm of travel, and `barrel_middle` (2) is the long top part that rides 18.4 with it. The two
    -- are SIBLINGS under `barrel`, not nested, so each is written on its own. This is the second weapon to lean on
    -- the signature-table fix: on bone count alone it is indistinguishable from the Kenshin.
    { which = 1, bones = 10, names = { { 3, 'barrel_front' }, { 2, 'barrel_middle' }, { 6, 'mag_slot' } } },
    -- Militech Lexington: ELEVEN bones like the Unity family, and not that family at all -- different names in a
    -- different order, and no bone called `slide` anywhere. Its moving parts are `barrel_back` (the slide the hand
    -- racks, 108 mm of geometry) and `barrel_top` (a smaller piece riding on top). This rig is why a signature
    -- carries NAMES: on the bone count alone it would have been read as a Unity and driven on the wrong bones.
    { which = 1, bones = 11, names = { { 3, 'barrel_back' }, { 4, 'barrel_top' }, { 10, 'mag_slot' } } },
    -- Kang Tao Chao: TWELVE bones, and the first rig in the set with NO SLIDE of any kind. Nothing on this weapon
    -- translates in any clip -- the moving part is `magazine_open` (8), the whole upper section, which ROTATES 90 deg
    -- about the bore to expose the magazine. `spinner` (9) and `trigger` (10) carry meshes but never move in a
    -- reload. Its own 5-bone magazine rig needs no entry of its own: mag_std at 3 and mag_stdr at 4 is the signature
    -- already registered below, and this rig matches it exactly.
    { which = 1, bones = 12, names = { { 8, 'magazine_open' }, { 11, 'mag_slot' }, { 9, 'spinner' } } },
    -- Arasaka Yukimura, and the Tsunami Kappa which borrows the whole thing: EIGHT bones, the smallest rig in the
    -- set. The Kappa has no rig and no anims of its own -- its .app, its appearance package and its .ent all name
    -- the Yukimura's -- so one signature covers both weapons, and each config decides for itself what to drive.
    -- `slider` (7) is the front piece: 17.0 mm of travel, back on an empty gun. It carries geometry on the Yukimura
    -- and NONE on the Kappa, whose six body meshes skin to `barrel` and `gun_trigger` only.
    { which = 1, bones = 8, names = { { 7, 'slider' }, { 6, 'mag_slot' }, { 5, 'gun_trigger' } } },
    -- Militech Ticon (Phantom Liberty): ELEVEN bones, the third rig in the set with that count and unlike either of
    -- the others. TWO large moving parts -- `middle_slider` (3), the front shroud, and `end_slider` (5), the whole
    -- upper -- and the muzzle slots are CHILDREN of the front one, so on this weapon the muzzle moves. Its magazine
    -- rig is the first with SIX bones (an extra `bullet_rotator`), which makes it unambiguous on count alone.
    { which = 1, bones = 11, names = { { 3, 'middle_slider' }, { 5, 'end_slider' }, { 6, 'mag_slot' } } },
    { which = 0, bones = 6,  names = { { 1, 'magazine' }, { 3, 'mag_std' }, { 5, 'bullet_rotator' } } },
    -- Malorian Overture, the first REVOLVER: ten frame bones, and every moving part turns rather than slides.
      -- `magazine_open` (4) is the CRANE -- it swings the cylinder 100 deg out of the frame -- `cylinder` (8) is its
      -- child and spins about its own axis (57.7 deg is one chamber of six, 177.7 is three), and `mag_slot` (9) is a
      -- child of the CYLINDER, so the speedloader's mount rides both. `hammer` (3) cocks 37-40 deg.
      -- Ten bones is taken twice already (the Unity family and the Lexington), so this one leans on its names.
      { which = 1, bones = 10, names = { { 4, 'magazine_open' }, { 8, 'cylinder' }, { 3, 'hammer' } } },
      -- ...and its speedloader: ELEVEN bones, six of them `bullet_01..06` on a 13.4 mm circle -- the six chambers.
      { which = 0, bones = 11, names = { { 1, 'magazine' }, { 3, 'bullet_01' }, { 2, 'reload_magazine' } } },
      -- The 5-bone magazine rig is shared: both weapons name mag_std at 3 and mag_stdr at 4
    { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
    -- ...and the SHARED handgun magazine rig, base\weapons\firearms\handgun\rig\w_handgun__mag_std.rig, which the
    -- Lexington uses instead of carrying its own. The same five bones in a DIFFERENT order: `magazine` is 2 rather
    -- than 1, and mag_std / mag_stdr are swapped (4 and 3). Its two float tracks are the same pair in the same
    -- order, so `showTrack = 0` still holds.
    { which = 0, bones = 5,  names = { { 2, 'magazine' }, { 4, 'mag_std' }, { 3, 'mag_stdr' } } },
    -- ------------------------------------------------------------------ THE SHOTGUNS
    -- Read off the .rig assets (build/shotgun-reload-20261019/rigs.txt), registered before any config so the
    -- recorder can see their bones. Every break-action barrel hinges at the same place -- `barrel_hinge` (2), 161 mm
    -- up the bore -- and every frame bone past it rides the hinge, the chambers (`mag_slot`) included.
    --
    -- Rostovic Satara: 36 bones, twelve of them the rail magnets that fan out when it breaks.
    { which = 1, bones = 36, names = { { 2, 'barrel_hinge' }, { 21, 'mag_slot' }, { 9, 'l_magnet_rail_01_bottom' } } },
    -- Rostovic Palica: 13 bones, `hinge_open` (7) is the cover over the hinge.
    { which = 1, bones = 13, names = { { 2, 'barrel_hinge' }, { 7, 'hinge_open' }, { 12, 'mag_slot' } } },
    -- Rostovic Testera: 9 bones, `barrel_opener` (3) is its side latch.
    { which = 1, bones = 9,  names = { { 2, 'barrel_hinge' }, { 3, 'barrel_opener' }, { 8, 'mag_slot' } } },
    -- Rostovic Igla: 8 bones -- the Yukimura's count, told apart by name.
    { which = 1, bones = 8,  names = { { 2, 'barrel_hinge' }, { 3, 'muzzle_slot' }, { 7, 'mag_slot' } } },
    -- The Satara's shells, which the Igla and the Testera borrow whole: the two seated (`l_/r_bullet`) and the two
    -- the hand brings (`*_reload`), on tracks showMagazineLeft / ReloadLeft / Right / ReloadRight.
    { which = 0, bones = 5,  names = { { 1, 'l_bullet' }, { 2, 'r_bullet' }, { 3, 'l_bullet_reload' } } },
    -- ...and the Palica's own, nine bones: a magazine and a reload magazine per barrel.
    { which = 0, bones = 9,  names = { { 1, 'left_magazine' }, { 5, 'left_mag_std' }, { 8, 'right_mag_std' } } },
    -- THE RECEIVER, the third rig (which = 2): the stock and action every Rostovic shares, w_shotgun_dual__rcv1.
    -- `barrel_open` (5) is the top lever the thumb pushes over to break the gun.
    { which = 2, bones = 6,  names = { { 5, 'barrel_open' }, { 4, 'safeLock' }, { 2, 'trigger' } } },
    -- Budget Carnage: 15 bones, the pump is `cocking_handle` (4) and `flap_open` (10) the loading gate.
    { which = 1, bones = 15, names = { { 4, 'cocking_handle' }, { 10, 'flap_open' }, { 5, 'mag_slot' } } },
    -- Constitutional Tactician: 13 bones like the Palica, `foreend` (9) the pump, `lifter` (8) the carrier.
    { which = 1, bones = 13, names = { { 9, 'foreend' }, { 8, 'lifter' }, { 2, 'mag_slot' } } },
    -- (Both pumps' shell rigs are the shared 5-bone mag_std / mag_stdr signature above.)
    --
    -- THE BOX-MAGAZINE SHOTGUNS, driven by reload.lua like the pistols (rigs read off the .rig assets in
    -- build/shotgun-reload-20261019/json3; both magazine rigs are the shared 5-bone signature above).
    -- Militech Crusher: 14 bones, the charging handle `reload_handle` (5) on the LEFT of the receiver and the
    -- magazine catch `magazine_lock` (11) in front of the well.
    { which = 1, bones = 14, names = { { 5, 'reload_handle' }, { 10, 'mag_slot' }, { 11, 'magazine_lock' } } },
    -- Kang Tao Zhuo (and the Ba Xing Chong, its iconic): 21 bones, eight barrels on `barrel_cocking` (2). No
    -- `mag_slot` on this rig -- the magazine sits in the RECEIVER's.
    { which = 1, bones = 21, names = { { 2, 'barrel_cocking' }, { 3, 'targeting_system' }, { 4, 'l_pin' } } },
    -- ...and its receiver, w_special__kangtao_zhuo__rcv1: 19 bones, `mag_slot` (2) with the two pairs of
    -- magazine holders either side of it (3..6) and their clips (10..17).
    { which = 2, bones = 19, names = { { 2, 'mag_slot' }, { 3, 'l_mag_holder_01' }, { 4, 'r_mag_holder_01' } } },
    -- Techtronika Pozhar (filed under rifle_precision, w_rifle_precision__techtronika_pozhar): 16 bones, a box
    -- magazine on the shared 5-bone rig, the charging handle `cocking_device` (10) on the RIGHT of the receiver with
    -- its two pins, `slider` (9) the front shroud, and `bullet` (12) the round in the chamber (track showBullet).
    { which = 1, bones = 16, names = { { 9, 'slider' }, { 10, 'cocking_device' }, { 12, 'bullet' } } },
    -- ------------------------------------------------------------------ THE LAST PISTOLS (2026-10-20)
    -- Read off the .rig assets (build/pistols-20261020/rigs.txt) before any recording; every magazine rig among
    -- them is the shared 5-bone mag_std / mag_stdr one above.
    -- Krausser Grit (Phantom Liberty): 14 bones like the Crusher, told apart by name -- a magazine cover in two
    -- halves (`r_/l_magazine_open`, 7 / 8) and `chamber_pull` (12).
    { which = 1, bones = 14, names = { { 7, 'r_magazine_open' }, { 12, 'chamber_pull' }, { 13, 'mag_slot' } } },
    -- Darra Nova: 11 bones, no cylinder at all -- `top_button` (8) and `front_button` (9), the magazine under the
    -- barrel (`mag_slot` 117 mm up the bore).
    { which = 1, bones = 11, names = { { 8, 'top_button' }, { 9, 'front_button' }, { 5, 'mag_slot' } } },
    -- Darra Quasar: 12 bones, `button_01` (8) and two switches, `mag_slot` (5) at the back and turned 45 deg.
    { which = 1, bones = 12, names = { { 8, 'button_01' }, { 9, 'switch_01' }, { 5, 'mag_slot' } } },
    -- Techtronika Burya: 10 bones, `slider` (6) the top piece the scope rides, `reload_button` (7).
    { which = 1, bones = 10, names = { { 6, 'slider' }, { 7, 'reload_button' }, { 8, 'mag_slot' } } },
    -- Techtronika Metel (Phantom Liberty): 15 bones, `chamber` (6), `slider` (11) and two hinges (12 / 13).
    { which = 1, bones = 15, names = { { 6, 'chamber' }, { 11, 'slider' }, { 12, 'back_hinge' } } },
    -- ------------------------------------------------------------------ THE RIFLES (2026-10-20)
    -- Read off the .rig assets (build/rifles-20261020/rigs.txt) before any recording. Every magazine rig among them is
    -- the shared 5-bone mag_std / mag_stdr one above except the Ashura's (below); the Ajax and the Kyubi share one frame
    -- rig, the Copperhead and the Umbra another -- a signature names a RIG, the config is matched by the gun's name.
    -- Arasaka Masamune: 18 bones, `cocking_slider` (6) with two pairs of riders (14..17), `reload_chamber` (12).
    { which = 1, bones = 18, names = { { 6, 'cocking_slider' }, { 12, 'reload_chamber' }, { 5, 'mag_slot' } } },
    -- Militech Ajax / Tsunami Kyubi: 18 bones, `slider` (11), the catch `magazine_lock` (15), `mag_slot` (14).
    { which = 1, bones = 18, names = { { 11, 'slider' }, { 15, 'magazine_lock' }, { 14, 'mag_slot' } } },
    -- Techtronika Grad: 18 bones, `chamber_opener` (7) the bolt, `magazine_opener` (11).
    { which = 1, bones = 18, names = { { 7, 'chamber_opener' }, { 11, 'magazine_opener' }, { 5, 'mag_slot' } } },
    -- Nokota Copperhead / Darra Umbra: 12 bones, `reload_lever` (2) the charging handle on its pivot (11).
    { which = 1, bones = 12, names = { { 2, 'reload_lever' }, { 3, 'counter_mass' }, { 7, 'mag_slot' } } },
    -- Nokota Sidewinder: 13 bones, the Copperhead's lever plus `magazine_opener` (6) and two cables.
    { which = 1, bones = 13, names = { { 2, 'reload_lever' }, { 6, 'magazine_opener' }, { 9, 'mag_slot' } } },
    -- Militech Achilles: 13 bones, no bolt -- `barrel_movement_base` (2) with the rails on it, three rings.
    { which = 1, bones = 13, names = { { 2, 'barrel_movement_base' }, { 4, 'ring' }, { 6, 'mag_slot' } } },
    -- ...and its receiver, w_rifle_precision__rcv1 (the `Receiver` component): 9 bones, the magazine cover
    -- `magazine_open` (4) that swings up over the well, its lock (6) and `magazine_absorber` (7).
    { which = 2, bones = 9,  names = { { 4, 'magazine_open' }, { 6, 'magazine_open_lock' }, { 7, 'magazine_absorber' } } },
    -- Nokota Osprey (Phantom Liberty): 13 bones, `cocking_mechanism` (5), `magazine_release` (7).
    { which = 1, bones = 13, names = { { 5, 'cocking_mechanism' }, { 7, 'magazine_release' }, { 10, 'mag_slot' } } },
    -- Tsunami Rasetsu (Phantom Liberty): 13 bones, a magazine under a cover (`magazine_cover` 6 .. `magazine_rotator` 11).
    { which = 1, bones = 13, names = { { 6, 'magazine_cover' }, { 8, 'magazine_base' }, { 12, 'mag_slot' } } },
    -- Midnight Arms SOR-22: 14 bones, `cocking_handle` (5), `magazine_open` (7), the well turned on the side (8).
    { which = 1, bones = 14, names = { { 5, 'cocking_handle' }, { 7, 'magazine_open' }, { 8, 'mag_slot' } } },
    -- Tsunami Ashura: 14 bones, `cocking_mechanism` (5), `hammer` (10)...
    { which = 1, bones = 14, names = { { 5, 'cocking_mechanism' }, { 10, 'hammer' }, { 13, 'mag_slot' } } },
    -- ...and its own 3-bone magazine rig, with no reload copy of the magazine
    { which = 0, bones = 3,  names = { { 1, 'magazine' }, { 2, 'mag_std' } } },
    -- Rostovic Kolac: 19 bones, `cocking_handle` (6), the round `bullet` (13), `mag_slot` (16).
    { which = 1, bones = 19, names = { { 6, 'cocking_handle' }, { 13, 'bullet' }, { 16, 'mag_slot' } } },
    -- Tsunami Nekomata: 72 bones (55 of them the rails' magnets), `cocking_mechanism` (5), `back_bolt` (14).
    { which = 1, bones = 72, names = { { 5, 'cocking_mechanism' }, { 12, 'mag_slot' }, { 14, 'back_bolt' } } },
    -- Militech Hercules (Phantom Liberty, filed under `special`, an LMG receiver): 10 bones, `magazine_grabber` (7),
    -- the front `handle` (8), `reload_mechanism` (9) on top at the back...
    { which = 1, bones = 10, names = { { 7, 'magazine_grabber' }, { 8, 'handle' }, { 9, 'reload_mechanism' } } },
    -- ...and its 10-bone box: `mag_std` (3), the belt and its hinge (4 / 5), `mag_stdr` (6)
    { which = 0, bones = 10, names = { { 3, 'mag_std' }, { 4, 'belt_open' }, { 6, 'mag_stdr' } } },
    -- ------------------------------------------------------------------ THE SMGS, LMGS, AUTHORITY (2026-10-25)
    -- Read off the .rig assets (build/smg-20261025, rigdump.py) before any recording. Their magazine rigs are the
    -- shared 5-bone mag_std / mag_stdr one above except the Defender's drum and the Authority's two-shell clip.
    -- Arasaka Shingen: 15 bones, `cocking_device` (6) on the right, `magazine_open` (7), `magazine_lock` (9).
    { which = 1, bones = 15, names = { { 6, 'cocking_device' }, { 7, 'magazine_open' }, { 8, 'mag_slot' } } },
    -- Krausser Warden (Phantom Liberty): the Shingen's first eleven bones, nothing else -- told apart by the count.
    { which = 1, bones = 11, names = { { 6, 'cocking_device' }, { 7, 'magazine_open' }, { 8, 'mag_slot' } } },
    -- Militech Saratoga / Budget Guillotine: one 16-bone rig, `magazine_open` (4), `cocking` (5) at the front.
    { which = 1, bones = 16, names = { { 4, 'magazine_open' }, { 5, 'cocking' }, { 9, 'mag_slot' } } },
    -- Darra Pulsar: 12 bones, `cocking_handle` (6) on top at the back, `front_handle` (5), the well behind the grip.
    { which = 1, bones = 12, names = { { 6, 'cocking_handle' }, { 5, 'front_handle' }, { 4, 'mag_slot' } } },
    -- Midnight Borg (Phantom Liberty): 10 bones, `cocking_handle` (6), `magazine_release` (7), well behind the grip.
    { which = 1, bones = 10, names = { { 6, 'cocking_handle' }, { 7, 'magazine_release' }, { 9, 'mag_slot' } } },
    -- Arasaka Senkoh LX: 16 bones, `slider` (9) under the front, `front_part` (8), dangling hinges (10..15).
    { which = 1, bones = 16, names = { { 9, 'slider' }, { 8, 'front_part' }, { 6, 'mag_slot' } } },
    -- Kang Tao Dian (filed under `special`): a 9-bone frame with the `charging_handle` (3)...
    { which = 1, bones = 9,  names = { { 3, 'charging_handle' }, { 8, 'barrel_muzzle' }, { 6, 'r_blade_01' } } },
    -- ...and a 14-bone receiver: `magazine_open` (6) on the safe lock, `magazine_rail` (7) carrying `mag_slot` (11).
    { which = 2, bones = 14, names = { { 6, 'magazine_open' }, { 7, 'magazine_rail' }, { 11, 'mag_slot' } } },
    -- Constitutional Arms Defender: 22 bones, `cocking_handle` (7), `magazine_rotator` (11), a bipod (5 / 6)...
    { which = 1, bones = 22, names = { { 7, 'cocking_handle' }, { 11, 'magazine_rotator' }, { 13, 'mag_slot' } } },
    -- ...and its 23-bone drum: `magazine_opener` (2) with the belt's rounds (3..22)
    { which = 0, bones = 23, names = { { 1, 'magazine_base' }, { 2, 'magazine_opener' }, { 3, 'bullet_match_01' } } },
    -- Krausser Authority (Phantom Liberty, a grenade launcher): 9 bones, the breech on a `hinge` (2), `weapon_opener`
    -- (7) on it...
    { which = 1, bones = 9,  names = { { 2, 'hinge' }, { 7, 'weapon_opener' }, { 8, 'mag_slot' } } },
    -- ...and a 7-bone clip of two shells (`mag_std_01` / `_02`, each with its reload copy)
    { which = 0, bones = 7,  names = { { 1, 'magazine_01' }, { 2, 'magazine_02' }, { 3, 'mag_std_01' } } },

}
