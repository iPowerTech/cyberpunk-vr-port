-- Militech Hercules 3AX — physical-reload configuration.
--
-- Phantom Liberty's heavy rifle (filed under `special`): an ammo box (127 x 104 x 195 mm, 90 rounds) under the
-- receiver. Read off four takes with VRIK off (build/shotgun-reload-20261019/takes/hercules_*; the analysis in
-- build/rifles-20261020).
--
--   * NEITHER BOX EVER LEAVES THE GUN IN THE TAKES. The Hercules carries two box components on one 10-bone rig (the
--     shown `mag_std_01` and the reload `mag_stdr_01`), and both chains sit on the slot for the whole clip -- only
--     their clips and belt flap move. The frame rig has 10 bones too, and the recorder's frame row came back as the
--     second box, so the frame's own parts were never read: no `hatch` here.
--   * So THE HOLD IS THE HAND ON THE BOX AS IT LOCKS: the wrist at the `lock_ammo_box`, in the bind WeaponLeft frame,
--     against the box's seat (tools/reloadchain.py all=1) -- three takes agree to 0.1 mm. Carried rigidly in that hold
--     the box comes up from below and behind, stops 29 mm right of its seat and slides home leftwards
--     (tools/rigidbox.py); the well axis is the fit over that last 120 mm.
--   * No bolt: nothing is racked. The spent box rocked out by hand (`handPull`), as the take grabs it.
return {
    id    = 'militech_hercules',
    match = 'militech_hercules',

    -- pwa_w_special__militech_hercules.anims
    sounds = {
        magOut       = 'w_gun_hercules3ax_remove_ammo_box',
        magIn        = 'w_gun_hercules3ax_lock_ammo_box',
        magGrab      = 'w_gun_hercules3ax_take_out_ammo_box',
    },

    basis = 'weapon',

    rigs = {
        { which = 0, bones = 10, names = { { 3, 'mag_std' }, { 4, 'belt_open' }, { 6, 'mag_stdr' } } },
        { which = 1, bones = 10, names = { { 7, 'magazine_grabber' }, { 8, 'handle' }, { 9, 'reload_mechanism' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel' },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        -- THE BOX THAT IS DRAWN IS REACHED AS RIG 1, track 0. Both 10-bone passes the plugin identifies on this gun are
        -- box instances (VRRigBone reads the box's belt_open / release_hinge on rig 0 AND rig 1; tracks 1, 0 on both),
        -- and an A/B in the simulator settled which one is seen: holding rig 0's track 0 or 1 at 0 changed nothing,
        -- rig 1's track 0 took the drum off the gun. The frame shares the drawn box's track buffer, so the box passes
        -- arrive cached as rig 1 -- also why the recorder's frame row came back as a box. Tracks and scale are all
        -- `which` drives for a magazine, and VRRigReset drops a held track on every draw.
        which      = 1,
        showTrack  = 0,
        -- THE BOX IS CARRIED AS AN ITEM, box and cloth cover together (`Items.VRPortBoxHercules`, two parts placed at
        -- the hold): the carrier component the other magazines ride draws one mesh, and the hand held a bare box (the
        -- magazine's housing was missing). No `mesh` here, so the carrier is passed over; the falling box is `entity`,
        -- which has carried the cloth all along. (Was the carrier's: ep1\...\w_special__militech_hercules__mag_std_01.mesh)
        item       = 'Items.VRPortBoxHercules',
        entity     = 'base\\vrport\\vrp_mag_hercules.ent',
        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- the mesh's bounding box: 126.7 x 104.1 x 194.9 mm, long along its Z, thinnest along its Y
        drop = {
            centre  = { -0.0135, 0.0, 0.0337 },
            flat    = 0.0521,
            long    = { 0, 0, 1 },
            thin    = { 0, 1, 0 },
            arm     = 0.0454,
            probe   = 0.0521,
            inertia = 0.00450,
        },

        -- THE HOLD, from the wrist in the bind WeaponLeft frame at the lock: hercules_one_a / one_b t=4.60..4.80 and
        -- hercules_empty_a t=4.50..4.67 (0.1-0.3 mm, 0.4-0.5 deg), the three the same to 0.1 mm.
        holdOffset   = { 0.06432, 0.00185, -0.06057 },
        holdRotBasis = { 0.00772, 0.66726, 0.08549, 0.73986 },

        handPull       = true,
        pullTrigger    = true,
        -- THE SUPPORT HAND ON THE MAGAZINE, as on the Pozhar (2026-10-26: grip alone holds the gun by the magazine,
        -- grip + trigger takes the magazine out): a second two-hand hold, CyberpunkVR_TwoHandGrip_hercules@mag.ini
        -- (build/maghold-20261026/mag_holds.py: this gun's fitted carry round the seated magazine) -- hold 1,
        -- taken away with no magazine in the well.
        holdAlt        = 1,

        snapRadius     = 0.050,
        -- THE HAND-SIZED CATCH, A LITTLE STRONGER (too weak a magnet -- and then, at 140 mm / 240 mm out,
        -- an odd one: back to the old catch, a little stronger). The catch radius is also the SEAT test's
        -- sideways tolerance, so 140 mm seated the box while it was still being brought up -- it showed on the gun
        -- with the hand still holding one, and the next squeeze took it out again. 75 -> 85 mm, 128 -> 150 mm along
        -- the well, gain 6 -> 7.
        seatSnapRadius = 0.085,
        magnetDepth    = 0.150,
        armDist        = 0.070,
        snapGain       = 1.6,
        seatRadius     = 0.03,
        gravity        = 9.81,
        litterTime     = 3.0,
        holdWiden      = 2.2,
        insertGain     = 7.0,
        dropPush       = 1.1,

        -- UP FROM BELOW, BEHIND AND THE RIGHT: the box's last 120 mm carried in the hold above (tools/rigidbox.py),
        -- hercules_one_a / one_b / empty_a's fits averaged
        wellAxis   = { 0.6635, -0.3442, -0.6644 },
        insertRun  = 0.080,
        seatAt     = 0.70,

        poseStages = {
            { d = 0.080, off = { 0.06432, 0.00185, -0.06057 }, rot = { 0.00772, 0.66726, 0.08549, 0.73986 },
                         pose = 'hercules_mag_left_carry' },
            { d = 0.030, off = { 0.06432, 0.00185, -0.06057 }, rot = { 0.00772, 0.66726, 0.08549, 0.73986 },
                         pose = 'hercules_mag_left_seat' },
        },
        pose           = 'hercules_mag_left_seat',
    },
}
