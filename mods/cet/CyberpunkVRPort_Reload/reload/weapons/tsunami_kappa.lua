-- Tsunami Kappa — physical-reload configuration.
--
-- The tenth pistol, and the first that owns NO rig and NO animations of its own: its .app, its appearance package
-- and its .ent all name the Arasaka Yukimura's, and only the meshes are the Kappa's. So the rig signature is shared
-- between the two weapons and each config decides for itself what to drive.
--
-- Its magazine goes in at the BACK, where a slide would be, and comes straight out towards the shooter -- measured
-- three times, see `wellAxis`. A smart pistol: no ejection port in its spawner, so no casing.
--
-- NO `slide` SECTION, and on this weapon that is a statement about the MESH rather than about the rig. The rig does
-- carry a `slider` (bone 7, the front piece, 17.0 mm of travel, back on an empty gun) and the Yukimura hangs 2096
-- triangles off it -- but all six of the Kappa's own body meshes skin to `barrel` and `gun_trigger` only, so on this
-- weapon that bone moves nothing at all. Driving it would be writing to a part that is not there.
return {
    id    = 'tsunami_kappa',
    match = 'kappa',

    -- The Yukimura's bank, which is what the Kappa plays. `load` belongs to the empty reloads only -- it is the
    -- front piece going home, and with nothing skinned to it there is nothing to play it for.
    sounds = {
        magOut  = 'w_gun_pistol_smart_yukimura_mag_out',
        magIn   = 'w_gun_pistol_smart_yukimura_mag_in',
        magGrab = 'w_gun_pistol_smart_yukimura_mag_new_grab',
    },

    -- No slide slot means no slot-derived basis to build; the weapon's own orientation is exact and free.
    basis = 'weapon',

    rigs = {
        { which = 1, bones = 8, names = { { 7, 'slider' }, { 6, 'mag_slot' }, { 5, 'gun_trigger' } } },
        { which = 0, bones = 5, names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
    },

    -- The closest pair of slots to the bore this rig offers: muzzle_slot (0, 8.0, 192.9) and slider (0, 9.2, 129.2)
    -- are 1.1 deg apart from the true bore, against 2.4 for the barrel origin and 3.5 for the magazine slot. It
    -- matters little here -- with no slide and a weapon-framed basis the barrel axis is barely used -- but both
    -- slots must resolve or the module returns early, and these two do.
    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_slider' },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        showTrack  = 0,

        -- TAKEN OUT BY HAND, and on this weapon that is what the geometry says. The rest of the set holds its
        -- magazine in a well under the grip, behind a catch: the only way one comes out is the button, and
        -- reaching in with the left hand to pull it is a move that does not exist -- so they leave `handPull`
        -- unset and a squeeze at the well does nothing. Here the magazine leaves out of the BACK, straight
        -- towards the shooter (`wellAxis` below, within 0.9 deg of the reversed bore in all three takes), which is
        -- exactly where the free hand already is. So this one may be gripped and drawn out where it sits.
        --
        -- The button drop is untouched and still works; this only adds the second way in.
        handPull   = true,
        -- WHAT THE CARRIER DRAWS. The same mesh the spawned entity used, so nothing about the magazine
        -- changes on screen -- only who carries it. Read out of this weapon's own vrp_mag_*.ent rather
        -- than chosen; a weapon without this line keeps the entity route.
        mesh       = 'base\\weapons\\firearms\\handgun\\tsunami_kappa\\entities\\meshes\\w_handgun__tsunami_kappa__mag_std_01.mesh',

        entity     = 'base\\vrport\\vrp_mag_kappa.ent',

        -- Read from the mesh, not fitted: `boneRigMatrices[0].W` is zero, so the mesh's origin and its bone's
        -- coincide and nothing needs correcting. (Its ROTATION is the familiar 180 deg about (0, .707, .707) --
        -- the same bone-vs-mesh turn every magazine in this set carries, and it is composed into the holds below.)
        originOffset = { 0.0, 0.0, 0.0 },

        -- A FLAT STICK, 21.4 x 155.1 x 22.0 mm, long along its own Y. The falling-magazine solver assumes the older
        -- shape (long along Z) and has to be told.
        --   arm    66 mm  -- half-length 77.5 less the probe, so three probes cover it end to end
        --   probe  11 mm  -- its own half-thickness
        --   inertia       -- (0.1551^2 + 0.0214^2)/12, per unit mass
        drop = {
            long    = { 0, 1, 0 },
            thin    = { 1, 0, 0 },
            centre  = { 0.0, -0.00085, 0.0014 },
            flat    = 0.0107,
            arm     = 0.066,
            probe   = 0.011,
            inertia = 0.00204,
        },

        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- STRAIGHT OUT OF THE BACK. The magazine's displacement from its seat lies on the weapon's own -Y -- the
        -- bore, reversed -- to within 0.9 deg, in all three takes independently. So it leaves towards the shooter,
        -- which is the opposite end from the Kang Tao Chao and nothing like the rest of the set, who drop theirs
        -- out of the grip. The value is the mean of the three.
        wellAxis   = { -0.0029, -1.0000, 0.0024 },

        -- The stretch over which the approach still lies on that line: 134.7, 131.7 and 135.4 mm across the three
        -- takes. Beyond it the hand is still swinging the magazine round.
        insertRun  = 0.134,
        seatAt     = 0.70,

        -- ...and the magnet may not reach past that run. The default window is 1.6x it, which on a magazine that
        -- slides in along its own 155 mm would have the pull taking the hand before the two have met.
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

        -- WHICH GRIP IS CHOSEN BY WHERE THE HAND IS, not by a coin. The Lexington's two carries are
        -- interchangeable and it picks at random; these two are not -- the pinch takes the magazine by its rear
        -- END, which sticks out of the back of this gun, and the palm goes round the body 46 mm further along. So
        -- the module solves the wrist each grip would need and takes whichever is nearer the hand that is there.
        variantByHand = true,

        -- TWO GRIPS, both the game's own.
        --
        --   * the PALM: the whole hand round the magazine, fingers closed at 69/64/64/70 mm from the wrist, the
        --     magazine's mount 96.4 mm out. MEASURED TWICE -- the ordinary reload and the empty one, recorded
        --     separately -- and the two offsets agree to 0.07 mm, which is the best cross-check this set has had.
        --   * the PINCH: the index held OUT at 146.8 mm while middle, ring and little take it at 91/81/80, and the
        --     magazine 46 mm closer to the wrist at 50.3. A genuinely different hand, not a variant of the first.
        --
        -- Both rotations are composed with the bone-to-mesh turn already: what a take measures is the magazine's
        -- BONE, what the module orients is the spawned MESH, and on this rig those differ by 180 deg.
        --
        -- The hold does not change through the carry -- offsets steady to 3.8 and 7.8 mm over 26 and 33 frames --
        -- so every stage carries the same pair, and only the fingers move down the ladder. And unlike the Chao the
        -- hand does NOT let go early: it holds the magazine to the seat and opens as it arrives, so there is no
        -- `selfSeat` here and the click is the arrival.
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
