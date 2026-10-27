-- Kang Tao Zhuo, and the Ba Xing Chong (Items.Preset_Zhuo_Eight_Star, its `star_cannon` appearance) — physical-reload
-- configuration.
--
-- A SMART SHOTGUN WITH A BOX MAGAZINE, reload.lua's like the Crusher. Read off four takes of its own reload with VRIK
-- off (build/shotgun-reload-20261019/takes/zhuo_*; boxmag.py, boxsum.py, parts.py, timeline.py there).
--
--   * THREE RIGS: the frame (21 bones -- eight barrels on `barrel_cocking`, and `targeting_system`, which spins in
--     every clip and is left alone), the shared 5-bone magazine rig, and the RECEIVER (19 bones), where the magazine
--     actually sits: `mag_slot` between two pairs of holders. The frame has no `mag_slot`, so the appearance's
--     `vrp_mag_slot` is the receiver's, carried onto the frame's slot component at its rest place (0, 245.4, -21.2)
--     mm -- both rigs hang off the entity's origin. The magazine is drawn there, 25 cm ahead of the grip.
--   * NO HANDLE AND NO CHAMBER TO WORK: a magazine in is a loaded gun.
--   * THE MAGAZINE IS HELD BETWEEN HOLDERS, not latched in a well. The reload slides the barrel block 38.5 mm
--     forward, spreads the holders 16.5 mm each way and swings their clips out; the hand brings the fresh magazine
--     up under them and LETS GO about 90 mm short, and the holders, closing, pull it the rest of the way (t=3.18..3.36,
--     while they shut at 3.22..3.31). Hence `hatch.parts` -- the whole opening, keyed to the magazine being out --
--     and `selfSeat` with `shutOnGlide`.
return {
    id    = 'kangtao_zhuo',
    match = 'kangtao_zhuo',

    -- pwa_w_special__kangtao_zhuo.anims. `mag_open` is the opening itself (frame 0 of every reload); `move` is heard
    -- as the barrels come home (frame 95).
    sounds = {
        magOut       = 'w_gun_shotgun_smart_zhuo_mag_out',
        magIn        = 'w_gun_shotgun_smart_zhuo_mag_in',
        magGrab      = 'w_gun_shotgun_smart_zhuo_mag_new_grab',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
        { which = 1, bones = 21, names = { { 2, 'barrel_cocking' }, { 3, 'targeting_system' }, { 4, 'l_pin' } } },
        { which = 2, bones = 19, names = { { 2, 'mag_slot' }, { 3, 'l_mag_holder_01' }, { 4, 'r_mag_holder_01' } } },
    },

    -- The bore: `barrel` to the middle of the eight muzzles (0, 473.9, -61.5) mm, 3.8 deg off the weapon's Y.
    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    -- THE OPENING, every part with what it does at full open and the stretch of the opening it does it in. The
    -- opening's clock is the animation's, 0.89 .. 1.33 s of zhuo_one_a, and each part's stretch is where it went from
    -- 10 to 90 % of its travel on that clock (the empty takes agree). Parent-local, as the rig writes take them.
    -- `which`/`bone` name the part the diagnostic line reads back.
    hatch = {
        which = 1, bone = 2,
        openTime  = 0.44,
        closeTime = 0.54,      -- 3.22 .. 3.76: holders, clips, then the barrels last
        shutOnGlide = true,
        sounds = { open = 'w_gun_shotgun_smart_zhuo_mag_open', shut = 'w_gun_shotgun_smart_zhuo_move' },
        parts = {
            -- the barrel block, 38.47 mm forward, and the pin that rides it back 19.76
            { which = 1, bone = 2,  move = { 0.0, 0.0,  0.03847 }, from = 0.00, to = 0.86 },
            { which = 1, bone = 4,  move = { 0.0, 0.0, -0.01976 }, from = 0.00, to = 0.66 },
            -- the holders, spreading
            { which = 2, bone = 3,  move = {  0.01647, 0.0, 0.0 }, from = 0.05, to = 0.61 },
            { which = 2, bone = 4,  move = { -0.01721, 0.0, 0.0 }, from = 0.05, to = 0.61 },
            -- the middle clips swing about a pivot off their origin: a turn and the shift it makes
            { which = 2, bone = 13, move = {  0.00847, -0.01147, 0.0 }, turn = 45.1, axis = { 0, -1, 0 }, from = 0.05, to = 0.45 },
            { which = 2, bone = 17, move = { -0.00846,  0.01145, 0.0 }, turn = 45.1, axis = { 0,  1, 0 }, from = 0.05, to = 0.45 },
            -- the holders' own clips, snapping out
            { which = 2, bone = 10, turn = 75.6, axis = { 0, 0, 1 }, from = 0.55, to = 0.66 },
            { which = 2, bone = 14, turn = 82.8, axis = { 0, 0, 1 }, from = 0.55, to = 0.66 },
            -- and the front and back clips, last
            { which = 2, bone = 11, turn = 35.7, axis = { 0, 0, 1 }, from = 0.90, to = 1.00 },
            { which = 2, bone = 12, turn = 35.7, axis = { 0, 0, 1 }, from = 0.90, to = 1.00 },
            { which = 2, bone = 15, turn = 35.7, axis = { 0, 0, 1 }, from = 0.90, to = 1.00 },
            { which = 2, bone = 16, turn = 35.7, axis = { 0, 0, 1 }, from = 0.90, to = 1.00 },
        },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        showTrack  = 0,
        mesh       = 'base\\weapons\\firearms\\special\\kangtao_zhuo\\entities\\meshes\\w_special__kangtao_zhuo__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_zhuo.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- 78.2 x 108.3 x 122.2 mm: thin across X still, but nearly square -- a block more than a stick.
        drop = {
            centre  = { 0.0, -0.0007, -0.0426 },
            flat    = 0.0391,
            arm     = 0.022,
            probe   = 0.0391,
            inertia = 0.0022,
        },

        -- THE GAME'S HOLD, one relation from under the gun to the holders: rigid over t=2.18..3.02 (39 frames, 1.9 mm and
        -- 0.8 deg), three of the four takes within 0.3 mm / 0.05 deg of each other. (The fourth held it 40 deg
        -- differently for its whole carry; one way of holding it is plenty.)
        -- ...AND NOT A HOLD A VR HAND CAN USE: from there the magnet turned the wrist 106.8 deg at the holders, and no
        -- turn of a 78 x 108 x 122 block in that same hand comes nearer than 24 deg without leaving the fingers. So
        -- the hand is the CRUSHER's (base at the little finger, the box up past the thumb, 39 deg from a VR hand
        -- here) with its finger poses, the block moved (23.3, 10.5, -56.1) mm in its own frame so its -X face is on
        -- the same palm, its +Y face under the same fingertips and its base on the same heel (tools/borrow_hold.py).
        -- Was the take's own: holdOffset { 0.07775, 0.01755, -0.03590 }, holdRotBasis { 0.37236, 0.53627, 0.50669, 0.56306 }.
        holdOffset   = { 0.03000, 0.02072, -0.00798 },
        holdRotBasis = { -0.24320, 0.00253, 0.07919, 0.96673 },

        snapRadius     = 0.050,
        seatSnapRadius = 0.075,
        armDist        = 0.070,
        snapGain       = 1.6,
        seatRadius     = 0.03,
        gravity        = 9.81,
        litterTime     = 3.0,
        holdWiden      = 2.2,
        insertGain     = 6.0,
        dropPush       = 1.1,

        -- STRAIGHT UP INTO THE HOLDERS: the last 100 mm of every take lie on the weapon's -Z to under 3 mm.
        wellAxis   = { 0.0035, -0.0003, -1.0000 },
        -- The hand lets go 88..95 mm below the seat, so the click is at 90 mm: 0.40 of a 150 mm run ...
        insertRun  = 0.150,
        seatAt     = 0.40,
        -- ... and the holders take it home in 0.18 s (95.5 mm at t=3.18, 48.5 at 3.31, 0.9 at 3.36).
        selfSeat   = { time = 0.18 },

        poseStages = {
            { d = 0.150, off = { 0.03000, 0.02072, -0.00798 }, rot = { -0.24320, 0.00253, 0.07919, 0.96673 },
                         pose = 'crusher_mag_left_carry' },
            { d = 0.095, off = { 0.03000, 0.02072, -0.00798 }, rot = { -0.24320, 0.00253, 0.07919, 0.96673 },
                         pose = 'crusher_mag_left_seat' },
        },
        pose           = 'crusher_mag_left_seat',
    },
}
