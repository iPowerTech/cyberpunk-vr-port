-- Tsunami Rasetsu — physical-reload configuration.
--
-- Phantom Liberty's tech sniper rifle: a small magazine (23 x 92 x 49 mm, 3 rounds) that lives in a CARRIER under a
-- sliding top cover. Read off four takes with VRIK off (build/shotgun-reload-20261019/takes/rasetsu_*; the analysis in
-- build/rifles-20261020).
--
--   * Every reload opens the top, five parts in order: `magazine_cover` (6) slides 113 mm back, the carrier
--     `magazine_base` (8) moves 35 mm out to the left, `magazine_flap` (9) swings 50 deg, `magazine_up` (10) lifts 15 mm
--     and `magazine_rotator` (11) turns 28 deg -- and `mag_slot` hangs off that rotator, so the magazine goes into the
--     OPEN carrier, 134 mm from where it ends up, and the closing cover carries it home. A `hatch` of parts, open while
--     the magazine is out, on the takes' own clock (1.36..1.64 s open, 5.11..5.67 s shut).
--   * No bolt: nothing is racked.
--   * The magazine is under the cover, so it leaves on the button only (no `handPull`).
return {
    id    = 'tsunami_rasetsu',
    match = 'tsunami_rasetsu',

    -- pwa_w_rifle_sniper__tsunami_rasetsu.anims
    sounds = {
        magOut       = 'w_gun_sniper_tech_rasetsu_eject_magazine',
        magIn        = 'w_gun_sniper_tech_rasetsu_insert_magazine',
        magGrab      = 'w_gun_sniper_tech_rasetsu_place_mag',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
        { which = 1, bones = 13, names = { { 6, 'magazine_cover' }, { 8, 'magazine_base' }, { 12, 'mag_slot' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    -- THE TOP, parent-local as parts.py reads it at full open (rasetsu_empty_a t=3.0), each part on the stretch of the
    -- opening it moves in (10-90 % of its own travel on the 0.30 s clock)
    hatch = {
        which = 1, bone = 6,
        openTime  = 0.30,
        closeTime = 0.56,
        sounds = { open = 'w_gun_sniper_tech_rasetsu_slide_out_mag', shut = 'w_gun_sniper_tech_rasetsu_reload' },
        parts = {
            { which = 1, bone = 6,  move = { 0.0, 0.0, -0.11301 }, from = 0.00, to = 0.22 },
            { which = 1, bone = 8,  move = { 0.03500, 0.0, 0.0 },  from = 0.37, to = 0.68 },
            { which = 1, bone = 9,  turn = 50.0, axis = { 0, 0, -1 }, from = 0.59, to = 0.89 },
            { which = 1, bone = 10, move = { 0.0, 0.01502, 0.0 },  from = 0.74, to = 0.82 },
            { which = 1, bone = 11, move = { 0.00400, 0.01003, 0.0 }, turn = 28.2, axis = { 0, 0, -1 }, from = 0.67, to = 0.98 },
        },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        -- TRACK 0: a rifle's magazine rig idles at 1, 0 and the game's own reload drops index 0 while the magazine
        -- is out (the takes' `mt`); index 1 is the reload magazine. Held at 1, the gun kept its magazine after a drop.
        showTrack  = 0,
        mesh       = 'ep1\\weapons\\firearms\\rifle_sniper\\tsunami_rasetsu\\entities\\meshes\\w_rifle_sniper__tsunami_rasetsu__mag_std_01.mesh',
        entity     = 'base\\vrport\\vrp_mag_rasetsu.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 23.3 x 92.3 x 49.3 mm, long along its Y
        drop = {
            centre  = { 0.0, 0.0, 0.0 },
            flat    = 0.0116,
            long    = { 0, 1, 0 },
            thin    = { 1, 0, 0 },
            arm     = 0.0345,
            probe   = 0.0116,
            inertia = 0.00091,
        },

        -- THE HOLD, rasetsu_one_a / rasetsu_empty_a: rigid over 31-32 frames (1.2-1.6 mm, 1.0-1.7 deg), the two the
        -- same to 0.1 mm.
        holdOffset   = { 0.02001, -0.02043, -0.05776 },
        holdRotBasis = { -0.64302, -0.24507, 0.35366, 0.63356 },

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

        -- INTO THE OPEN CARRIER from the left and above: rasetsu_one_a's last 110 mm onto the open slot (4.24..4.38)
        wellAxis   = { -0.8662, -0.1856, 0.4640 },
        insertRun  = 0.080,
        seatAt     = 0.70,

        poseStages = {
            { d = 0.080, off = { 0.02001, -0.02043, -0.05776 }, rot = { -0.64302, -0.24507, 0.35366, 0.63356 },
                         pose = 'rasetsu_mag_left_carry' },
            { d = 0.030, off = { 0.02001, -0.02043, -0.05776 }, rot = { -0.64302, -0.24507, 0.35366, 0.63356 },
                         pose = 'rasetsu_mag_left_seat' },
        },
        pose           = 'rasetsu_mag_left_seat',
    },
}
