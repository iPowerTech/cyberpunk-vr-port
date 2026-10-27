-- Militech Ajax — physical-reload configuration. The Tsunami Kyubi is the same frame rig, magazine rig and hand
-- (tsunami_kyubi.lua).
--
-- A curved box magazine (27 x 212 x 202 mm, 30 rounds) rocked in from the front, like an AK's. Read off four takes
-- with VRIK off (build/shotgun-reload-20261019/takes/ajax_*; the analysis in build/rifles-20261020).
--
--   * The charging handle FOLDS: `reload_hinge` (16), a child of `reload_front` (8) on the LEFT of the receiver,
--     swings out 90 deg about its own Y, then `reload_front` comes back 85.0 mm with the bolt `reload_mechanism` (9)
--     riding it 48.0 mm, and both fly home and the handle folds away. Only the EMPTY reload works it, and nothing holds
--     it back -- the rounds go in with the rack (`chamberOnRack = 'dry'`).
--   * The hinge is a rider that only turns, and it is out by the first 5 mm of the pull (`turnAt`): the take has it
--     open 0.05 s before the handle moves.
--   * Two-handed: the handle on the LEFT TRIGGER, the magazine rocked out by hand (`handPull`).
return {
    id    = 'militech_ajax',
    match = 'militech_ajax',

    -- pwa_w_rifle_assault__militech_ajax.anims
    sounds = {
        slidePull    = 'w_gun_rifle_power_ajax_handle_pull',
        slideRelease = 'w_gun_rifle_power_ajax_handle_release',
        magOut       = 'w_gun_rifle_power_ajax_mag_out',
        magIn        = 'w_gun_rifle_power_ajax_mag_in',
        magGrab      = 'w_gun_rifle_power_ajax_mag_new_grab',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
        { which = 1, bones = 18, names = { { 11, 'slider' }, { 15, 'magazine_lock' }, { 14, 'mag_slot' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    slide = {
        slot       = 'vrp_reload_front',
        which      = 1, bone = 8,
        travel     = 0.0850,
        restLocal  = { 0.00000, 0.03540, 0.19040 },
        localAxis  = { 0, 0, -1 },
        sign       = 1.0,
        -- 85 mm in 0.045 s back home (t=3.69..3.76 of ajax_empty_a)
        releaseSpeed = 1.50,
        gripSnap   = true,
        previewRadius = 0.05,
        blendTime  = 0.15,
        pullSndAt  = 0.20,
        styleAngleMax = 180.0,
        grabWith   = 'trigger',
        -- THE CASE out of the port once the handle is this far back with a round in the chamber: our own effect
        -- on the gun's port slot, the case alone (build/rifles-20261020/make_case_fx.py)
        ejectAt    = 0.60,
        ejectFx    = 'vrp_eject_case',
        -- THE LEFT HAND ON THE UNFOLDED HANDLE while it pulls and holds it, ajax_empty_a t=3.48..3.63 (11.8 mm /
        -- 5.6 deg; the Kyubi's take puts it within 4 mm of this): left of the receiver, below the handle.
        grips = {
            behind = { off = { -0.0735, -0.0426, -0.0385 },
                       rot = { -0.3989, 0.2802, -0.5178, -0.7030 },
                       pose = 'ajax_handle_left' },
        },
        riders = {
            { bone = 9,  ratio = 0.565 },
            { bone = 16, ratio = 0.0, turn = 90.0, axis = { 0, 1, 0 }, turnAt = 0.005 },
        },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        -- TRACK 0: a rifle's magazine rig idles at 1, 0 and the game's own reload drops index 0 while the magazine
        -- is out (the takes' `mt`); index 1 is the reload magazine. Held at 1, the gun kept its magazine after a drop.
        showTrack  = 0,
        mesh       = 'base\\weapons\\firearms\\rifle_assault\\militech_ajax\\entities\\meshes\\w_rifle_assault__militech_ajax__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_ajax.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 26.7 x 211.5 x 202.2 mm -- a curve, as long along Y as along Z
        drop = {
            centre  = { 0.0, 0.0703, -0.0876 },
            flat    = 0.0134,
            long    = { 0, 1, 0 },
            thin    = { 1, 0, 0 },
            arm     = 0.0924,
            probe   = 0.0134,
            inertia = 0.00713,
        },

        -- THE HOLD, ajax_one_a / one_b / empty_b: rigid over 19-20 frames (0.1 mm, 0.4-0.7 deg), the three the same to
        -- 0.3 mm. (empty_a is the other of the bank's two holds, 50 mm further up the magazine.)
        holdOffset   = { -0.00476, 0.03099, 0.02503 },
        holdRotBasis = { -0.47855, -0.06154, 0.03470, 0.87521 },

        handPull       = true,
        pullTrigger    = true,
        -- THE SUPPORT HAND ON THE MAGAZINE, as on the Pozhar (2026-10-26: grip alone holds the gun by the magazine,
        -- grip + trigger takes the magazine out): a second two-hand hold, CyberpunkVR_TwoHandGrip_ajax@mag.ini
        -- (build/maghold-20261026/mag_holds.py: this gun's fitted carry round the seated magazine) -- hold 1,
        -- taken away with no magazine in the well.
        holdAlt        = 1,

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

        -- IN FROM BELOW AND IN FRONT, the curve's own line: the eight Ajax and Kyubi fits put Y 0.35..0.66, Z
        -- -0.75..-0.94 and X either side of zero (the rock-in, 11-16 mm residuals); this is their middle.
        wellAxis   = { 0.0, 0.5197, -0.8543 },
        insertRun  = 0.080,
        seatAt     = 0.70,
        chamberOnRack = 'dry',

        poseStages = {
            { d = 0.080, off = { -0.00476, 0.03099, 0.02503 }, rot = { -0.47855, -0.06154, 0.03470, 0.87521 },
                         pose = 'ajax_mag_left_carry' },
            { d = 0.030, off = { -0.00476, 0.03099, 0.02503 }, rot = { -0.47855, -0.06154, 0.03470, 0.87521 },
                         pose = 'ajax_mag_left_seat' },
        },
        pose           = 'ajax_mag_left_seat',
    },
}
