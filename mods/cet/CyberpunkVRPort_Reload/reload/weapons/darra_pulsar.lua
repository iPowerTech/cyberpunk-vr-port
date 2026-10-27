-- Darra Polytechnic Pulsar — physical-reload configuration (2026-10-25).
--
-- A box magazine (28 x 129 x 198 mm, 40 rounds) under the receiver and a cocking handle on top at the back.
-- Read off four takes with VRIK off (build/shotgun-reload-20261019/takes/pulsar_*; gunsum.py, assets in
-- build/smg-20261025).
--
--   * `cocking_handle` (6) only moves on the EMPTY reload: 53.5 mm straight back and home again in 0.2 s, the
--     left hand on it. No stop, so the rounds go in with that rack (`chamberOnRack = 'dry'`).
return {
    id    = 'darra_pulsar',
    match = 'darra_pulsar',

    -- pwa_w_smg__darra_pulsar.anims
    sounds = {
        slidePull    = 'w_gun_smg_power_pulsar_handle_pull',
        slideRelease = 'w_gun_smg_power_pulsar_handle_release',
        magOut       = 'w_gun_smg_power_pulsar_mag_out',
        magIn        = 'w_gun_smg_power_pulsar_mag_in',
        magGrab      = 'w_gun_smg_power_pulsar_mag_new_grab',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
        { which = 1, bones = 12, names = { { 6, 'cocking_handle' }, { 5, 'front_handle' }, { 4, 'mag_slot' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    slide = {
        slot       = 'vrp_cocking_handle',
        which      = 1, bone = 6,
        -- home at z +29.9, 53.5 mm back at the end of the empty takes' pull (both the same to 0.1 mm)
        travel     = 0.0535,
        restLocal  = { 0.00000, 0.05140, 0.02990 },
        localAxis  = { 0, 0, -1 },
        sign       = 1.0,
        releaseSpeed = 1.00,
        gripSnap   = true,
        previewRadius = 0.06,
        blendTime  = 0.15,
        pullSndAt  = 0.20,
        styleAngleMax = 180.0,
        grabWith   = 'trigger',
        -- THE ROUND OUT OF THE PORT once the handle is this far back with one in the chamber: our own effect
        -- on the gun's port slot, the case alone (build/smg-20261025/make_case_fx_smg.py), and its landing
        -- heard 0.45 s later (`caseSnd`, reload.lua)
        ejectAt    = 0.60,
        ejectFx    = 'vrp_eject_case',
        caseSnd    = 'w_casings_small_hit_floor',
        -- THE LEFT HAND ON IT as it starts back, pulsar_empty_a t=3.15..3.23 (7.1 mm / 7.3 deg; empty_b the same
        -- to 5 mm): left of the handle and a little below it, the take's fingers (pulsar_handle_left).
        grips = {
            behind = { off = { -0.0675, -0.0242, -0.0179 },
                       rot = { 0.4270, 0.4391, 0.0526, -0.7887 },
                       pose = 'pulsar_handle_left' },
        },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        -- TRACK 0: idle 1,0; the bank drops 0 and shows the reload magazine on 1 while it is out (the takes' `mt`).
        showTrack  = 0,
        mesh       = 'base\\weapons\\firearms\\smg\\darra_pulsar\\entities\\meshes\\w_smg__darra_pulsar__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_pulsar.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 27.6 x 129.1 x 198.1 mm, long along its Z
        drop = {
            centre  = { 0.0000, 0.0405, -0.0935 },
            flat    = 0.0138,
            arm     = 0.0852,
            probe   = 0.0138,
            inertia = 0.00466,
        },

        -- THE HOLD, all four takes: rigid over 11-22 frames, the same to 0.4 mm.
        -- CLOSED ON THE MESH (graspfit.py, 2026-10-25): that hold moved and the fingers bent until they touch the
        -- magazine and nothing is inside it; the numbers below and the poses are the fit's.
        holdOffset   = { -0.01117, 0.03901, 0.04480 },
        holdRotBasis = { -0.35634, -0.04863, 0.11228, 0.92631 },

        handPull       = true,
        pullTrigger    = true,
        -- THE SUPPORT HAND ON THE MAGAZINE, as on the Pozhar and the rifles (2026-10-26, for the SMGs whose magazine
        -- sticks out): a second two-hand hold, CyberpunkVR_TwoHandGrip_pulsar@mag.ini
        -- (build/maghold-20261026/mag_holds.py: this gun's fitted carry round the seated magazine) (the magazine goes up through the pistol grip, so this hold is UNDER the right hand) -- hold 1,
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

        -- STRAIGHT UP for the last 40 mm (the four takes 9..40 mm out, residuals 4-8 mm); further out the hand
        -- brings it in from the left, which the snap covers.
        wellAxis   = { 0.0629, 0.0962, -0.9934 },
        insertRun  = 0.040,
        seatAt     = 0.70,
        chamberOnRack = 'dry',

        poseStages = {
            { d = 0.040, off = { -0.01117, 0.03901, 0.04480 }, rot = { -0.35634, -0.04863, 0.11228, 0.92631 },
                         pose = 'pulsar_mag_left_carry' },
            { d = 0.030, off = { -0.01117, 0.03901, 0.04480 }, rot = { -0.35634, -0.04863, 0.11228, 0.92631 },
                         pose = 'pulsar_mag_left_seat' },
        },
        pose           = 'pulsar_mag_left_seat',
    },
}
