-- Rostovic Satara — physical-reload configuration. The FIRST BREAK-ACTION, and the base of two more: the Igla
-- and the Testera are built on its receiver, its magazine rig and its own two shells.
--
-- Driven by reload/shotgun.lua, not by the slide-and-magazine code: the whole `shotgun` section below is read
-- there. Every number was taken off the game's own reloads, recorded with VRIK switched off -- four takes
-- (satara_empty_b / empty_c fired out, satara_one_a / one_b fired once) under build/shotgun-reload-20261019:
--
--   * the TOP LEVER is bone 5 of the shared dual receiver rig (w_shotgun_dual__rcv1, `barrel_open`): it turns
--     about its own Y, 34 to 42.6 deg, under the right thumb, for about 0.4 s;
--   * the BARRELS are `barrel_hinge` (frame rig bone 2). They drop 43.1 to 44.6 deg -- the hold angle in every
--     take; the 55 deg that shows up is the barrels swung FURTHER open just before the close -- and they get
--     there in about a tenth of a second from the latch;
--   * the CHAMBERS hang off `mag_slot`, which rides the hinge: the magazine rig's `l_bullet` / `r_bullet` (1, 2)
--     are the seated shells at 18 mm either side, `l_bullet_reload` / `r_bullet_reload` (3, 4) the FIRED ones the
--     native reload throws out, on the same rest. Their meshes say which is which: `_mag_stdr_` is 8.4 mm longer
--     (88.9 against 80.5 -- the crimp has opened).
return {
    id    = 'rostovic_satara',
    -- `rostovic_satara__base`, not the bare model name: the Igla, the Testera and the Palica all carry the
    -- Satara's receiver (`w_shotgun_dual__rostovic_satara__rcv1_01`), so the bare name matches all four.
    match = 'rostovic_satara__base',

    sounds = {
        lever   = 'w_gun_shotgun_tech_satara_top_lever',
        open    = 'w_gun_shotgun_tech_satara_open',
        close   = 'w_gun_shotgun_tech_satara_close',
        shellIn = 'w_gun_shotgun_tech_satara_shell_in',
        grab    = 'w_gun_shotgun_tech_satara_mag_new_grab',
    },

    shotgun = {
        kind = 'break',
        frameWhich = 1,      -- the frame rig: barrels and hinge
        magWhich   = 0,      -- the magazine rig: the shells

        -- THE BARRELS. About the hinge's own X, NEGATIVE: positive turned the muzzle up through the receiver.
        -- 6500 deg/s^2 puts them on the stop in 0.12 s, which is the take's 0.96 -> 1.09 (3.9 -> 44.6 deg).
        -- The close is the native one's six hundredths of a second, accelerating.
        hinge = { bone = 2, axis = { 1, 0, 0 }, sign = -1.0, angle = 44.6, accel = 6500.0, bounce = 0.22,
                  closeTime = 0.07 },

        -- THE TOP LEVER: rig 2 (the receiver), bone 5, about its own Y. Springs back 0.12 s after the thumb
        -- leaves it, as in the take (back to 0 at 1.36 with the barrels still open).
        lever = { which = 2, bone = 5, axis = { 0, 1, 0 }, sign = 1.0, angle = 42.6, back = 0.12 },

        -- THE RIGHT THUMB, keyframed on the hand that holds the gun: lifting off the grip (0.88 in the take) and
        -- straight out on the lever (1.07 .. 1.22). Three joints, the thumb only.
        thumb = { over = 'sg_break_thumb_over', press = 'sg_break_thumb_press',
                  reach = 0.10, pressTime = 0.08, hold = 0.30, away = 0.18 },

        -- THE CHAMBERS, in FIRING ORDER: the left barrel goes first (the one-shot takes empty only the left).
        slot = 'vrp_mag_slot',
        bindRot = { 0.0, 0.70711, 0.70711, 0.0 },
        chambers = {
            { name = 'left',  pos = { -0.018, 0.0, 0.0 }, live = { bone = 1, track = 0 }, spent = { bone = 3, track = 1 } },
            { name = 'right', pos = {  0.018, 0.0, 0.0 }, live = { bone = 2, track = 2 }, spent = { bone = 4, track = 3 } },
        },
        shellLen = 0.0805,   -- w_shotgun_dual__rostovic_satara__mag_std_01_left_01.mesh, along the shell
        spentLen = 0.0889,   -- ..._mag_stdr_01_left_01.mesh: the fired one, crimp open

        -- THE SPENT CASES, out of the chambers by their own weight. A plastic hull in a steel chamber holds less
        -- than brass in a cylinder: 0.40 to break it loose, 0.30 once it is moving, which lets go with the barrels
        -- about 15 deg above level -- the breech end has to come DOWN, which on a broken gun means bringing the
        -- muzzle up. The extractor lifts every fired case 8 mm as the barrels go over; a sharp jerk of the gun
        -- throws them out on its own (`inertia` = the chamber's acceleration, 1:1).
        eject = { extract = 0.008, mu = 0.30, muStatic = 0.40, inertia = 1.0, tumble = 6.0 },

        -- THE SHELLS IN THE LEFT HAND, an item of two parts (one per shell) placed in the hold slot. Each part
        -- draws the left or the right shell mesh, whose vertices sit 18 mm out at the bone's rest: `meshBind`
        -- is that rest, taken back out so a part lands exactly where the measured hold puts its shell.
        item = 'Items.VRPortShellsSatara',
        meshBind = { a = { -0.018, 0.0, 0.0 }, b = { 0.018, 0.0, 0.0 } },

        -- TWO SHELLS, side by side, the way the empty reload carries them (satara_empty_c, t = 1.31 .. 1.49,
        -- each held to under a millimetre): bone 1 is the left shell, bone 2 the right, in the WeaponLeft slot.
        carry2 = {
            { off = { -0.00916, -0.01340, -0.02173 }, rot = { 0.00661, -0.64251, 0.39645, -0.65572 } },
            { off = {  0.00106,  0.00904, -0.02296 }, rot = { 0.03352, -0.61102, 0.45309, -0.64826 } },
            pose = 'sg_satara_carry2',
        },
        -- ONE SHELL, after one shot (satara_one_a, t = 1.58 .. 1.69)
        carry1 = { off = { 0.00240, -0.01333, -0.01166 }, rot = { 0.83133, -0.48627, 0.23660, 0.12826 },
                   pose = 'sg_satara_carry1' },
        -- and the hand coming off the shells once they are home (satara_empty_c, 2.07 .. 2.20)
        releasePose = 'sg_satara_release',

        -- THE MAGNET, one shell into one chamber at a time: within 45 mm of the chamber's line, pointing within
        -- 55 deg of it, and up to 70 mm short of the mouth. Seated at 80 % of the way in.
        magnet = { radius = 0.045, reach = 0.070, angle = 55.0, gain = 3.0, widen = 1.5, seatAt = 0.80 },

        -- A FLICK SHUTS IT: the gun pitched muzzle-UP at 320 deg/s or more for three frames, in the player's own
        -- frame (so turning and walking read nothing). The click shuts it too. 240 deg/s over two frames closed it
        -- on the way back up from tipping the cases out (too sensitive when the muzzle is only lowered to
        -- tip the cases out).
        flick = { rate = 320.0, frames = 3, arm = 0.35 },

        -- what leaves the gun: the fired case, and a live shell let go of before it went in
        caseEntity  = 'base\\vrport\\vrp_sg_case_satara.ent',  caseBind  = { -0.018, 0.0, 0.0 },
        roundEntity = 'base\\vrport\\vrp_sg_round_satara.ent', roundBind = { -0.018, 0.0, 0.0 },
    },
}
