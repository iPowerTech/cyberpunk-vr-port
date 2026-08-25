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
}
