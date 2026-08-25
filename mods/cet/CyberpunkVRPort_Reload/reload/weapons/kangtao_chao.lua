-- Kang Tao Chao — physical-reload configuration.
--
-- The ninth pistol, and the one that has no slide of ANY kind. Measured across every clip of its own rig animation,
-- not one bone of its frame rig translates -- there is no slide, no bolt, no charging handle and no slide stop, and
-- its sound bank has no rack event to go with them (mag_out, mag_in, mag_new_grab, grab, move, and that is all).
-- Its whole reload is the magazine, plus a COVER:
--
--   * `magazine_open` (bone 8) is the entire upper section, 9512 triangles and 121 mm of it, and it ROTATES 90 deg
--     about the bore to expose the magazine. It is the only thing that moves. See `hatch`.
--   * TWELVE bones, unique in the set so far, so the signature is unambiguous on count alone -- but it carries names
--     anyway, because two rigs have already collided on count in this project.
--   * A SMART pistol: no ejection port in its effect spawner (muzzle flashes and one `reload` effect), so no casing
--     and no `ejectFx`.
--   * Its live mesh is `base1_02`, not `base1_01`. Both exist in the archives; only the second is what any of its
--     eight appearances reference, and the first would have been patched into a file nothing loads.
return {
    id    = 'kangtao_chao',
    match = 'kangtao_chao',

    -- Its own bank. There is no slide sound because there is no slide; `magOut` doubles as the cover's noise, which
    -- is what the game does too -- in its own reload the cover swings on the same frame mag_out plays.
    sounds = {
        magOut  = 'w_gun_pistol_smart_chao_mag_out',
        magIn   = 'w_gun_pistol_smart_chao_mag_in',
        magGrab = 'w_gun_pistol_smart_chao_mag_new_grab',
    },

    -- MANDATORY here, not a preference. The slot-derived basis needs the magazine slot to sit well OFF the barrel
    -- axis, and on this weapon it sits exactly ON it: mag_slot (0, 27.4, 69.8) and muzzle_slot (0, 27.4, 157.0) share
    -- both other coordinates, so the perpendicular residual is zero and the basis is not merely unstable but
    -- undefined. The weapon entity's own orientation is exact and free.
    basis = 'weapon',

    rigs = {
        { which = 1, bones = 12, names = { { 8, 'magazine_open' }, { 11, 'mag_slot' }, { 9, 'spinner' } } },
        { which = 0, bones = 5,  names = { { 3, 'mag_std' }, { 4, 'mag_stdr' } } },
    },

    -- Both of these sit exactly on the bore (same X, same Y, 87.2 mm apart in Z), so the forward axis comes out as
    -- (0, 0, 1) with no residual at all. Every other weapon in the set anchors `back` on its slide; this one has
    -- none, and the magazine slot is the better answer anyway -- it is on the line and it never moves.
    barrel = { front = 'vrp_muzzle_slot', back = 'vrp_mag_slot' },

    -- NO `slide` SECTION AT ALL. The module treats that as "this weapon has no slide": nothing is read, nothing is
    -- written, no grip, no preview, no stop, and the right stick click has nothing to release. It is the first
    -- config to leave it out, and the first weapon that honestly has nothing to put in it.

    -- THE COVER. Driven by the magazine's own state -- open whenever the magazine is anywhere but home, shut the
    -- moment it seats -- which is what the game's animation does, only keyed to the player instead of to a clip.
    --
    -- Measured off `magazine_open` in `empty_reload`, and identical in every reload variant: +90.0 deg about the
    -- rig's local Z (the bore), open in 0.167 s, shut in 0.133. The 6 deg wind-up is the animation's own: it rocks
    -- that far the WRONG way over the first two frames before it swings.
    --
    -- The axis is the rig's, not the GLB's -- those differ, and here by a whole axis: the export's Y is the rig's Z.
    -- Checked bone by bone against the .rig bind pose (magazine_open sits at rig (0, 23.3, -35.5), exported as
    -- (0, -35.5, -23.3)), so a rotation the GLB reports about Y is a rotation about the bore.
    hatch = {
        which = 1, bone = 8,
        axis      = { 0, 0, 1 },
        angle     = 90.0,
        sign      = 1.0,          -- the animation's quaternion is +0.7071 about +Y(GLB) = +90 about +Z(rig)
        windUp    = 6.0,
        openTime  = 0.167,
        closeTime = 0.133,
    },

    mag = {
        enabled    = true,
        slot       = 'vrp_mag_slot',
        which      = 0,
        showTrack  = 0,
        -- WHAT THE CARRIER DRAWS. The same mesh the spawned entity used, so nothing about the magazine
        -- changes on screen -- only who carries it. Read out of this weapon's own vrp_mag_*.ent rather
        -- than chosen; a weapon without this line keeps the entity route.
        mesh       = 'base\\weapons\\firearms\\handgun\\kangtao_chao\\entities\\meshes\\w_handgun__kangtao_chao__mag_std_02.mesh',

        entity     = 'base\\vrport\\vrp_mag_chao.ent',

        -- ITS MAGAZINE IS A CYLINDER: 176.9 mm long and 31.7 across, round to within a tenth of a millimetre over
        -- the whole length (measured off the vertices, not guessed from the silhouette). Every other magazine in the
        -- set is a flat box. It is also built along a different axis -- long along local Y, where the rest are long
        -- along Z -- which matters for `drop` below and for `originOffset`, whose calibrated rule (the difference of
        -- bounding-box centres against the Tamayura) cannot cross a frame that is turned. Left at zero until the
        -- headset says otherwise; a miss at the SEAT is what this knob answers, and only along the magazine itself.
        --
        -- WHICH FRAME THOSE NUMBERS ARE IN is the trap here, and it nearly took this weapon's drop with it. A mesh
        -- JSON reports two different things: `boundingBox` is in the MESH's own space, while the vertex data, once
        -- `cook_weapon.decode` has applied the bone matrices, is in the BONE's. On this magazine the two differ by a
        -- whole axis -- bbox says long along Y, the decoded vertices say Z. The spawned entity draws the raw mesh,
        -- so everything below is the BOUNDING BOX's frame, and the decoder is only good for the cross-section.
        originOffset = { 0.00000, 0.00000, 0.00000 },

        -- A LONG THIN STICK, and the falling-magazine solver has to be told so. Its defaults assume the family's
        -- shape (long along local Z, thin across local X); on this one the probes would all sit inside the body and
        -- both ends would go through the floor.
        --   arm    70 mm  -- half-length 88.5 minus the probe, so three probes cover it end to end
        --   probe  18 mm  -- its cross-section is 32 x 32, so this is the body's own half-thickness
        --   centre       -- the bounding box's middle, which is what it tumbles about
        --   inertia      -- (0.177^2 + 0.0316^2)/12, per unit mass
        drop = {
            long    = { 0, 1, 0 },
            thin    = { 0, 0, 1 },
            centre  = { 0.0, -0.00365, -0.0044 },
            flat    = 0.0158,
            arm     = 0.070,
            probe   = 0.018,
            inertia = 0.0027,
        },

        holdSlot   = 'WeaponLeft', holdSlotRight = 'WeaponRight',

        -- TAKING IT AND PUSHING IT IN ARE NOT THE SAME WRIST. The hold below is the insertion's, settled in the
        -- headset: palm up, and the right end of the magazine going in first. Reaching for one still in the gun
        -- wants a different hand, and it can have one for free -- at that moment the magazine is the game's own,
        -- seated and untouched, so this rotation moves nothing but the wrist the preview aims for.
        --
        -- The value is not a guess: it is exactly what turns the shipped hold into the frame-correct composition of
        -- the MEASURED one (the take's relation, composed with the bone-vs-mesh turn), which is the orientation the
        -- preview looked right in. 177.2 deg about the magazine's own X -- across it, not along it. A roll along its
        -- length was tried first and was wrong in the other direction ("ладонь смотрит на меня"): a cylinder's four
        -- 180-degree relations all look the same on the magazine and all feel different in the hand, so the only
        -- way through is to derive each from a state that was judged, rather than to turn things until one fits.
        previewRot = { -0.99971,  0.00001,  0.00000,  0.02417 },

        -- EITHER WAY UP. The gun's own hand inserts palm over the top, along the guide, and `holdRotBasis` below is
        -- that. But the magazine is a tube: pushing it in palm-down is just as real a way to do it, and the module
        -- takes whichever of the two the hand is actually nearer, so neither has to be learned.
        --
        -- WHICH 180 IT IS took measuring. A tube has three of them and they are not interchangeable:
        --     about its own length (Y)  ends kept      palm flipped
        --     about X                   ends SWAPPED   palm flipped   <- this one
        --     about Z                   ends swapped   palm kept
        -- The length was tried first and was wrong in a way worth writing down: the palm turned over but the
        -- magazine went on pointing the same way, so the hand had to come at it from the far end and the arm wound
        -- round behind ("рука выворачивается с обратной стороны"). Turning the hand over turns the magazine over
        -- with it, which is what X does. Which axis carries the palm is not a guess either -- solving for where the
        -- wrist sits in the magazine's own axes puts it 55.9 mm along the tube and 13.5 mm off it towards Z, so Z is
        -- the palm side, and the flip that moves BOTH is the one about X.
        rollAlt = { 1.0, 0.0, 0.0, 0.0 },

        -- The family's 1.1 m/s, now that the axis IS measured -- and on this weapon that sends the magazine out
        -- FORWARD, past the muzzle, which is where its own animation sends it too.
        dropPush   = 1.1,

        -- MEASURED, from its own two takes (ordinary and empty reload, 233 and 264 frames).
        --
        -- THE WELL RUNS ALONG THE BARREL. Not down, not up: the magazine's displacement from its seat lies on the
        -- bore to within 0.0 deg over the frames 10-60 mm out, in both takes, and the bore itself measures
        -- (0.0006, 1.0000, -0.0004) from the well to the muzzle. So this magazine slides out FORWARDS, lengthwise,
        -- once the cover is off it -- which is the only way a 177 mm cylinder could ever leave a 157 mm gun. Every
        -- other weapon in the set drops its magazine downward out of the grip; the family average would have been
        -- 90 deg wrong here, which is exactly why it is measured and not inherited.
        -- ORIENTING A CYLINDER TAKES THREE FACTS, and the eye can only supply one of them. The take measures the
        -- magazine BONE while the module orients the spawned MESH, and on this weapon those frames differ by a real
        -- 180 deg: the `magazine` bone carries a bind rotation about (0, .707, .707) -- in its rig, confirmed live
        -- (at rest the bone reads back exactly its bind, so the pose buffer holds the whole parent-local transform),
        -- and the mesh's own mesh->bone matrix is the same turn.
        --
        -- The three facts, and where each came from:
        --   * WHICH LINE the magazine lies on in the hand -- the eye's, +/- 87.23 deg about X in the tuner. Visible.
        --   * WHICH END points forward -- also the eye's, and it took a wrong guess to find out: composing the
        --     measured value with the frame turn put the body on the same line the other way round, and that DID
        --     show ("не той стороной заходит"). So the tuner's end is the right one.
        --   * THE ROLL about its own length -- nobody's eye, because a tube looks identical at every roll. It is
        --     also the one that matters most: the wrist is solved by INVERTING this relation, so a free roll comes
        --     out as a turned hand. Palm down at one value, palm up 180 deg away, and the magazine identical in
        --     both. Hence the final `* 180 deg about the magazine's own Y` below -- it moves nothing you can see
        --     and everything you can feel.

        holdOffset   = {  0.01389, -0.02206, -0.05128 },
        holdRotBasis = { -0.59687, -0.16866, -0.22504, -0.75144 },
        wellAxis     = {  0.0005,   1.0000,  -0.0048  },
        insertRun    = 0.120,

        -- THE MAGNET MAY NOT ACT OUTSIDE THE RUN THE GAME ITSELF TAKES. The default window is 1.6 x the run, which
        -- here is 192 mm -- and this magazine is 177 mm long and goes in along its own length, so the pull was
        -- reaching out to where the magazine has not even met the gun and taking the hand while the player was
        -- still lining it up. Cut to the measured run itself.
        magnetDepth  = 0.120,

        -- WHERE THE HAND LETS GO, and this one is measured rather than judged by eye. On every other weapon the
        -- hand rides the magazine all the way home and `seatAt` is a matter of taste; here the game's own hand
        -- releases it 58 mm short -- the fingers snap open (111/106/102/97 mm from the wrist to 145/145/135/122)
        -- and the wrist walks away while the magazine covers the last 58 mm by itself. It is a smart gun; it takes
        -- its own magazine. 58 mm of a 120.7 mm run is 0.52, so that is the number.
        seatAt       = 0.52,

        -- ...and the gun takes it from there. Without this the click is also the arrival, so the magazine vanishes
        -- from the hand and reappears in the well in one frame -- a teleport, which is exactly how it read. With
        -- `selfSeat` the hand is freed at the click and the magazine keeps travelling under nobody's control until
        -- it is home; only then does the game's own magazine come back and the rounds go in.
        --
        -- 0.11 s is the animation's: from the frame the fingers open, the magazine dwells about a tenth of a second
        -- and then covers the last 58 mm in another tenth (57.9 mm at t=2.71, 29.4 at 2.75, 1.7 at 2.80).
        selfSeat     = { time = 0.11 },

        -- ONE CARRY, no variants. Measured, the two takes hold it 154 deg apart about a transverse axis -- roughly
        -- end for end -- and 22 mm apart along its length, but on a plain cylinder that is not something an eye can
        -- see, and the user's own verdict from the headset was that every reload looks the same. So the ordinary
        -- reload's carry is the one, and it is the better measurement anyway: 26 frames, 6.1 mm of spread and
        -- 0.55 deg, against 8.55 deg for the empty one.
        --
        -- The ladder is SHORT because the hand does only two things. It carries with one unchanging grip down to
        -- the release, and the release is the seat -- so any stage below 58 mm is a stage the magazine never
        -- reaches, and the open hand is only ever seen as the preview.
        poseStages = {
            { d = 0.120, pose = 'chao_mag_left_carry' },
            { d = 0.058, pose = 'chao_mag_left_carry' },   -- the same grip, all the way to the release
            { d = 0.000, pose = 'chao_mag_left_open' },
        },
        pose           = 'chao_mag_left_open',

        snapRadius     = 0.10,
        seatSnapRadius = 0.055,
        armDist        = 0.12,
        snapGain       = 1.6,
        seatRadius     = 0.03,
        gravity        = 9.81,
        litterTime     = 3.0,
        holdWiden      = 2.2,
        insertGain     = 2.6,
    },
}
