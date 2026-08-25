-- Militech Omaha — physical-reload configuration.
--
-- The eighth pistol, and the first with NO SLIDE GRIP AT ALL. Its own animations never put a hand on the slide --
-- measured across both takes, the left wrist never comes within 131 mm of it -- so there is nothing to record and
-- nothing to invent: the slide is closed with the right stick click, the way the other pistols' catches already are.
-- The user's call, and the weapon's own behaviour.
--
--   * TEN bones, exactly like the Kenshin's rig and nothing like it otherwise: `barrel_front` (3) is the piece that
--     travels 51.0 mm, `barrel_middle` (2) the long top part that rides 18.4 with it, and the two are SIBLINGS under
--     `barrel`, so each is written on its own. This is the second weapon that would have been mis-identified on bone
--     count alone -- see the signature-table fix in the plugin.
--   * A TECH PISTOL, like the Kenshin: no `ejection_port` anywhere in its effect spawner (charging, charged,
--     discharge and a reload diode instead), so no casing and no `ejectFx`.
--   * Its magazine sits on the SHARED handgun rig (`magazine` is bone 2 there), and its mesh origin is offset from
--     the family's convention -- see `originOffset`.
return {
    id    = 'militech_omaha',
    match = 'militech_omaha',

    -- Its own bank, 64 events. `chamber_slide_back` is in every empty reload; `safe_handle_open` / `_close` belong
    -- to `safe_action`, the clip where the hand works the front piece and nothing else moves. There is no separate
    -- "slide home" event, so the close borrows the safe handle's -- which is the same part returning.
    sounds = {
        slidePull    = 'w_gun_pistol_tech_omaha_chamber_slide_back',
        slideRelease = 'w_gun_pistol_tech_omaha_safe_handle_close',
        magOut       = 'w_gun_pistol_tech_omaha_mag_out',
        magIn        = 'w_gun_pistol_tech_omaha_mag_in',
        magGrab      = 'w_gun_pistol_tech_omaha_mag_new_grab',
    },

    -- MEASURED: the magazine slot sits 9.3 mm off the bore line, so a basis built on it would swing on a lever arm
    -- of millimetres. The weapon's own orientation is exact and free.
    basis = 'weapon',

    rigs = {
        { which = 1, bones = 10, names = { { 3, 'barrel_front' }, { 2, 'barrel_middle' }, { 6, 'mag_slot' } } },
        { which = 0, bones = 5,  names = { { 2, 'magazine' }, { 4, 'mag_std' }, { 3, 'mag_stdr' } } },
    },

    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_barrel_front' },

    slide = {
        slot       = 'vrp_barrel_front',
        which      = 1, bone = 3,
        -- Measured twice and agreeing to 0.1 mm: the rig animation's clips give 123.2 -> 72.2 mm, and the recorded
        -- take gives the same, with the slide slot's distance to the muzzle spanning 55.5 -> 106.6 mm as it goes.
        travel     = 0.0510,
        restLocal  = { 0.00000,  0.02820,  0.12320 },
        -- THE SLIDE STOP is the full stroke on this weapon -- the bone sits at 72.2 mm for the whole of an empty
        -- reload, with `barrel_middle` back at 46.7 beside it.
        lockLocal  = { 0.00000,  0.02820,  0.07220 },
        localAxis  = { 0, 0, -1 },
        sign       = 1.0,
        -- MEASURED off the recorded release: 51 mm home in 0.11 s, 0.43 m/s average and 0.57 at its quickest.
        releaseSpeed = 0.55,
        blendTime  = 0.15,
        pullSndAt  = 0.20,

        -- NO `grips`, and that is deliberate rather than unfinished. The module reads this as "the slide cannot be
        -- taken by hand": no preview, no grab, no wrist snap. The stop is still detected and still held off the
        -- catch by the graph claim, and the right stick click closes it -- which is the whole interaction this
        -- weapon has, and the whole interaction the game gives it.

        -- The long top part rides at a CONSTANT fraction, measured live and identical in the rig animation:
        -- 18.4 mm against 51.0, every frame it is back. It leads slightly on the way home (about 50 ms in the
        -- animation, and the same in the take), and that is left out: a delay line can lag, not lead, and 18 mm of
        -- part arriving two frames early is not worth pretending about.
        riders = { { bone = 2, ratio = 0.360 } },
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        showTrack  = 0,
        -- WHAT THE CARRIER DRAWS. The same mesh the spawned entity used, so nothing about the magazine
        -- changes on screen -- only who carries it. Read out of this weapon's own vrp_mag_*.ent rather
        -- than chosen; a weapon without this line keeps the entity route.
        mesh       = 'base\\weapons\\firearms\\handgun\\militech_omaha\\entities\\meshes\\w_handgun__militech_omaha__mag_std_01.mesh',

        entity     = 'base\\vrport\\vrp_mag_omaha.ent',

        -- ITS MESH ORIGIN IS NOT WHERE THE FAMILY PUTS IT: the body runs Z -151.6..-7.4 mm, so the face that enters
        -- the well sits 7.4 mm BELOW the origin, where the family's sits above it (the Tamayura, which ships with no
        -- offset at all, has its mouth 5.9 mm above). The origin therefore has to ride 13.3 mm higher for the two
        -- mouths to land in the same place, along the magazine's own +Z -- the direction INTO the well, since its -Z
        -- is the measured way out.
        --
        -- WHAT THIS KNOB IS NOT: a place to put an eyeballed miss. It was briefly given -60 mm along the BORE to
        -- answer "the magazine goes in 6 cm too far forward", and that is 60 mm in a direction a mesh correction
        -- cannot have -- the offset says where this mesh's origin sits inside its own body, and a body does not
        -- move sideways within itself. The result was the miss doubling the other way ("на целый магазин назад").
        -- A miss in the INSERTION belongs to the hold and to the well axis; this one belongs to the mesh.
        -- ...plus 5 mm forward along the bore, from the headset in two passes: 10 mm overshot by half a centimetre,
        -- so half of it is the answer. It lands almost exactly on what the bounding-box CENTRE rule asked for on this
        -- component (+6.85 mm) and for the same reason -- the Omaha's magazine is 9 mm deeper front to back than the
        -- reference's -- so the eye and the geometry agree here to under 2 mm.
        originOffset = { 0.00000, 0.00500, 0.01330 },

        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- MEASURED on the ordinary reload, over the frames where the magazine is home and the hand still on it:
        -- offset steady to 4.4 mm, rotation to 1.89 deg.
        -- The 40 mm "closer to the hand" step is REVERTED: moving along the line between them walks the magazine
        -- toward the WRIST, which is precisely what it was then reported as sitting on. Where in the hand it should
        -- lie is a judgement the headset has to make -- the overlay tuner, as on the Kenshin and the Lexington.
        -- ...then placed by eye in the headset and printed from the game: += { -0.028, +0.022, +0.006 } m and
        -- -6.05 / +1.51 / -12.60 deg, composed in the tuner's own order. The finger corrections from the same
        -- print are baked into the CARRY pose, which is the one that was on the hand when it was taken.
        holdOffset   = { 0.11132, 0.02750, -0.05040 },
        holdRotBasis = { -0.04971, 0.76256, 0.03482, 0.64407 },

        snapRadius     = 0.10,
        seatSnapRadius = 0.055,
        armDist        = 0.12,
        snapGain       = 1.6,
        seatRadius     = 0.03,
        gravity        = 9.81,
        litterTime     = 3.0,
        holdWiden      = 2.2,
        insertGain     = 2.6,

        -- THE WELL AXIS IS NOT JUST THE WELL'S DIRECTION -- it is also the LINE the module expects the hand to
        -- travel along, and that is what settles which number belongs here.
        --
        -- This weapon cannot supply its own: the whole insertion is THREE frames (122 -> 86 -> 32 -> 5 mm at
        -- 2.8 m/s), far too fast for 45 Hz, and fitting a line to it gives 70 deg off vertical, which is nonsense
        -- for a magazine well. The geometric answer IS available and exact -- with the magazine seated, its own long
        -- axis reads (0, 0, -1) in the weapon's frame over 174 frames with no spread, and the same holds on the
        -- Kenshin, the Liberty and the Lexington to four decimals -- but shipping it was a mistake and the log said
        -- so plainly: the magnet's weight stayed 0.000 for every frame of every attempt, because `perp` is measured
        -- from that line and a hand coming in from below and behind is 44 mm off a vertical one at 150 mm of depth,
        -- against a 55 mm catch radius. It never bit, and an unguided magazine wanders.
        --
        -- So: the three weapons whose axes were fitted to their own animations all sit 16-19 deg back from vertical
        -- and agree with each other to about two degrees -- the tilt is how a HAND comes at a well, not a property
        -- of any one gun. Their average is what this weapon uses.
        wellAxis   = { -0.0054, -0.3021, -0.9533 },
        -- MEASURED: the visible approach runs about 125 mm before the seat.
        insertRun  = 0.125,
        -- 0.75 at the user's eye, after 0.70 and 0.80: the magazine clicks home with a quarter of the run showing,
        -- 31 mm of depth. The palm stage sits at 45 mm, so the hand is flat on the base 14 mm before the click.
        seatAt     = 0.75,

        -- Its own hand, through its own insertion. Measured by fingertip-to-wrist distance: carried with the ring
        -- and little fingers closed (66 and 55 mm) and the index out, opening through 94/85 at 36 mm of depth and
        -- 122/111 at 11, flat by the seat (136/123).
        -- DEPTHS REMAPPED ONTO THE BAND THE HAND TRAVELS, which is the trap this set keeps falling into: `seatAt`
        -- 0.70 of a 125 mm run clicks the magazine home at 37.5 mm, so a ladder written at the depths the ANIMATION
        -- shows -- its fingers only open between 36 mm and the seat -- asks for its last two poses below the point
        -- where the magazine is already gone. On screen that is "the palm push never happens", and the little that
        -- does happen crawls, because only the first two stages are ever reached.
        --
        -- The pose FILES are the animation's, unchanged; the numbers below spread that same sequence over 90 -> 38 mm
        -- so the flat palm arrives exactly as the magazine seats.
        poseStages = {
            { d = 0.125, pose = 'omaha_mag_left_d125' },   -- carried in
            { d = 0.090, pose = 'omaha_mag_left_d125' },   -- still the carry
            { d = 0.075, pose = 'omaha_mag_left_d080' },   -- closing on the well
            { d = 0.060, pose = 'omaha_mag_left_d035' },   -- the fingers opening off it
            { d = 0.045, pose = 'omaha_mag_left_d000' },   -- flat palm, 20 mm before the click
            { d = 0.000, pose = 'omaha_mag_left_d000' },   -- seated, and the preview
        },
        pose           = 'omaha_mag_left_d000',
    },
}
