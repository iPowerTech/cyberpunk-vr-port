-- Arasaka Yukimura — physical-reload configuration.
--
-- The eleventh pistol, and the twin of the Tsunami Kappa: the Kappa borrows this weapon's rig AND its animations
-- whole, so the two share a signature and, as it turns out, share their magazine handling exactly. What differs is
-- the mesh -- and that difference is the whole config below.
--
-- ITS `slider` IS REAL. Both weapons' rigs carry the bone; only this one hangs geometry off it (2096 triangles, the
-- front block, Z 81..162 in the weapon's frame). So where the Kappa declares no slide at all, this one does.
return {
    id    = 'arasaka_yukimura',
    match = 'yukimura',

    sounds = {
        magOut  = 'w_gun_pistol_smart_yukimura_mag_out',
        magIn   = 'w_gun_pistol_smart_yukimura_mag_in',
        magGrab = 'w_gun_pistol_smart_yukimura_mag_new_grab',
        -- `load` appears in the EMPTY reloads only, on the frame the front block goes home -- so it is this
        -- weapon's slide-release sound, and the stick click is what should play it.
        slideRelease = 'w_gun_pistol_smart_yukimura_load',
    },

    -- MEASURED: from the slide slot, the magazine slot's perpendicular residual off the barrel axis is 7.2 mm. The
    -- Unity taught what a basis built on 5.8 mm does -- "down" came out pointing up -- so this weapon takes its
    -- frame from the weapon entity, like its twin.
    basis = 'weapon',

    rigs = {
        { which = 1, bones = 8, names = { { 7, 'slider' }, { 6, 'mag_slot' }, { 5, 'gun_trigger' } } },
        { which = 0, bones = 5, names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_slider' },

    slide = {
        slot       = 'vrp_slider',
        which      = 1, bone = 7,
        -- 17.0 mm, home at Z 129.2 and back at 112.2. Read off its own rig animation and confirmed live in the
        -- empty take, where the bone sits at 112.2 from the first frame and returns at t=3.73.
        travel     = 0.0170,
        restLocal  = { 0.00000, 0.00920, 0.12920 },
        lockLocal  = { 0.00000, 0.00920, 0.11220 },
        localAxis  = { 0, 0, -1 },
        sign       = 1.0,
        -- MEASURED off `empty_reload`: the 17 mm takes 0.066 s, so 0.26 m/s. Recoil throws it back faster (0.51)
        -- and that is the game's own business, not ours.
        releaseSpeed = 0.30,
        blendTime  = 0.15,
        pullSndAt  = 0.20,

        -- NO `grips`, at the user's call and matching the weapon: nothing in any of its clips puts a hand on this
        -- block -- it moves on recoil and at the end of an empty reload and nowhere else -- so it comes forward on
        -- the right stick click, the way the Omaha's does.
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        showTrack  = 0,

        -- Its twin's reason, and its own measurement: the magazine leaves out of the BACK, towards the shooter
        -- (`wellAxis` below, 1.5 deg worst deviation), so the free hand can take it where it sits. See the
        -- Kappa's config for why the other eleven leave this unset.
        handPull   = true,
        -- WHAT THE CARRIER DRAWS. The same mesh the spawned entity used, so nothing about the magazine
        -- changes on screen -- only who carries it. Read out of this weapon's own vrp_mag_*.ent rather
        -- than chosen; a weapon without this line keeps the entity route.
        mesh       = 'base\\weapons\\firearms\\handgun\\arasaka_yukimura\\entities\\meshes\\w_handgun__arasaka_yukimura__mag_std_01.mesh',

        entity     = 'base\\vrport\\vrp_mag_yukimura.ent',
        originOffset = { 0.0, 0.0, 0.0 },        -- read from the mesh matrix: the origin is already on the bone

        -- Its magazine is its own mesh -- 23.8 x 165.1 x 20.9 mm against the Kappa's 21.4 x 155.1 x 22.0 -- so the
        -- falling body is measured here rather than shared.
        drop = {
            long    = { 0, 1, 0 },
            thin    = { 0, 0, 1 },
            centre  = { 0.0, -0.00405, -0.00005 },
            flat    = 0.01045,
            arm     = 0.072,
            probe   = 0.0105,
            inertia = 0.00231,
        },

        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- Its own take: (-0.0054, -0.9999, +0.0117), worst deviation 1.5 deg. Straight out of the BACK, towards the
        -- shooter, and 0.6 deg from the Kappa's -- which is what one expects of two weapons sharing a well.
        wellAxis   = { -0.0054, -0.9999, 0.0117 },

        -- The same magazine animation drives both weapons, so the run is the Kappa's better-supported number:
        -- three takes there agreed at 134.7 / 131.7 / 135.4 mm, against a single clean take here.
        insertRun  = 0.134,
        seatAt     = 0.70,
        magnetDepth = 0.134,

        snapRadius     = 0.10,
        seatSnapRadius = 0.055,
        armDist        = 0.12,
        snapGain       = 1.6,
        seatRadius     = 0.03,
        gravity        = 9.81,
        litterTime     = 3.0,
        holdWiden      = 2.2,
        insertGain     = 2.6,
        dropPush       = 1.1,
        variantByHand  = true,

        -- THE HOLDS AND THE POSES ARE THE KAPPA'S, and that is not a transfer of convenience -- it is the same
        -- animation file. `w_handgun__arasaka_yukimura__mag_std.anims` is what the Kappa's own .app names, so both
        -- weapons' hands do literally the same thing with the magazine.
        --
        -- Measured anyway, to be sure rather than to assume: this weapon's own empty take gives the palm hold as
        -- { 0.00915, -0.03548, -0.08894 } against the Kappa's { 0.00916, -0.03557, -0.08897 } -- 0.1 mm apart, the
        -- same rotation, the fingertips within 0.7 mm and the magazine 96.2 mm from the wrist against 96.4. So the
        -- pose files are shared outright, and the pinch comes across as it stands (the take here never caught one).
        poseStages = {
            { d = 0.134, off  = {  0.00916, -0.03557, -0.08897 },
                         rot  = {  0.63083,  0.41930, -0.42412, -0.49635 },
                         pose = 'kappa_mag_left_palm',
                         off2 = {  0.01058,  0.04641, -0.01548 },
                         rot2 = { -0.04317, -0.75147, -0.04742,  0.65665 },
                         pose2 = 'kappa_mag_left_pinch' },
            { d = 0.060, off  = {  0.00916, -0.03557, -0.08897 },
                         rot  = {  0.63083,  0.41930, -0.42412, -0.49635 },
                         pose = 'kappa_mag_left_palm',
                         off2 = {  0.01058,  0.04641, -0.01548 },
                         rot2 = { -0.04317, -0.75147, -0.04742,  0.65665 },
                         pose2 = 'kappa_mag_left_pinch' },
            { d = 0.048, off  = {  0.00916, -0.03557, -0.08897 },
                         rot  = {  0.63083,  0.41930, -0.42412, -0.49635 },
                         pose = 'kappa_mag_left_palm_open',
                         off2 = {  0.01058,  0.04641, -0.01548 },
                         rot2 = { -0.04317, -0.75147, -0.04742,  0.65665 },
                         pose2 = 'kappa_mag_left_pinch_open' },
            { d = 0.041, off  = {  0.00916, -0.03557, -0.08897 },
                         rot  = {  0.63083,  0.41930, -0.42412, -0.49635 },
                         pose = 'kappa_mag_left_seated',
                         off2 = {  0.01058,  0.04641, -0.01548 },
                         rot2 = { -0.04317, -0.75147, -0.04742,  0.65665 },
                         pose2 = 'kappa_mag_left_pinch_seated' },
        },
        pose           = 'kappa_mag_left_seated',
    },
}
