-- Tsunami Ashura — physical-reload configuration.
--
-- Phantom Liberty's smart single-shot sniper rifle: no magazine, one round (15 x 75 x 15 mm) laid onto the bolt.
-- Read off four takes with VRIK off (build/shotgun-reload-20261019/takes/ashura_*; the analysis in
-- build/rifles-20261020).
--
--   * THE BOLT RUNS ITSELF. `cocking_mechanism` (5) goes back 100 mm at the `handle_pull` and home at the
--     `handle_release` in every reload, and neither hand is on it: the left wrist stays on the fore-end 77 mm from it
--     (moving with it by exactly its travel) and the right on the grip. So it is a `hatch` -- open while the round is
--     out, shut when one is laid in -- the way the Rasetsu's carrier is, not a slide to rack.
--   * `mag_slot` (13) hangs off the bolt, so the round goes onto the OPEN bolt, 100 mm back from where it fires
--     from, and the bolt carries it home.
--   * THE ROUND HAS NO SWITCH: no `visibilityAnimationParam`, no tracks on its rig, so it is hidden by scale
--     (`hideScale`, the `mag_std` bone it is skinned to).
--   * It leaves on the button only (no `handPull`): there is nothing to take hold of.
return {
    id    = 'tsunami_ashura',
    match = 'tsunami_ashura',

    -- pwa_w_rifle_sniper__tsunami_ashura.anims
    sounds = {
        magIn        = 'w_gun_sniper_smart_ashura_load',
        magGrab      = 'w_gun_sniper_smart_ashura_mag_new_grab',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 3,  names = { { 1, 'magazine' }, { 2, 'mag_std' } } },
        { which = 1, bones = 14, names = { { 5, 'cocking_mechanism' }, { 10, 'hammer' }, { 13, 'mag_slot' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    -- THE BOLT, parent-local: back 100 mm along the bore, in 0.11 s out (ashura_one_a t=1.33..1.44) and 0.07 s home
    -- (t=3.13..3.20)
    hatch = {
        which = 1, bone = 5,
        openTime  = 0.11,
        closeTime = 0.07,
        sounds = { open = 'w_gun_sniper_smart_ashura_handle_pull', shut = 'w_gun_sniper_smart_ashura_handle_release' },
        parts = {
            { which = 1, bone = 5, move = { 0.0, 0.0, -0.100 }, from = 0.0, to = 1.0 },
        },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        hideScale  = { 2 },
        mesh       = 'base\\weapons\\firearms\\rifle_sniper\\tsunami_ashura\\entities\\meshes\\w_rifle_sniper__tsunami_ashura__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_ashura.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 14.8 x 74.7 x 14.8 mm, long along its Y (the bore)
        drop = {
            centre  = { 0.0, 0.0396, -0.0004 },
            flat    = 0.0074,
            long    = { 0, 1, 0 },
            thin    = { 0, 0, 1 },
            arm     = 0.0299,
            probe   = 0.0074,
            inertia = 0.00048,
        },
        -- a round is the same object turned any way about its own length
        rollSym = { n = 8, axis = { 0, 1, 0 } },

        -- THE HOLD, from the wrist in the bind WeaponLeft frame: ashura_empty_a t=2.07..2.20, the only take whose
        -- hand carries it (1.1 mm, 0.5 deg); the other three bring the round in from 400 mm off with no hand on it.
        holdOffset   = { 0.00310, 0.00360, 0.00229 },
        holdRotBasis = { -0.05719, -0.24518, 0.16938, 0.95285 },

        snapRadius     = 0.050,
        -- THE MAGNET LATER (2026-10-25): a round is 15 mm thick, and the rifle defaults (75 mm
        -- off the line, 128 mm out along it) took it from the hand while it was still a hand's width off the bolt.
        -- Now 50 mm off the line and 60 mm out: the player brings it most of the way.
        seatSnapRadius = 0.050,
        magnetDepth    = 0.060,
        armDist        = 0.070,
        snapGain       = 1.6,
        seatRadius     = 0.03,
        gravity        = 9.81,
        litterTime     = 3.0,
        holdWiden      = 2.2,
        insertGain     = 6.0,
        dropPush       = 1.1,

        -- ONTO THE OPEN BOLT ON THE DIAGONAL, from behind and above at 45 deg, one straight line: the take's own way
        -- in (ashura_empty_a's last 55 mm, t=2.49..2.65). It was a `wellBend` for a while -- level with the bore,
        -- then the last 20 mm down onto the bolt (parallel to the bore, then a little down) -- and is back on
        -- the diagonal now (the round goes in on a diagonal, 2026-10-25).
        wellAxis   = { -0.1100, -0.6930, 0.7130 },
        insertRun  = 0.080,
        seatAt     = 0.90,

        poseStages = {
            { d = 0.080, off = { 0.00310, 0.00360, 0.00229 }, rot = { -0.05719, -0.24518, 0.16938, 0.95285 },
                         pose = 'ashura_round_left' },
        },
        pose           = 'ashura_round_left',
    },
}
