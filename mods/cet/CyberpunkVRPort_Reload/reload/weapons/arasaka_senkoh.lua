-- Arasaka Senkoh LX — physical-reload configuration (2026-10-25).
--
-- A tech SMG with a short box magazine (24 x 94 x 138 mm, 30 rounds) and NOTHING TO RACK: the takes move no
-- frame bone but the iron sight's 12.8 mm fold, so there is no `slide` section (the Chao's way) and the
-- magazine chambers as it seats. Read off four takes with VRIK off (build/shotgun-reload-20261019/takes/
-- senkoh_*). The game's WeaponLeft bone wanders 130 mm off the hand here, so the hold is measured from the
-- WRIST (reloadchain.py, chain 1,3).
return {
    id    = 'arasaka_senkoh',
    match = 'senkoh',

    -- pwa_w_smg__senkoh_lx.anims
    sounds = {
        magOut       = 'w_gun_smg_tech_senkoh_mag_out',
        magIn        = 'w_gun_smg_tech_senkoh_mag_in',
        magGrab      = 'w_gun_smg_tech_senkoh_mag_new_grab',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
        { which = 1, bones = 16, names = { { 9, 'slider' }, { 8, 'front_part' }, { 6, 'mag_slot' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        -- TRACK 0: idle 1,0; the bank drops 0 and shows the reload magazine on 1 while it is out (the takes' `mt`).
        showTrack  = 0,
        mesh       = 'base\\weapons\\firearms\\smg\\senkoh_lx\\entities\\meshes\\w_smg__senkoh_lx__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_senkoh.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 23.5 x 93.8 x 138.2 mm, long along its Z
        drop = {
            centre  = { 0.0000, -0.0257, -0.0627 },
            flat    = 0.0118,
            arm     = 0.0574,
            probe   = 0.0118,
            inertia = 0.00233,
        },

        -- THE HOLD, senkoh_empty_b t=1.27..1.62 and senkoh_one_a t=1.27..1.71, from the wrist into the bind
        -- WeaponLeft: 15 / 19 frames (17-24 mm, 14 deg -- a loose carry), the two the same to 6 mm / 1.5 deg.
        -- CLOSED ON THE MESH (graspfit.py, 2026-10-25): that hold moved and the fingers bent until they touch the
        -- magazine and nothing is inside it; the numbers below and the poses are the fit's.
        holdOffset   = { -0.06913, 0.08123, -0.06383 },
        holdRotBasis = { -0.58183, 0.50882, 0.51441, 0.37142 },

        handPull       = true,
        pullTrigger    = true,
        -- THE SUPPORT HAND ON THE MAGAZINE, as on the Pozhar and the rifles (2026-10-26, for the SMGs whose magazine
        -- sticks out): a second two-hand hold, CyberpunkVR_TwoHandGrip_senkoh@mag.ini
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

        -- UP AND BACK, rocked in: three takes 9..60 mm out, averaged (residuals 2.5-9 mm), their 11 deg lean to
        -- the right taken out -- the magazine came in right of the well, looking down the bore
        -- (2026-10-25).
        wellAxis   = { 0.0000, -0.3688, -0.9295 },
        insertRun  = 0.060,
        seatAt     = 0.70,

        poseStages = {
            { d = 0.060, off = { -0.06913, 0.08123, -0.06383 }, rot = { -0.58183, 0.50882, 0.51441, 0.37142 },
                         pose = 'senkoh_mag_left_carry' },
            { d = 0.030, off = { -0.06913, 0.08123, -0.06383 }, rot = { -0.58183, 0.50882, 0.51441, 0.37142 },
                         pose = 'senkoh_mag_left_seat' },
        },
        pose           = 'senkoh_mag_left_seat',
    },
}
