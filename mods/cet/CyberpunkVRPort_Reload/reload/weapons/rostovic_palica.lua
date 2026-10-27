-- Rostovic Palica — physical-reload configuration. A break-action, the SMART one of the four.
--
-- The Satara's receiver and lever, its own barrels and its own, wider-set shells: the chambers are 26.2 mm either
-- side of `mag_slot` (the Satara's are 18), and its magazine rig is nine bones -- `left_magazine` (1) and
-- `right_magazine` (4) the seated shells, `left_magazine_reload` (2) and `right_magazine_reload` (3) the fired
-- ones, on four tracks in the Satara's order. Measured off palica_empty_a / empty_b / one_a:
--
--   * the barrels drop FURTHER than the Satara's: 67.8 .. 68.7 deg held open (one take settles at 57 after a
--     bounce, the other two do not move off 68);
--   * the lever is the receiver's, 41.1 .. 42.6 deg;
--   * and one part of its own moves: `hinge_open` (frame bone 7), a latch that slides 13 mm out along its parent's
--     Y the moment the lever goes over, and creeps most of the way home again while the barrels are open.
return {
    id    = 'rostovic_palica',
    match = 'rostovic_palica__base',

    sounds = {
        open    = 'w_gun_shotgun_smart_palica_open',
        close   = 'w_gun_shotgun_smart_palica_close',
        shellIn = 'w_gun_shotgun_smart_palica_shell_in',
        grab    = 'w_gun_shotgun_smart_palica_mag_new_grab',
    },

    shotgun = {
        kind = 'break',
        frameWhich = 1,
        magWhich   = 0,

        hinge = { bone = 2, axis = { 1, 0, 0 }, sign = -1.0, angle = 68.0, accel = 6500.0, bounce = 0.20,
                  closeTime = 0.08 },
        lever = { which = 2, bone = 5, axis = { 0, 1, 0 }, sign = 1.0, angle = 42.6, back = 0.12 },
        riders = {
            { which = 1, bone = 7, vec = { 0.0, 0.013, 0.0 }, follow = 'latch', openHold = 0.75 },
        },
        thumb = { over = 'sg_break_thumb_over', press = 'sg_break_thumb_press',
                  reach = 0.10, pressTime = 0.08, hold = 0.30, away = 0.18 },

        slot = 'vrp_mag_slot',
        bindRot = { 0.0, 0.70711, 0.70711, 0.0 },
        chambers = {
            { name = 'left',  pos = { -0.0262, 0.0, 0.0 }, live = { bone = 1, track = 0 }, spent = { bone = 2, track = 1 } },
            { name = 'right', pos = {  0.0262, 0.0, 0.0 }, live = { bone = 4, track = 2 }, spent = { bone = 3, track = 3 } },
        },
        -- both shell meshes run -2.1 .. 59.8 mm along the shell: the rim stands 2 mm proud of the bone
        shellLen = 0.0600,
        spentLen = 0.0600,

        eject = { extract = 0.008, mu = 0.30, muStatic = 0.40, inertia = 1.0, tumble = 6.0 },

        item = 'Items.VRPortShellsPalica',
        meshBind = { a = { -0.0262, 0.0, 0.0 }, b = { 0.0262, 0.0, 0.0 } },
        -- the empty reload's pair (palica_empty_a, 1.44 .. 1.62): bone 1 the left shell, bone 4 the right
        carry2 = {
            { off = { -0.01438, -0.02081, -0.02472 }, rot = { -0.26950, 0.51257, -0.70362, 0.41177 } },
            { off = {  0.00071,  0.00705, -0.02322 }, rot = { -0.12068, 0.52251, -0.50816, 0.67394 } },
            pose = 'sg_palica_carry2',
        },
        carry1 = { off = { -0.00858, 0.00517, -0.00110 }, rot = { -0.29874, 0.67448, -0.54821, 0.39407 },
                   pose = 'sg_palica_carry1' },

        magnet = { radius = 0.045, reach = 0.070, angle = 55.0, gain = 3.0, widen = 1.5, seatAt = 0.80 },
        flick = { rate = 320.0, frames = 3, arm = 0.35 },      -- the Satara's, raised with it (see there)

        caseEntity  = 'base\\vrport\\vrp_sg_case_palica.ent',  caseBind  = { -0.0262, 0.0, 0.0 },
        roundEntity = 'base\\vrport\\vrp_sg_round_palica.ent', roundBind = { -0.0262, 0.0, 0.0 },
    },
}
