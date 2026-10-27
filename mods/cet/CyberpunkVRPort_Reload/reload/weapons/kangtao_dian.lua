-- Kang Tao Dian — physical-reload configuration (2026-10-25).
--
-- A smart SMG with a long box magazine (38 x 95 x 246 mm, 45 rounds) put in FROM BELOW. Read off four takes
-- with VRIK off (build/shotgun-reload-20261019/takes/dian_*). Three rigs: the barrel is the frame
-- (`charging_handle` 3, `barrel_muzzle` 8), the receiver (rcv1: `magazine_open` 6, `magazine_rail` 7,
-- `mag_slot` 11) and the magazine's own.
--
--   * THE MAGAZINE SLOT IS OURS, on `barrel` at the receiver mag_slot's place -- and `barrel`'s axes are the
--     bore's (Z forward), so it is turned back to the weapon frame (build/smg-20261025/dian_slotrot.py). Unturned,
--     every direction read off it came out 90 deg round: the magazine went in "forward along the bore"
--     (the magazine's magnet ran forward along the bore instead of up from below).
--
--   * EVERYTHING MOVES BY ITSELF on the empty reload, no hand on it (the left wrist is 150-400 mm off the
--     handle): the handle 33.4 mm forward with the muzzle 61.7 mm out, then the cover up 28 deg, then the rail
--     31 mm DOWN (its parent-local Y is the model's Z) -- and all of it home again once the magazine is in.
--     So it is a `hatch` (open while the magazine is out, the Ashura's bolt), nothing to rack, and the
--     rounds go in with the seat.
--   * The muzzle also spins half a turn as it goes; one write slot per bone, so only its travel is kept.
return {
    id    = 'kangtao_dian',
    match = 'kangtao_dian',

    -- pwa_w_special_kangtao_dian.anims
    sounds = {
        magOut       = 'w_gun_smg_smart_dian_mag_out',
        magIn        = 'w_gun_smg_smart_dian_mag_in',
        magGrab      = 'w_gun_smg_smart_dian_mag_new_grab',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
        { which = 1, bones = 9,  names = { { 3, 'charging_handle' }, { 8, 'barrel_muzzle' }, { 6, 'r_blade_01' } } },
        { which = 2, bones = 14, names = { { 6, 'magazine_open' }, { 7, 'magazine_rail' }, { 11, 'mag_slot' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    -- THE TAKES' ORDER (dian_empty_a): handle and muzzle t=0.87..1.07, cover 1.09..1.27, rail 1.22..1.38; back
    -- the other way round. Parent-local, as parts.py read them.
    hatch = {
        which = 1, bone = 3,
        openTime  = 0.50,
        closeTime = 0.40,
        sounds = { open = 'w_gun_smg_smart_dian_move', shut = 'w_gun_smg_smart_dian_load' },
        parts = {
            { which = 1, bone = 3, move = { 0.0, 0.0, 0.03335 }, from = 0.00, to = 0.40 },
            { which = 1, bone = 8, move = { 0.0, 0.0, 0.06166 }, from = 0.00, to = 0.40 },
            { which = 2, bone = 6, turn = 28.1, axis = { -1.0, 0.0, 0.0 }, from = 0.40, to = 0.75 },
            { which = 2, bone = 7, move = { 0.0, -0.03126, 0.0 }, from = 0.70, to = 1.00 },
        },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        -- TRACK 0: idle 1,0; the bank drops 0 and shows the reload magazine on 1 while it is out (the takes' `mt`).
        showTrack  = 0,
        mesh       = 'base\\weapons\\firearms\\special\\kangtao_dian\\entities\\meshes\\w_special__kangtao_dian__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_dian.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 37.6 x 94.5 x 246.0 mm, long along its Z
        drop = {
            centre  = { 0.0000, -0.0027, -0.1153 },
            flat    = 0.0188,
            arm     = 0.1042,
            probe   = 0.0188,
            inertia = 0.00579,
        },

        -- THE HOLD: the Guillotine's fitted hold -- a long magazine held by its lower half -- as the start, closed
        -- on this mesh (the takes were read through the unturned slot, so their hand was no guide).
        -- CLOSED ON THE MESH (graspfit.py, 2026-10-25): that hold moved and the fingers bent until they touch the
        -- magazine and nothing is inside it; the numbers below and the poses are the fit's.
        holdOffset   = { -0.01169, 0.03074, 0.02608 },
        holdRotBasis = { -0.26962, -0.15556, -0.03465, 0.94969 },

        handPull       = true,
        pullTrigger    = true,
        -- THE SUPPORT HAND ON THE MAGAZINE, as on the Pozhar and the rifles (2026-10-26, for the SMGs whose magazine
        -- sticks out): a second two-hand hold, CyberpunkVR_TwoHandGrip_dian@mag.ini
        -- (build/maghold-20261026/mag_holds.py: this gun's fitted carry round the seated magazine) -- hold 1,
        -- taken away with no magazine in the well.
        holdAlt        = 1,

        snapRadius     = 0.050,
        -- A STRONGER, LONGER MAGNET (the magnet did not catch, or too weakly, 2026-10-25): 110 mm off
        -- the line instead of 75, 200 mm out along it instead of 128, full strength sooner (gain 9).
        seatSnapRadius = 0.110,
        magnetDepth    = 0.200,
        armDist        = 0.070,
        snapGain       = 1.6,
        seatRadius     = 0.03,
        gravity        = 9.81,
        litterTime     = 3.0,
        holdWiden      = 2.2,
        insertGain     = 9.0,
        dropPush       = 1.1,

        -- STRAIGHT UP from below: the takes' last 80 mm (dian_empty_a t=2.33..2.69, 0.1 mm off one line), turned
        -- back through the slot's 180 deg about (0, 1, 1) -- what read as -Y through the barrel's axes is -Z.
        wellAxis   = { 0.0000, 0.0000, -1.0000 },
        insertRun  = 0.080,
        seatAt     = 0.70,

        poseStages = {
            { d = 0.080, off = { -0.01169, 0.03074, 0.02608 }, rot = { -0.26962, -0.15556, -0.03465, 0.94969 },
                         pose = 'dian_mag_left_carry' },
            { d = 0.030, off = { -0.01169, 0.03074, 0.02608 }, rot = { -0.26962, -0.15556, -0.03465, 0.94969 },
                         pose = 'dian_mag_left_seat' },
        },
        pose           = 'dian_mag_left_seat',
    },
}
