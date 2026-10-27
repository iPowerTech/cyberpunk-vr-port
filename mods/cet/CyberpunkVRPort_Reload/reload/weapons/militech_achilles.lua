-- Militech Achilles — physical-reload configuration.
--
-- The tech precision rifle: a flat magazine (30 x 148 x 39 mm, 9 rounds) laid along the top of the receiver behind
-- the slot, going in from behind and above at 45 deg. Read off four takes with VRIK off
-- (build/shotgun-reload-20261019/takes/achilles_*; the analysis in build/rifles-20261020).
--
--   * THE FRESH MAGAZINE TRAVELS ON THE RELOAD CHAIN (tools/reloadchain.py): the show chain is thrown 700 mm out of
--     the way for the whole reload, while `reload_magazine` (2) -> `mag_stdr` (4) takes the spent one out of the
--     hand (1.6-2.3 s), drops it, and comes back with the new one at 2.56 s.
--   * `l_barrel_hinge` / `r_barrel_hinge` (9 / 10), the two halves of the barrel, roll 12.6 deg apart about the bore
--     at the `cover_open` and stay there while the magazine is out: a `hatch`. The 54 deg spread and the front halves
--     sliding after the magazine is in are the `loaded` charge -- the game's flourish, left to it.
--   * No bolt: nothing is racked, the magazine is the whole reload.
--   * The magazine rocked out by hand (`handPull`): the take pulls it out along the well.
--   * THE COVER OVER THE WELL lives on a third rig, the shared precision receiver w_rifle_precision__rcv1 (the
--     `Receiver` component): `magazine_open` (4) swings UP 33.5 deg, its lock (6) rides it round the same hinge and
--     `transform_lock` (5) slides 5.2 mm. That rig had no signature, so it was never recognised, never recorded and
--     never moved (the cover did not open upwards); re-recorded 2026-10-25 with it (rigs.lua, which = 2).
return {
    id    = 'militech_achilles',
    match = 'militech_achilles',

    -- pwa_w_rifle_precision__militech_achilles.anims
    sounds = {
        magOut       = 'w_gun_rifle_tech_achilles_mag_out',
        magIn        = 'w_gun_rifle_tech_achilles_mag_in',
        magGrab      = 'w_gun_rifle_tech_achilles_mag_new_grab',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
        { which = 1, bones = 13, names = { { 2, 'barrel_movement_base' }, { 4, 'ring' }, { 6, 'mag_slot' } } },
        { which = 2, bones = 9,  names = { { 4, 'magazine_open' }, { 6, 'magazine_open_lock' }, { 7, 'magazine_absorber' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    -- THE BARREL HALVES AND THE COVER, parent-local as parts.py reads them (achilles_one_a t=1.09..3.89): the halves
    -- 12.6 deg each, opposite ways about the bore, in 0.05 s at the `cover_open`; the cover 33.5 deg up about +X over
    -- 0.3 s with the lock going round its hinge (43.3 / 22.6 mm and the same turn -- a straight line for the arc, 3.6 mm
    -- off it half way); shut again in 0.13 s once the magazine is in (t=3.76..3.89).
    hatch = {
        which = 1, bone = 9,
        openTime  = 0.30,
        closeTime = 0.13,
        sounds = { open = 'w_gun_rifle_tech_achilles_cover_open', shut = 'w_gun_rifle_tech_achilles_cover_close_empty' },
        parts = {
            { which = 1, bone = 9,  turn = 12.6, axis = { 0, 0, -1 }, from = 0.0, to = 0.17 },
            { which = 1, bone = 10, turn = 12.6, axis = { 0, 0, 1 },  from = 0.0, to = 0.17 },
            { which = 2, bone = 5,  move = { 0.0, 0.0, 0.00519 },     from = 0.0, to = 0.17 },
            { which = 2, bone = 4,  turn = 33.5, axis = { 1, 0, 0 },  from = 0.0, to = 1.0 },
            { which = 2, bone = 6,  move = { 0.0, 0.04329, 0.02255 }, turn = 33.5, axis = { 1, 0, 0 }, from = 0.0, to = 1.0 },
        },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        -- TRACK 0: this magazine rig is its own, not the shared rifle rig whose tracks read 0, 1 -- live, with the
        -- magazine home, they read 1, 0. (It sits inside the stock, so an A/B shows nothing either way.)
        showTrack  = 0,
        mesh       = 'base\\weapons\\firearms\\rifle_precision\\militech_achilles\\entities\\meshes\\w_rifle_precision__militech_achilles__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_achilles.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 30.4 x 148.0 x 38.6 mm, long along its Y (the bore)
        drop = {
            centre  = { 0.0, -0.0693, -0.0012 },
            flat    = 0.0152,
            long    = { 0, 1, 0 },
            thin    = { 1, 0, 0 },
            arm     = 0.0588,
            probe   = 0.0152,
            inertia = 0.00195,
        },

        -- THE HOLD, from the wrist in the bind WeaponLeft frame on the reload chain: achilles_one_b t=2.62..2.83,
        -- achilles_empty_b t=2.49..2.70 and achilles_one_a t=2.69..2.90 (0.1-0.3 mm, 0.6-1.9 deg), the three the same
        -- to 0.1 mm. (achilles_empty_a carries it in another hand, 86 mm off; one take of four.)
        holdOffset   = { 0.02528, -0.01222, -0.14134 },
        holdRotBasis = { -0.70639, -0.27349, 0.20873, 0.61858 },

        handPull       = true,
        pullTrigger    = true,
        -- THE SUPPORT HAND ON THE MAGAZINE, as on the Pozhar (2026-10-26: grip alone holds the gun by the magazine,
        -- grip + trigger takes the magazine out): a second two-hand hold, CyberpunkVR_TwoHandGrip_achilles@mag.ini
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

        -- FROM BEHIND AND ABOVE: achilles_one_b / achilles_empty_b's last 60 mm (worst residual 6.5-6.6 mm)
        wellAxis   = { 0.0619, -0.7112, 0.7003 },
        -- ...NOSE FIRST, THE TAIL UP: the takes seat the magazine's front end with its tail still 46 mm up, turned
        -- 19-25 deg about -X over the last 35 mm (34 deg at 84 mm), and rock the tail down after. Held level on the
        -- line it went in and out through the receiver (in and out through the geometry).
        wellTurn   = { axis = { -1.0, 0.0, 0.0 }, deg = 20.0, to = 0.035 },
        insertRun  = 0.080,
        seatAt     = 0.70,

        poseStages = {
            { d = 0.080, off = { 0.02528, -0.01222, -0.14134 }, rot = { -0.70639, -0.27349, 0.20873, 0.61858 },
                         pose = 'achilles_mag_left_carry' },
            { d = 0.030, off = { 0.02528, -0.01222, -0.14134 }, rot = { -0.70639, -0.27349, 0.20873, 0.61858 },
                         pose = 'achilles_mag_left_seat' },
        },
        pose           = 'achilles_mag_left_seat',
    },
}
