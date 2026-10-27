-- Arasaka Masamune — physical-reload configuration.
--
-- The first ASSAULT RIFLE: a box magazine (28 x 119 x 194 mm, 24 rounds) on the shared 5-bone rig, and a charging
-- handle on top that locks back on an empty magazine. Read off four takes with VRIK off
-- (build/shotgun-reload-20261019/takes/masamune_*; the analysis in build/rifles-20261020).
--
--   * `cocking_slider` (6) is the handle, on top and well forward, its two wings out either side (14..17). Empty, the
--     gun holds it 65.0 mm back; the empty reload ends with the left hand pulling it 4 mm further and letting it fly
--     home in 0.1 s (`handle_release`). So it is a slide stop -- the hand (or R3) releases it. The reload with rounds
--     left never touches it.
--   * `reload_chamber` (12) rides it, 100 mm against its 65 (1.54).
--   * Two-handed, so the handle is worked on the LEFT TRIGGER like the Crusher's (a grip there is the two-hand hold),
--     and the magazine is rocked out by hand (`handPull`).
return {
    id    = 'arasaka_masamune',
    match = 'arasaka_masamune',

    -- pwa_w_rifle_assault__arasaka_masamune.anims
    sounds = {
        slidePull    = 'w_gun_rifle_power_masamune_handle_pull',
        slideRelease = 'w_gun_rifle_power_masamune_handle_release',
        magOut       = 'w_gun_rifle_power_masamune_mag_out',
        magIn        = 'w_gun_rifle_power_masamune_mag_in',
        magGrab      = 'w_gun_rifle_power_masamune_mag_new_grab',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
        { which = 1, bones = 18, names = { { 6, 'cocking_slider' }, { 12, 'reload_chamber' }, { 5, 'mag_slot' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    slide = {
        slot       = 'vrp_cocking_slider',
        which      = 1, bone = 6,
        -- home at Z +206.5, on the stop at +141.5 (both empty takes to 0.1 mm), the release nudge 4 mm deeper
        travel     = 0.0700,
        restLocal  = { 0.00000, 0.08660, 0.20650 },
        lockLocal  = { 0.00000, 0.08660, 0.14150 },
        localAxis  = { 0, 0, -1 },
        sign       = 1.0,
        -- 69 mm in 0.09 s at the release (t=3.72..3.81 of masamune_empty_a)
        releaseSpeed = 1.00,
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
        -- THE LEFT HAND AT THE NUDGE, masamune_empty_b t=3.70..3.77 (8.6 mm / 7.4 deg): under and left of the handle,
        -- reaching over the receiver. The animation slaps it rather than holding it (the other empty take is 60 mm off
        -- this one), so this is the closer of the two.
        grips = {
            behind = { off = { -0.0537, -0.0683, -0.0423 },
                       rot = { 0.2398, 0.6231, -0.2922, -0.6847 },
                       pose = 'copperhead_handle_left' },
        },
        riders = {
            { bone = 12, ratio = 1.538, abs = true },
        },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        -- TRACK 0: a rifle's magazine rig idles at 1, 0 and the game's own reload drops index 0 while the magazine
        -- is out (the takes' `mt`); index 1 is the reload magazine. Held at 1, the gun kept its magazine after a drop.
        showTrack  = 0,
        mesh       = 'base\\weapons\\firearms\\rifle_assault\\arasaka_masamune\\entities\\meshes\\w_rifle_assault__arasaka_masamune__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_masamune.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 28.0 x 119.2 x 193.8 mm, long along its Z
        drop = {
            centre  = { 0.0, -0.0187, -0.0885 },
            flat    = 0.0140,
            arm     = 0.0829,
            probe   = 0.0140,
            inertia = 0.00432,
        },

        -- THE HOLD, masamune_one_a / masamune_empty_a: rigid over 25 / 23 frames (2.2-2.3 mm, 2.4 deg), the two the
        -- same to 0.7 mm. (The `_b` takes carry it 180 mm from the hand slot.)
        holdOffset   = { -0.00451, -0.02707, 0.09985 },
        holdRotBasis = { 0.02161, -0.08565, 0.04247, 0.99518 },

        -- a rifle's magazine is rocked out by hand
        handPull       = true,
        pullTrigger    = true,
        -- THE SUPPORT HAND ON THE MAGAZINE, as on the Pozhar (2026-10-26: grip alone holds the gun by the magazine,
        -- grip + trigger takes the magazine out): a second two-hand hold, CyberpunkVR_TwoHandGrip_masamune@mag.ini
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

        -- UP AND A LITTLE FORWARD: the four takes' last 60 mm averaged (worst residual 3.2 mm)
        wellAxis   = { -0.0173, -0.2585, -0.9656 },
        insertRun  = 0.100,
        seatAt     = 0.70,

        poseStages = {
            { d = 0.100, off = { -0.00451, -0.02707, 0.09985 }, rot = { 0.02161, -0.08565, 0.04247, 0.99518 },
                         pose = 'masamune_mag_left_carry' },
            { d = 0.030, off = { -0.00451, -0.02707, 0.09985 }, rot = { 0.02161, -0.08565, 0.04247, 0.99518 },
                         pose = 'masamune_mag_left_seat' },
        },
        pose           = 'masamune_mag_left_seat',
    },
}
