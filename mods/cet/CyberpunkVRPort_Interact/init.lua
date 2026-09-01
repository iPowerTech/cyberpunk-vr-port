-- CyberpunkVRPort — reach out and take it: the hand does what X does.
--
-- WHAT THIS IS. When the game is already offering an interaction -- "pick up", "open", "press" --
-- putting a hand on the thing it is offering it FOR and squeezing the grip presses X. Devices with
-- no grip at all: a hand laid on an elevator button, a terminal or a crate holds X by itself, the
-- way a hand on a real button does.
--
-- WHAT THIS IS NOT, and the rule the whole module is built around:
--
--     THE GAME'S OWN X DECIDES. THE HAND ONLY CHOOSES WHEN.
--
-- THE CAMERA RAYCAST LINE THAT USED TO BE HERE IS NO LONGER TRUE, and the correction matters more
-- than the sentence: the engine's look-at ray is now cast from the HAND, at its source, by
-- src/Hooks/LookAtRayFromHand.cpp. What has NOT changed is the rule -- this module still never
-- invents an interaction, never reaches something the game is not already offering, and never fires
-- on an object other than the focused one. It decides WHEN, and the engine still decides WHAT. Look away and the mask drops
-- to zero: the grip goes straight back to meaning what it always meant (holster equip, magazine,
-- ball) and the pad's own X still works as it always did. Nothing here takes anything away -- it
-- adds a second, physical way to press a button that is already being offered.
--
-- HOW THE HAND IS JUDGED TO BE "ON" SOMETHING, in two tests, either of which is enough:
--
--   * ORIGIN -- the palm is within RADIUS_IN of the focused entity's own position. Exact, and right
--     for anything whose origin IS its surface: a shard, a bottle, a can on a table.
--   * SURFACE -- the palm is within the same radius of the point where the line of sight LANDS, found
--     by one cast along the FPP camera's forward. This carries doors, elevator panels and terminals,
--     whose origins sit metres from the part you actually touch (measured in the field: 2.4 m, 4.7 m,
--     9.1 m, while the hand was correctly 1.2 m from the player).
--
-- The alternative -- letting the hand find its own interactables, independent of gaze -- needs the
-- interaction executed directly on the entity from redscript, because a synthesised X can only ever
-- fire the hub the engine has already focused. That is a bigger feature and a separate one.
--
-- Bridge: SetVRInteract(armedMask, press) -> shared[166] (which hands are on it, for the other CET
-- mods whose grip this would otherwise steal) and shared[167] + a 250 ms deadline in the plugin,
-- which the XInput merge turns into XINPUT_GAMEPAD_X.

-- ---- settings ---------------------------------------------------------------------------------
local ENABLED      = true
-- How near the focused object's ORIGIN the palm has to be. An origin is not a surface: a crate's is
-- its centre and a door's is its hinge, so this is deliberately generous. Too small and big objects
-- become unreachable; too large and the hand "arrives" while it is still visibly short of the thing.
-- 0.45, RAISED FROM 0.35 after the surface cast went in. Against an entity ORIGIN the old figure was
-- generous; against the point the line of sight LANDS on it was not, because the two are not the same
-- kind of target. The cast lands where you are LOOKING, and a hand reaches for the part of the panel
-- it wants -- a button off to one side of the gaze, a handle below it -- so the gap between the two
-- is real even when the hand is touching the thing. At 0.35 the field test had to push the hand into
-- the object up to the wrist before it armed, i.e. the player had to clip through geometry to press
-- a button. A palm arriving at a surface is the gesture; sinking into it is a side effect nobody
-- should have to perform.
local RADIUS_IN    = 0.45
-- THE AUTOMATIC HALF NEEDS A STRICTER REACH THAN THE DELIBERATE ONE, and a braindance loop is what
-- proved it. A grip press has a confirmation in it -- the squeeze -- so 45 cm of slack is generous
-- rather than dangerous. A device presses itself, so the proximity IS the whole decision, and 45 cm
-- is not "a hand on a panel", it is "a panel somewhere in front of me".
--
-- It became dangerous when the contact point moved onto the engine's look-at landing point, because
-- that ray now leaves the PALM: measuring palm-to-landing-point is then just measuring the ray's
-- length, and "the hand is on it" degenerates into "something is within reach in front of the hand".
-- Sitting in front of a braindance reader with both controllers on the desk satisfied that forever:
-- press, scene, exit, press again -- three re-entries with nobody touching anything.
--
-- 0.15 m is touching. It is the number that makes the automatic press mean what its name says.
local RADIUS_TOUCH = 0.15
local RADIUS_OUT   = 0.52   -- release distance. The gap is hysteresis: without it a hand held at
                            -- exactly the radius chatters the button on and off every frame.
-- THE ORIGIN TEST ALONE DOES NOT REACH HALF THE GAME, and the first field test proved it. Shards and
-- loose props worked; every door panel and elevator call button did nothing, and the log said why:
-- the palm sat 1.2 m from the player, correctly placed, while the focused entity's origin was 2.4 m,
-- 4.7 m, even 9.1 m away. A door's origin is its hinge and an elevator's is the car. Measuring to an
-- origin can only ever work for objects small enough that their origin IS their surface.
--
-- THE SURFACE, THEN -- and the way to it is a ray along the line of sight, which is the same ray the
-- game used to choose the interaction. Where it lands is the panel, the button, the terminal screen:
-- the thing the hand is actually reaching for. That point replaces the origin and everything else
-- stays as it was, distance and hysteresis included.
--
-- A PLAIN "IS THE HAND NEAR THE LINE OF SIGHT" TEST WAS TRIED FIRST AND IS WRONG. It shipped for one
-- test and armed everything: a line has no end, so a hand held anywhere in front of the face counted
-- as being on a panel across the room, and with devices pressing themselves the doors opened from
-- three metres away. Distance along the ray cannot be guessed -- it has to be measured, which is
-- exactly what the cast does.
local HIT_MAX_DIST = 3.0    -- how far down the line of sight to look for the surface, metres
-- WHERE THE SURFACE CAST BEGINS, and it depends on WHOSE ray it is.
--
-- 0.12 was chosen when this ray left the CAMERA, to keep the cast from hitting the player's own
-- body. When the ray moved to the palm the constant came along unexamined, and it broke the very
-- gesture it is there to serve: with a hand laid ON a car door, the cast started twelve centimetres
-- further along -- INSIDE the car, past the panel -- so it never found the door, the hand was never
-- armed, and the grip did nothing. The tell was that rotating the hand while holding the grip DID
-- fire it: the rotation swings those twelve centimetres back outside the bodywork, and the cast
-- finds the door again from the outside.
--
-- A hand is not the player's chest, so it needs no clearance in front. It needs the opposite: the
-- cast starts slightly BEHIND the palm, so a surface the hand is already touching -- or has sunk a
-- few centimetres into -- is still in front of the ray and still gets hit. The camera keeps its
-- original clearance, since the reason for that one has not changed.
local RAY_START      = 0.12    -- camera ray: clear the player's body
local HAND_RAY_START = -0.10   -- hand ray: begin behind the palm, so a touched surface is still ahead
-- THE CONTACT POINT SITS IN FRONT OF THE SURFACE, NOT ON IT. The cast lands exactly on the geometry,
-- and measuring the palm against that point means the hand has to arrive AT the surface -- which in
-- practice means pushing it INTO the object, because the palm's own centre is a few centimetres
-- behind the skin of the hand. The player ends up clipping a hand through a terminal to press it,
-- which is a gesture nobody should have to perform to be understood.
--
-- So the point is pulled back along the ray, towards the hand. Six centimetres is roughly the palm
-- centre to the fingertips: the hand now arrives when it LOOKS like it has arrived. It is not extra
-- reach -- the radius is unchanged -- it is the same radius measured from where the hand actually
-- touches rather than from where the ray stopped.
local SURFACE_BACKOFF = 0.06
-- HOW FAR THE LANDING POINT MAY SIT FROM THE FOCUSED OBJECT'S ORIGIN before it is treated as
-- belonging to something else. Deliberately loose -- a door's origin is its hinge and a lift's is
-- the car, several metres from the panel you actually touch, and that gap is why the surface test
-- exists at all. It is here to reject a point on the floor or the desk, not to tighten the reach.
local SURFACE_OWNERSHIP = 3.0
-- Devices press themselves. Off = everything, devices included, waits for a grip.
local AUTO_DEVICES = true
-- How long an automatic press is HELD, seconds. Not a nicety: most device interactions in this game
-- are hold bindings (open, loot, enter), so a one-frame press would fire only the tap ones and the
-- feature would look broken on exactly the crates it was built for. The hold also has to END on its
-- own -- a hand resting on a terminal must not sit on X forever -- so it stops here and does not
-- start again until the hand leaves or the game focuses something else.
local AUTO_HOLD_MAX = 1.2
-- HOW LONG A PRESS SURVIVES A GAP IN THE ARMING, and it is not a nicety: half this game's
-- interactions are HOLD bindings, and a hold that is interrupted restarts from zero.
--
-- Measured, not guessed. Holding the grip on a car door produced twenty separate X windows in one
-- session -- 16, 31, 46, 47, 62, 63, 78 ms among them -- because the surface test flickers as the
-- hand moves against the panel while the grip is held down throughout. Every one of those gaps
-- cancelled the door's hold, which is why it opened only when a rotation happened to produce a
-- longer window by luck. The press now rides over gaps shorter than this, as long as the grip is
-- still held and it is still the same object: a hand that has not let go has not stopped asking.
local PRESS_GRACE = 0.35
-- Log a line per state change with the port-wide DEBUG box ticked.
local DEBUG_SLOT   = 156

-- ---- shared slots (see include/Utils/SharedSlots.hpp) -------------------------------------------
local SLOT_GRIP_R      = 49
local SLOT_GRIP_L      = 155
local SLOT_IN_VEHICLE  = 31
local SLOT_WHEEL_MASK  = 163    -- a hand at the steering wheel is not a hand on a crate
local SLOT_WEAPON      = 144    -- weapon in hand: that hand's grip is spoken for, so a pickup with
                                -- BOTH hands is only offered when no weapon is drawn (see bothHands)
local BIT_RIGHT, BIT_LEFT = 1, 2

local function slot(i)
    if type(GetVRSharedSlot) ~= 'function' then return 0.0 end
    local ok, v = pcall(GetVRSharedSlot, i)
    return (ok and v) or 0.0
end

local dbgCache, dbgAt = false, -1.0
local function vrDebug()
    local now = (os and os.clock and os.clock()) or 0.0
    if now - dbgAt > 0.25 then
        dbgAt = now
        dbgCache = slot(DEBUG_SLOT) > 0.5
    end
    return dbgCache
end

-- WHY THIS LOGS WITHOUT THE DEBUG BOX. The first field test of the gesture reported "nothing
-- happened", and the module had nothing to say about it: its only logging sat behind the port-wide
-- DEBUG slot, which was off, so the log file was zero bytes after six sessions. Four separate gates
-- can each swallow the press silently -- no choice hub, no look-at object, no palm pose, palm too
-- far -- and from outside they all look identical. This line fires on CHANGE only (never per frame)
-- and names the gate that stopped it, so one reach in-game is enough to tell them apart.
-- Change-gated AND time-gated: the reach line carries a live distance, which changes every frame a
-- hand is moving, so dedupe alone would still write sixty lines a second.
local lastNote, lastNoteAt = nil, -1.0
local onItGripR, onItGripL = false, false
local function note(line)
    if line == lastNote then return end
    local now = (os and os.clock and os.clock()) or 0.0
    if now - lastNoteAt < 0.4 then return end
    lastNote, lastNoteAt = line, now
    spdlog.info('[VRInteract] ' .. line)
end

-- ---- is the game offering an interaction, and what for? -----------------------------------------
--
-- Two questions with two different answers, and both have to say yes.
--
--   * the CHOICE HUB says an interaction exists at all. Without this test a look-at object is only
--     "something the player is facing", and every wall would arm the gesture.
--   * the LOOK-AT OBJECT says which entity it is, and gives the world position to measure against.
--     The hub carries the choices, not the thing they belong to.
--
-- The blackboard field is probed once rather than hardcoded: the name has moved between game
-- versions, and a wrong constant here would fail silently as "no interaction, ever".
local hubField, hubProbed = nil, false
local hubCaptionLast, hubCaptionLogged = nil, 0

local function interactionOffered()
    local defs = Game.GetAllBlackboardDefs()
    if not defs or not defs.UIInteractions then return false end
    local bb = Game.GetBlackboardSystem():Get(defs.UIInteractions)
    if not bb then return false end

    if not hubProbed then
        hubProbed = true
        for _, name in ipairs({ 'InteractionChoiceHub', 'InteractionChoiceHubs', 'ChoiceHub' }) do
            local okf, field = pcall(function() return defs.UIInteractions[name] end)
            if okf and field ~= nil then hubField = name break end
        end
        if vrDebug() then
            spdlog.info(string.format('[VRInteract] choice-hub blackboard field: %s',
                                      hubField or 'NOT FOUND -- module idle'))
        end
    end
    if not hubField then return false end

    local ok, hub = pcall(function()
        return FromVariant(bb:GetVariant(defs.UIInteractions[hubField]))
    end)
    if not ok or not hub then return false end
    -- A hub with no choices is a hub that has been cleared but not yet replaced; the prompt is gone
    -- from the screen and X does nothing, so neither should the hand.
    local n = 0
    pcall(function() n = #hub.choices end)

    -- THE CAPTION, LOGGED AND NOT ACTED ON. The class is too blunt to decide with: a braindance and
    -- an elevator button are both GenericDevice, so refusing the automatic touch by class would take
    -- the feature away from every panel in the game to protect one scene. What actually separates
    -- them is what the prompt SAYS. Naming that string costs one session; guessing it costs a
    -- regression nobody would connect back to this file.
    if n > 0 and hubCaptionLogged < 12 then
        local cap = nil
        pcall(function() cap = tostring(hub.title) end)
        if cap and cap ~= hubCaptionLast then
            hubCaptionLast = cap
            hubCaptionLogged = hubCaptionLogged + 1
            spdlog.info('[VRInteract] prompt caption: ' .. cap)
        end
    end
    return n > 0
end

local function lookAtObject(player)
    local ok, obj = pcall(function()
        return Game.GetTargetingSystem():GetLookAtObject(player, false, false)
    end)
    if not ok or not obj then return nil end
    return obj
end

-- ---- the line of sight ---------------------------------------------------------------------------
--
-- The FPP camera component, not the render view: the port's VRViewWorldPos sits about a metre below
-- the real camera (see the note in the HandCollision mod, which measured it), and a ray from a metre
-- too low points at the floor in front of the panel instead of at the panel.
-- ---- WHICH HAND POINTS -----------------------------------------------------------------------
--
-- 'right' | 'left' | 'auto'. Auto is the right hand until a weapon is drawn, and the left after that:
-- the hand holding a gun is not free to point at a door, and swapping is what a person does anyway.
local RAY_HAND = 'auto'
-- 1 = the ray leaves the PALM, along the hand's own forward. 0 = the old behaviour, a ray along the
-- line of sight. This decides where the SURFACE the hand is judged against comes from; what the game
-- offers is still decided by the game (see the header), so pointing somewhere the head is not looking
-- arms nothing -- that half is the engine's targeting query and is a separate piece of work.
local RAY_FROM_HAND = 1

local function weaponOut()
    local ok, w = pcall(function() return Game.GetPlayer():GetActiveWeapon() end)
    return ok and w ~= nil
end

-- WHICH HAND POINTS -- READ, NOT RECOMPUTED. Slot 169 carries the plugin's answer, the same one the
-- engine's look-at ray is aimed with. It used to be worked out here from GetActiveWeapon() while the
-- plugin used the port's own weapon flag, and those two do not always agree: for as long as they
-- disagree the ray leaves one hand and the reach is measured against the other, which is precisely
-- "the ray moved to the left hand but the grip does nothing". Two decisions that must always match
-- must not be made twice.
--
-- RAY_HAND still forces a side for testing; 'auto' -- the shipping value -- defers to the plugin,
-- and falls back to the old local reasoning only if the slot is not being published at all.
local SLOT_POINTING_HAND = 169

local function pointingHand()
    if RAY_HAND == 'left' then return false end
    if RAY_HAND == 'right' then return true end
    if type(GetVRSharedSlot) == 'function' then
        local ok, v = pcall(GetVRSharedSlot, SLOT_POINTING_HAND)
        -- NIL IS AN ANSWER, not a failure: -1 means no hand is free at all -- driving, or both hands
        -- on the weapon. Every caller has to handle it, which is why it is nil and not `false`: a
        -- false would quietly mean "the left hand", and the left hand is exactly the one on the
        -- foregrip in the case this is here to cover.
        if ok and v ~= nil then
            if v > 1.5 then return 'both' end   -- the feature is off: either hand, as it always was
            if v < -0.5 then return nil end     -- nobody: driving, or both hands on the weapon
            return v > 0.5
        end
    end
    return not weaponOut()
end

-- WHERE THE HAND POINTS. Palm position plus the palm's own forward, both in world space, published by
-- the plugin. Returns nil when VRIK has not solved, and the caller then falls back to the line of
-- sight rather than losing the surface test altogether.
local function handRay()
    if RAY_FROM_HAND == 0 then return nil end
    if type(VRPalmWorldPos) ~= 'function' or type(VRPalmWorldRot) ~= 'function' then return nil end
    local right = pointingHand()
    if right == nil then return nil end
    if right == 'both' then right = not weaponOut() end
    local okp, p = pcall(VRPalmWorldPos, right and 1 or 0)
    if not okp or not p or (p.w or 0) < 0.5 then return nil end
    local okq, q = pcall(VRPalmWorldRot, right and 1 or 0)
    if not okq or not q then return nil end
    if q.i == 0.0 and q.j == 0.0 and q.k == 0.0 and q.r == 0.0 then return nil end
    local quat = GetSingleton('Quaternion')
    if not quat then return nil end
    local okf, fwd = pcall(function() return quat:GetForward(q) end)
    if not okf or not fwd then return nil end
    return p, fwd
end

local function cameraRay(player)
    local okc, cam = pcall(function() return player:GetFPPCameraComponent() end)
    if not okc or not cam then return nil end
    local okl, l2w = pcall(function() return cam:GetLocalToWorld() end)
    if not okl or not l2w then return nil end
    local mat, quat = GetSingleton('Matrix'), GetSingleton('Quaternion')
    if not mat or not quat then return nil end
    local okp, pos = pcall(function() return mat:GetTranslation(l2w) end)
    local okq, q   = pcall(function() return mat:ToQuat(l2w) end)
    if not okp or not okq or not pos or not q then return nil end
    local okf, fwd = pcall(function() return quat:GetForward(q) end)
    if not okf or not fwd then return nil end
    return pos, fwd
end

-- WHERE THE LINE OF SIGHT LANDS. One cast, from just ahead of the camera to HIT_MAX_DIST, and the
-- hit position is the point the hand is judged against.
--
-- THE COLLISION GROUP IS PROBED, NOT ASSUMED. A door panel is not the same kind of body as a wall,
-- the names have moved between game versions, and a wrong constant here fails the way the whole
-- feature already failed once: silently, and indistinguishably from "the hand was not close enough".
-- So the groups are tried in order on the first cast that hits anything, and the winner is
-- remembered and named in the log.
local RAY_GROUPS = { 'Static', 'World', 'Device', 'Interaction', 'PlayerBlocker', 'Dynamic' }
local rayGroup, rayGroupLogged = nil, false

local function castHit(o, f, startAt)
    if not o or not f then return nil end
    startAt = startAt or RAY_START
    local sys = nil
    if not pcall(function() sys = Game.GetSpatialQueriesSystem() end) or not sys then return nil end

    local from = Vector4.new(o.x + f.x * startAt, o.y + f.y * startAt, o.z + f.z * startAt, 1.0)
    local to   = Vector4.new(o.x + f.x * HIT_MAX_DIST, o.y + f.y * HIT_MAX_DIST, o.z + f.z * HIT_MAX_DIST, 1.0)

    local function tryGroup(g)
        -- The call has been spelled two ways across CET versions: some return (didHit, result), some
        -- return the result alone. Both are accepted rather than picking one and hoping.
        --
        -- AND THE BOOLEAN'S VALUE IS READ, not just its type. The first version tested `type(a) ==
        -- 'boolean'` to decide WHICH return held the result and then used that result whatever the flag
        -- said -- so a MISS (didHit = false, result empty) was taken for a hit. The empty result still
        -- carries a position, far enough out that the distance to it came back as `inf`, which is
        -- exactly what the field log showed on every line: a surface reading of `inf`, no "no hit"
        -- marker, and an elevator panel that could never be reached because the only test that can
        -- reach one was answering with garbage.
        local ok, a, b = pcall(function()
            return sys:SyncRaycastByCollisionGroup(from, to, g, false, false)
        end)
        if not ok then return nil end
        local didHit, res
        if type(a) == 'boolean' then didHit, res = a, b else didHit, res = true, a end
        if not didHit or not res then return nil end
        local pos = nil
        pcall(function() pos = res.position end)
        if not pos then return nil end
        if pos.x == 0.0 and pos.y == 0.0 and pos.z == 0.0 then return nil end
        -- A HIT HAS TO BE ON THE RAY. Whatever a given CET build puts in an unfilled result, it is not
        -- a point between the two ends of the cast: NaN fails every comparison it is in, and a sentinel
        -- coordinate lands astronomically far away. One length check rejects both, and it costs nothing
        -- on a real hit.
        local dx, dy, dz = pos.x - o.x, pos.y - o.y, pos.z - o.z
        local d2 = dx * dx + dy * dy + dz * dz
        if not (d2 >= 0.0) then return nil end                       -- NaN
        if d2 > (HIT_MAX_DIST + 1.0) * (HIT_MAX_DIST + 1.0) then return nil end
        -- Backed off along the ray, towards the hand. See SURFACE_BACKOFF. Clamped so a hit closer
        -- than the back-off cannot land BEHIND the ray origin, which would put the contact point
        -- inside the player.
        local back = SURFACE_BACKOFF
        local dist = math.sqrt(d2)
        if back > dist then back = dist end
        return Vector4.new(pos.x - f.x * back, pos.y - f.y * back, pos.z - f.z * back, 1.0)
    end

    -- THE REMEMBERED GROUP IS A HINT, NOT AN ANSWER, and treating it as an answer was a regression
    -- with a hidden trigger: the first cast that hits anything locks its group in for the session, so
    -- looking at a wall before ever looking at a panel locked 'Static' -- and an elevator's button
    -- panel is not a Static body, so from then on the surface test was dead and only the origin test
    -- was left, which for a panel sits metres away. It worked or did not work depending on what the
    -- player happened to look at first, which is exactly the kind of failure that comes back.
    --
    -- So the hint is tried first, and the rest are tried when it misses. A hit costs one call either
    -- way; only a genuine miss pays for the sweep.
    if rayGroup then
        local pos = tryGroup(rayGroup)
        if pos then return pos end
    end
    for _, g in ipairs(RAY_GROUPS) do
        if g == rayGroup then goto continue end
        local pos = tryGroup(g)
        if pos then
            rayGroup = g
            if not rayGroupLogged then
                rayGroupLogged = true
                spdlog.info('[VRInteract] line-of-sight cast works on collision group: ' .. g)
            end
            return pos
        end
        ::continue::
    end
    if not rayGroupLogged then
        rayGroupLogged = true
        spdlog.info('[VRInteract] NO collision group answered the cast -- origin test only')
    end
    return nil
end

-- WHERE ONE NAMED HAND'S OWN RAY LANDS. The look-at surface (`hit`) is the POINTING hand's landing
-- point -- the engine has one look-at ray and it leaves whichever hand currently holds it. That is
-- the whole reason touching a door with the OTHER hand did nothing: the other hand was being measured
-- against a point on the door that the FIRST hand was aimed at, metres from where the second hand
-- actually landed. So for the both-hands device case each hand gets its OWN surface: a cast from that
-- palm, along that palm's forward, exactly like the one the pointing hand feeds the engine. Reaching
-- for a door IS a pointing gesture, so this cast finds the door in front of the hand; a hand laid dead
-- flat on a small panel has no forward into it, and that case is still carried by the shared `hit`.
local function palmSurface(right)
    if type(VRPalmWorldPos) ~= 'function' or type(VRPalmWorldRot) ~= 'function' then return nil end
    local okp, p = pcall(VRPalmWorldPos, right and 1 or 0)
    if not okp or not p or (p.w or 0) < 0.5 then return nil end
    local okq, q = pcall(VRPalmWorldRot, right and 1 or 0)
    if not okq or not q then return nil end
    if q.i == 0.0 and q.j == 0.0 and q.k == 0.0 and q.r == 0.0 then return nil end
    local quat = GetSingleton('Quaternion')
    if not quat then return nil end
    local okf, fwd = pcall(function() return quat:GetForward(q) end)
    if not okf or not fwd then return nil end
    return castHit(p, fwd, HAND_RAY_START)
end

-- STICKY PER-HAND SURFACE, and it is the fix for the touch depth flipping between fingertip and wrist.
-- palmSurface casts along the palm's OWN forward, and a palm laid flat on a panel points ALONG the
-- glass, not into it -- so that cast blinks out at the exact moment the hand arrives. For the one
-- frame it is gone the hand falls back on a deeper reference (the object ORIGIN, which for a terminal
-- sits behind the screen), so the depth the hand must reach jumps back and forth every frame. Holding
-- each hand's last good surface for a short while keeps the contact point still; the palm still has to
-- be within the arming radius OF it, so a hand that has withdrawn simply falls out of range and the
-- cache does no harm.
local ownSurf   = { [true] = nil,  [false] = nil }
local ownSurfAt = { [true] = -1.0, [false] = -1.0 }
local OWNHIT_TTL  = 0.30    -- seconds a cached surface outlives a cast that missed
local OWNHIT_NEAR = 0.60    -- metres: only cache a hit this close to the palm -- a real touch surface,
                            -- not a wall metres down the palm's forward

local function stableSurface(right, palm)
    local now = (os and os.clock and os.clock()) or 0.0
    local fresh = palmSurface(right)
    if fresh and palm then
        local dx, dy, dz = palm.x - fresh.x, palm.y - fresh.y, palm.z - fresh.z
        if (dx * dx + dy * dy + dz * dz) <= OWNHIT_NEAR * OWNHIT_NEAR then
            ownSurf[right], ownSurfAt[right] = fresh, now
            return fresh
        end
    end
    if ownSurf[right] and (now - ownSurfAt[right]) < OWNHIT_TTL then
        return ownSurf[right]
    end
    ownSurf[right] = nil
    return nil
end

-- ---- state --------------------------------------------------------------------------------------
local armedR, armedL = false, false      -- palm inside the radius (hysteresis applied)
local pressGraceKey, pressGraceUntil = nil, 0.0   -- see PRESS_GRACE

-- ---- HAPTICS: THE TWO THINGS THE PICTURE CANNOT SAY ---------------------------------------------
--
-- The prompt is drawn on the OBJECT, never on the hand -- so with both hands live there is nothing
-- on screen that says which one would act. And a touch panel has no moment of confirmation at all:
-- the hand lands, the press goes out, and the terminal answers a beat later if it answers visibly.
--
-- Two taps, deliberately different so they cannot be confused for each other:
--   ARMED  -- short and light, on the hand that just took the object. Fires on the EDGE, never while
--             the hand rests there, or holding a hand on a crate would buzz for as long as you did.
--   FIRED  -- longer and firmer, on the same hand, when the press actually goes out for a device.
--             This is the one that says the panel took it.
local HAPTIC = true
local HAPTIC_ARM_AMP,  HAPTIC_ARM_MS  = 0.35, 15
local HAPTIC_FIRE_AMP, HAPTIC_FIRE_MS = 0.75, 45

local function haptic(rightHand, amp, ms)
    if not HAPTIC then return end
    if type(SetVRHaptic) ~= 'function' then return end
    pcall(SetVRHaptic, rightHand and 1 or 0, amp, ms)
end

local hapticArmedR, hapticArmedL = false, false
local hapticFiredKey = nil
-- An auto-press latches per target: a hand parked on an elevator button presses it ONCE. The latch
-- clears when the hand leaves or the game focuses something else -- so pressing it again is a
-- deliberate reach, not a matter of holding still for another second.
local autoLatch, autoLatchKey, autoStart = false, nil, 0.0
local autoLatchLogged = 0
local deviceTraceCount = 0
local rawHitLogged = 0
local surfaceRejectLogged = 0
local lastLog = nil

local function palmWorld(right)
    if type(VRPalmWorldPos) ~= 'function' then return nil end
    local ok, p = pcall(VRPalmWorldPos, right and 1 or 0)
    if not ok or not p or (p.w or 0) < 0.5 then return nil end
    return p
end

-- `lim` overrides the arming radius: the automatic touch passes RADIUS_TOUCH, everything else gets
-- the generous pair. The hysteresis is kept in both cases -- a hand held exactly at the edge would
-- otherwise chatter the press on and off every frame -- and for the tight radius the release
-- distance grows by the same proportion the loose one does.
local function near(p, t, armed, lim)
    if not p then return false end
    local dx, dy, dz = p.x - t.x, p.y - t.y, p.z - t.z
    local d = math.sqrt(dx * dx + dy * dy + dz * dz)
    local rin  = lim or RADIUS_IN
    local rout = lim and (lim * (RADIUS_OUT / RADIUS_IN)) or RADIUS_OUT
    return d <= (armed and rout or rin), d
end

local function publish(mask, press)
    if type(SetVRInteract) ~= 'function' then return end
    pcall(SetVRInteract, mask, press and 1 or 0)
end

-- THE AUTO-PRESS LATCH IS NOT CLEARED HERE, and that is the whole point of this note.
--
-- clear() is the generic stand-down: it runs when the hand leaves, but ALSO when the player gets
-- into a vehicle, when VRIK has no pose, and -- the one that mattered -- when the game stops
-- offering an interaction. Resetting the latch here meant that anything which merely took the
-- prompt away re-armed the automatic press, with the hand still resting exactly where it was.
--
-- A braindance is precisely that shape, and it looped: a palm starts it, the prompt disappears
-- while it plays, the latch is wiped, the prompt comes back on exit with the hand still on the
-- device, and the press fires itself. Enter, exit, enter -- with no way to stop it, because the
-- gesture that would stop it is the one being retriggered.
--
-- So the latch is released in ONE place only: the branch where the mask is zero, which is the
-- only condition that actually means the hand is not on anything. That is what the comment there
-- has always said it was for.
local function clear()
    armedR, armedL = false, false
    hapticArmedR, hapticArmedL = false, false
    hapticFiredKey = nil
    publish(0, false)
end

-- GRIP EDGES, logged whatever else the frame decides. The ON IT line already prints the grip, but
-- note() is rate limited to one line per 0.4 s and drops repeats -- so a squeeze that lasts 200 ms
-- can be entirely invisible in the log while the player is certain they squeezed. A field report of
-- "the first grip does nothing, the second works" cannot be read at all without this: it is the one
-- fact that separates "the grip was never seen" from "it was seen and the press went nowhere".
local dbgGripR, dbgGripL = false, false
-- BOUNDED, for the reason the DEBUG gate exists at all: the grip is the busiest button in this port
-- -- holsters, the magazine, the ball, the wheel -- so an unbounded edge log is a megabyte of noise
-- by the end of an evening. Forty edges is the first minute or so, which is where the question this
-- was added for lives, and after that it goes quiet on its own.
local dbgGripEdges = 0

registerForEvent('onUpdate', function()
    if not ENABLED then return end

    local player = Game.GetPlayer()
    if not player then clear() return end

    do
        local gr = slot(SLOT_GRIP_R) > 0.5
        local gl = slot(SLOT_GRIP_L) > 0.5
        if (gr ~= dbgGripR or gl ~= dbgGripL) and dbgGripEdges < 40 then
            dbgGripR, dbgGripL = gr, gl
            dbgGripEdges = dbgGripEdges + 1
            spdlog.info(string.format('[VRInteract] grip edge: R=%s L=%s  armed R=%s L=%s',
                                      tostring(gr), tostring(gl),
                                      tostring(armedR), tostring(armedL)))
        end
    end
    -- Mounted, X belongs to the port's own exit gesture and the merge holds it out entirely. A hand
    -- waving near the dashboard must not press anything.
    if slot(SLOT_IN_VEHICLE) > 0.5 then note('idle: in a vehicle') clear() return end

    -- THE LOOK-AT OBJECT IS OPTIONAL NOW, and the log is why. It came back empty 47 times in one short
    -- session while the game was plainly offering an interaction -- the targeting system does not
    -- always name an entity for a prompt it is showing. Requiring it meant the gesture went dead on
    -- exactly those prompts, silently. What the entity buys is two things and only two: the origin
    -- test, and knowing whether the thing is picked UP (grip) or pressed (a hand is enough). Without
    -- it the line-of-sight test still knows where the hand is pointing, so the reach still works --
    -- it just asks for the grip, which is the deliberate half of the gesture and never the wrong
    -- thing to ask for when the port cannot tell what it is reaching at.
    local target = lookAtObject(player)

    -- THE ONE BIT THE HAND ALTERNATION RUNS ON. The plugin casts the look-at ray from one hand and
    -- needs to know whether it landed on anything; this is that answer, from the API that cannot be
    -- misread. Published every frame, including when nothing was found -- a producer that only spoke
    -- up on success would look exactly like a producer that had died.
    -- TAGGED WITH THE HAND IT IS ABOUT. The ray alternates, and this answer cannot come back in the
    -- frame the ray was cast -- so an untagged bit describes whichever hand happened to be up when it
    -- arrived, which is usually the wrong one. Slot 169 says which hand the plugin is currently
    -- casting from; sending it back with the answer is what lets the plugin ignore a reply meant for
    -- the other hand instead of locking on it.
    if type(SetVRLookAtAcquired) == 'function' then
        local h = -1
        if type(GetVRSharedSlot) == 'function' then
            local okh, hv = pcall(GetVRSharedSlot, SLOT_POINTING_HAND)
            if okh and hv ~= nil then h = math.floor(hv + 0.5) end
        end
        pcall(SetVRLookAtAcquired, target and 1 or 0, h)
    end

    local tp = nil
    if target then
        local okp, p = pcall(function() return target:GetWorldPosition() end)
        if okp then tp = p end
    end

    -- ---- IS THE GAME OFFERING ANYTHING? ---------------------------------------------------------
    --
    -- THIS GATE USED TO BE THE CHOICE HUB ALONE, AND THAT COST THE FIRST OBJECT OF EVERY SESSION.
    -- Measured, not guessed: for 54 seconds after load the UI_Interaction blackboard answered "no
    -- interaction" while the prompt was plainly on screen, and the whole module sat behind it. The
    -- player squeezed the grip sixteen times on a shard -- every one of those edges is in the log,
    -- every one with `armed R=false`, because nothing downstream of the gate ever ran. The first
    -- terminal of the session wrote hubId=12 into that blackboard and from then on everything
    -- worked, which is exactly the "it starts working after one interaction" the field report
    -- described. The same log also shows the value going STALE the other way: hubId=13 still
    -- reported a hub for a vending machine 54 metres away.
    --
    -- So the hub is a poor gate in both directions -- empty when it should be full, full when it
    -- should be empty -- and it is kept only as a fallback for prompts that never reach the
    -- candidate list.
    --
    -- THE PRIMARY GATE IS THE ENGINE'S OWN QUESTION, asked about THIS object:
    --
    --     InteractionManager.IsInteractionLookAtTarget(player, target)
    --
    -- Reverse engineering says what it really answers (see src/Hooks/LookAtRayFromHand.cpp for the
    -- trail): the object is in the activator's interaction candidate list. It is per-object, and it
    -- is live from the first frame -- the probe had it answering correctly eleven seconds before a
    -- choice hub existed at all.
    local offered = false
    if target then
        local mgr = nil
        local okm = pcall(function() mgr = Game.GetInteractionManager() end)
        if okm and mgr then
            local okv, v = pcall(function() return mgr:IsInteractionLookAtTarget(player, target) end)
            if okv and v == true then offered = true end
        end
    end
    if not offered then offered = interactionOffered() end
    if not offered then note('idle: the game is offering no interaction') clear() return end

    -- A hand holding the steering wheel is spoken for; so is the grip that holds it.
    local wheel = math.floor(slot(SLOT_WHEEL_MASK) + 0.5)
    local wheelR = (wheel % 2) == 1
    local wheelL = (math.floor(wheel / 2) % 2) == 1
    local pR, pL = palmWorld(true), palmWorld(false)
    -- THE HAND POINTS FIRST, the head only when the hand has nothing to say. Which one it is decides
    -- where the cast begins: see HAND_RAY_START.
    local rayStart = HAND_RAY_START
    local camO, camF = handRay()
    if not camO then
        camO, camF = cameraRay(player)
        rayStart = RAY_START
    end

    -- TWO POINTS TO BE NEAR, and either is enough, both judged by the SAME radius. The entity origin
    -- is the exact one whenever the origin is the thing (a shard, a bottle, a can). The surface the
    -- line of sight lands on carries everything the origin cannot reach -- panels, doors, terminals.
    -- Neither is a direction test: the hand has to be at a place, which is what stops a raised hand
    -- from opening a door across the room.
    -- ---- WHERE THE SURFACE IS ------------------------------------------------------------------
    --
    -- ASK THE ENGINE, DO NOT CAST AGAIN. GetLookAtPosition returns the point where the look-at ray
    -- LANDED -- the same ray, from the same hand, that chose the object in the first place. Using it
    -- makes the contact point consistent with the selection by construction, instead of by two casts
    -- agreeing.
    --
    -- IT ALSO REMOVES A WHOLE CLASS OF FAILURE. Our own cast ran along the palm's FORWARD, and laying
    -- a hand flat on a terminal is not a pointing gesture: the fingers end up parallel to the screen,
    -- so the ray skimmed past the glass and never hit it. The action then only fired when the hand was
    -- ROTATED far enough for the forward to dive into the panel -- reported for a car door and again
    -- for terminals, and the rotation was the tell both times.
    --
    -- The cast stays as the fallback, for the frames where the engine names no landing point.
    local hit = nil
    do
        local okp, lp = pcall(function()
            return Game.GetTargetingSystem():GetLookAtPosition(player, false, false)
        end)
        if okp and lp and not (lp.x == 0.0 and lp.y == 0.0 and lp.z == 0.0) then hit = lp end
    end
    local hitFromEngine = hit ~= nil
    if not hit then hit = castHit(camO, camF, rayStart) end

    -- THE LANDING POINT HAS TO BE ON THE OBJECT THE GAME IS OFFERING, and until now nothing checked
    -- that. Measured during a braindance loop with both controllers lying on a desk:
    --
    --     GenericDevice  L origin=0.80  surface=0.11  armedL=true  press=true  (no grip)
    --
    -- The focused object was 80 cm away. The "surface" was 11 cm away -- because a resting hand
    -- points at the desk, the ray lands there, and that point passed the proximity test. So the hand
    -- was judged to be ON a device it was nowhere near, and the automatic path pressed it. The shard
    -- reopened, the scene replayed, and it looped with nobody touching anything.
    --
    -- A distance to SOME surface is not evidence of contact with THAT object. The landing point is
    -- only usable when it lies on the object in hand, and the honest fallback when we cannot tell is
    -- the object's own origin -- less precise, never wrong about which thing it belongs to.
    if hit and tp then
        local sx, sy, sz = hit.x - tp.x, hit.y - tp.y, hit.z - tp.z
        local sd = math.sqrt(sx * sx + sy * sy + sz * sz)
        -- Generous: an origin can sit metres from the surface you touch (a door's hinge, a lift's
        -- car), which is the whole reason the surface test exists. This only has to reject a point
        -- that belongs to a different piece of the world entirely.
        if sd > SURFACE_OWNERSHIP then
            if surfaceRejectLogged < 20 then
                surfaceRejectLogged = surfaceRejectLogged + 1
                spdlog.info(string.format(
                    '[VRInteract] landing point discarded: %.2f m from the focused object (limit %.2f)',
                    sd, SURFACE_OWNERSHIP))
            end
            hit = nil
        end
    end

    -- THE RAW LANDING POINT, before anything of mine touches it. The surface distance has been
    -- printing 0.00 on every frame, which means the point is arriving within the 6 cm back-off of
    -- the palm and the back-off is then collapsing it onto the palm exactly -- so the proximity test
    -- has been passing unconditionally. Whether that is because GetLookAtPosition hands back the ray
    -- ORIGIN (which is the palm, since the ray was moved there) or because the ray hits the hand's
    -- own collision changes the fix completely, and only the raw number separates them.
    if hit and rawHitLogged < 30 then
        local ph0 = pR or pL
        if ph0 then
            local rdx, rdy, rdz = ph0.x - hit.x, ph0.y - hit.y, ph0.z - hit.z
            local rawd = math.sqrt(rdx * rdx + rdy * rdy + rdz * rdz)
            rawHitLogged = rawHitLogged + 1
            spdlog.info(string.format(
                '[VRInteract] raw landing point: %.3f m from the palm (source: %s)',
                rawd, hitFromEngine and 'GetLookAtPosition' or 'our cast'))
        end
    end

    -- PULLED TOWARDS THE HAND, not along a ray. Six centimetres is roughly palm centre to fingertips,
    -- so the hand arrives when it LOOKS like it has arrived instead of when it has sunk into the
    -- geometry. Backing off towards the PALM rather than down the ray keeps this working for a flat
    -- hand, which is the case that has no meaningful ray direction at all.
    if hit then
        local ph = pR or pL
        if ph then
            local dx, dy, dz = ph.x - hit.x, ph.y - hit.y, ph.z - hit.z
            local d = math.sqrt(dx * dx + dy * dy + dz * dz)
            local back = SURFACE_BACKOFF
            if back > d then back = d end
            if d > 0.0001 then
                hit = Vector4.new(hit.x + dx / d * back, hit.y + dy / d * back, hit.z + dz / d * back, 1.0)
            end
        end
    end
    -- CLASSIFY THE TARGET ONCE, BEFORE ARMING -- three later decisions read it and they must agree:
    --   * touchOnly  -- the tight 15 cm radius. Only the objects the RTTI names a device OUTRIGHT
    --                   (panels, terminals) get it; a door does NOT, because its origin is a hinge
    --                   metres from the handle and 15 cm there is unreachable.
    --   * isDevice   -- pressed by a touch rather than picked up with the grip. Broader than touchOnly:
    --                   it also trusts the class NAME (Door, Elevator, Terminal), which is how doors
    --                   and lifts -- whose base class the RTTI does not answer for -- qualify.
    --   * both hands -- a touch does not care which hand cast the ray, so EITHER hand that lands on a
    --                   device presses it. Gated on isDevice, which is why it now covers doors too.
    --                   (The earlier version gated it on touchOnly, so touching a DOOR with the hand
    --                   that had not cast the ray did nothing -- a door is not touchOnly.)
    local cls = '?'
    if target then pcall(function() cls = tostring(target:GetClassName()) end) end
    local touchOnly, isDevice = false, false
    if target then
        for _, n in ipairs({ 'gameDeviceBase', 'Device', 'gameinteractionsvisDevice' }) do
            local okd, yes = pcall(function() return target:IsA(n) end)
            if okd and yes then isDevice = true break end
        end
        if AUTO_DEVICES and isDevice then touchOnly = true end
        if not isDevice and (string.find(cls, 'Device') or string.find(cls, 'Terminal')
                             or string.find(cls, 'Door') or string.find(cls, 'Elevator')) then
            isDevice = true
        end
    end
    local isItem = not isDevice
    -- BOTH HANDS ARM FOR EITHER KIND OF INTERACTION -- the ray only chooses the target, and which
    -- hand then acts on it is decided by which hand makes CONTACT, not by which cast the ray.
    --   * DEVICE (touch) -- always both hands: a touch has no grip to conflict with a weapon.
    --   * ITEM (grip)    -- both hands too, BUT only when no weapon is drawn. A weapon puts that
    --                       hand's grip to work (holster, foregrip), so with a weapon out only the
    --                       free hand may grab -- exactly the old pointing-hand behaviour. The press
    --                       still comes from the COLLIDING hand's OWN grip (see below), so the wrong
    --                       hand can never pick something up.
    local weaponOn = slot(SLOT_WEAPON) > 0.5
    local bothHands = (AUTO_DEVICES and isDevice) or (isItem and not weaponOn)

    -- ownHit is this hand's OWN surface landing (see palmSurface), passed only for the both-hands
    -- device case. It is what lets the hand that did not cast the look-at ray still be judged against
    -- the door in front of ITSELF, rather than against the point the other hand is aimed at.
    local function onIt(palm, wheeled, wasArmed, ownHit)
        if wheeled or not palm then return false, nil, nil end
        local lim = touchOnly and RADIUS_TOUCH or nil
        local byOrigin, d = false, nil
        if tp then byOrigin, d = near(palm, tp, wasArmed, lim) end
        local byHit, dh = false, nil
        if hit then byHit, dh = near(palm, hit, wasArmed, lim) end
        if ownHit then
            local byOwn, dOwn = near(palm, ownHit, wasArmed, lim)
            if byOwn then byHit = true end
            if dOwn and (not dh or dOwn < dh) then dh = dOwn end
        end
        return (byOrigin or byHit), d, dh
    end

    -- THE RAY ONLY CHOOSES THE TARGET; THE HAND THAT ACTS IS THE ONE THAT MAKES CONTACT. The look-at
    -- ray leaves one hand and can flicker or lock to it, but it no longer decides who may act -- both
    -- hands are armed against the object (each at its own surface, see palmSurface), so the hand that
    -- did NOT cast the ray still works.
    --   * DEVICE -- the colliding hand's touch presses it, and the haptic goes to that hand.
    --   * ITEM   -- the colliding hand's OWN grip picks it up (below), and the haptic goes to that
    --              hand. With a weapon out the free hand alone arms (bothHands is false), so a grip
    --              held for the weapon can never answer -- which is the ambiguity the old
    --              pointing-hand-only rule existed to avoid.
    local pointRight = pointingHand()
    if pointRight == nil then
        note('idle: no hand is free to point (driving, or both hands on the weapon)')
        clear()
        return
    end
    local nearR, dR, hitR = false, nil, nil
    local nearL, dL, hitL = false, nil, nil
    if bothHands or pointRight == 'both' then
        -- Each hand against the door in front of ITSELF, not the point the other hand is aimed at.
        -- stableSurface holds each hand's own surface steady so the touch depth does not flicker.
        local ownR = bothHands and stableSurface(true, pR) or nil
        local ownL = bothHands and stableSurface(false, pL) or nil
        nearR, dR, hitR = onIt(pR, wheelR, armedR, ownR)
        nearL, dL, hitL = onIt(pL, wheelL, armedL, ownL)
    elseif pointRight then
        nearR, dR, hitR = onIt(pR, wheelR, armedR)
    else
        nearL, dL, hitL = onIt(pL, wheelL, armedL)
    end
    armedR, armedL = nearR, nearL

    -- ON THE EDGE ONLY. A hand that stays on the object has already been told.
    if armedR and not hapticArmedR then haptic(true,  HAPTIC_ARM_AMP, HAPTIC_ARM_MS) end
    if armedL and not hapticArmedL then haptic(false, HAPTIC_ARM_AMP, HAPTIC_ARM_MS) end
    hapticArmedR, hapticArmedL = armedR, armedL

    local mask = (armedR and BIT_RIGHT or 0) + (armedL and BIT_LEFT or 0)
    if mask == 0 then
        -- The two ways to be here are worlds apart and used to look the same from the log: a palm
        -- pose that never arrived (VRIK not solving, or the avatar suspended) reads as "no hand at
        -- all", while a real distance means the reach was simply short. Both are named.
        if not pR and not pL then
            note('BLOCKED: no palm pose from VRIK -- VRPalmWorldPos returned nothing for either hand')
        else
            -- BOTH TESTS, side by side, plus the palm-to-player distance. That last number is the
            -- frame check and it has already earned its place once: it read 1.05-1.47 m against a
            -- player whose origin is at the feet, which is exactly where a hand is, and that is what
            -- ruled out a bad palm frame and pointed at the object origins instead.
            local pp = nil
            pcall(function() pp = player:GetWorldPosition() end)
            local ppd = (pp and pR) and math.sqrt((pR.x - pp.x)^2 + (pR.y - pp.y)^2 + (pR.z - pp.z)^2) or nil
            note(string.format(
                'reaching: R origin %s  surface %s | L origin %s  surface %s | arms at %.2f m '
                .. '| palm-to-player %s%s%s',
                dR and string.format('%.2f', dR) or '--',
                hitR and string.format('%.2f', hitR) or '--',
                dL and string.format('%.2f', dL) or '--',
                hitL and string.format('%.2f', hitL) or '--',
                RADIUS_IN,
                ppd and string.format('%.2f m', ppd) or 'n/a',
                camO and '' or '  [NO CAMERA RAY]',
                hit and '' or '  [NO SURFACE HIT within ' .. HIT_MAX_DIST .. ' m]'))
        end
        -- The hand left. Whatever the automatic press was doing, it is over, and the next reach
        -- starts a fresh one.
        autoLatch, autoLatchKey, autoStart = false, nil, 0.0
        publish(0, false)
        return
    end

    -- WHICH HALF OF THE GESTURE THIS OBJECT WANTS: a hand arriving, or a hand arriving AND closing.
    --
    --   * A DEVICE is pressed. Panels, doors, elevator calls, terminals, vending machines -- in the
    --     real world your hand does the whole job by landing on them, and holding a grip to press a
    --     lift button would be pantomime.
    --   * EVERYTHING ELSE is picked up, and picking up is a grab: it waits for the grip. Shards,
    --     bottles, weapons on the floor, bodies.
    --
    -- THE TEST USED TO BE IsA('ItemObject') AND IT NEVER ONCE RETURNED TRUE. That is why loose items
    -- were being taken by a passing hand with no grip at all: everything fell through to the device
    -- branch, which presses itself. RTTI names are the game's, not the docs' -- an item is
    -- `gameItemObject` -- so the question is asked the other way round now, of the device base class,
    -- and the class name is logged so a miss can be seen instead of guessed at. A thing the port
    -- cannot identify asks for the grip: the deliberate half is never the wrong thing to ask for.
    --
    -- isDevice / isItem / cls are worked out ONCE, up where the arming radius and the both-hands rule
    -- need them (see the classify block before arming). They are the same values here.

    local press = false
    -- WHICH HAND SENDS THE ACTION, so the FIRED haptic reaches it and no other. For an item it is the
    -- hand whose grip fired; for a device it is the hand that touched. true = right, false = left.
    local pressHand = false
    if isItem or not AUTO_DEVICES then
        local gR = slot(SLOT_GRIP_R) > 0.5
        local gL = slot(SLOT_GRIP_L) > 0.5
        -- THE COLLIDING HAND'S OWN GRIP, never the other's: a hand armed on the item presses only if
        -- ITS grip is the one squeezed. This is what lets both hands be armed without either grabbing
        -- for the other.
        local firedR = armedR and gR
        local firedL = armedL and gL
        press = firedR or firedL

        -- THE GRACE. See PRESS_GRACE: the arming flickers, the grip does not, and a hold binding
        -- cannot survive the flicker. Keyed on the object so that letting go, or moving to something
        -- else, ends the press immediately -- the grace covers a gap in the measurement, never a
        -- change of intent.
        local okk, key = false, nil
        if target then okk, key = pcall(function() return tostring(target:GetEntityID().hash) end) end
        key = (okk and key) or 'unknown'
        local now = (os and os.clock and os.clock()) or 0.0
        if press then
            pressGraceKey, pressGraceUntil = key, now + PRESS_GRACE
        elseif (gR or gL) and key == pressGraceKey and now < pressGraceUntil then
            press = true
        end
        -- The acting hand: the one whose grip fired, or -- under grace -- the one still gripping.
        if firedR then pressHand = true
        elseif firedL then pressHand = false
        elseif gR then pressHand = true
        else pressHand = false end
    else
        -- Device presses on a touch: the acting hand is the one that collided. If both are on it,
        -- either serves; the right is reported.
        pressHand = armedR
        local okk, key = false, nil
        if target then okk, key = pcall(function() return tostring(target:GetEntityID().hash) end) end
        key = (okk and key) or 'unknown'
        local now = (os and os.clock and os.clock()) or 0.0
        -- THE LATCH RELEASES ON GEOMETRY, NOT ON IDENTITY. It used to reset whenever the entity id
        -- under the hand changed, on the reasoning that a different object is a different press.
        -- The braindance loop disproved that: leaving the braindance destroys the shard case and
        -- rebuilds it with a FRESH id, so the key never matched, the latch reset every cycle, and
        -- the automatic path pressed again -- with both controllers lying on a desk. The measured
        -- trace showed press=true, latch=false on all 120 frames and not one 'held back' line.
        --
        -- What actually ends a press is the hand leaving, and that is already handled where the
        -- armed mask falls to zero. Until then the latch holds no matter what the game rebuilds
        -- underneath it. The cost is that sliding from one device straight onto another without
        -- withdrawing needs the hand pulled back first; the gain is that no object can press
        -- itself by being reborn under a hand that never moved.
        autoLatchKey = key
        if autoLatch then
            if autoLatchLogged < 8 then
                autoLatchLogged = autoLatchLogged + 1
                spdlog.info('[VRInteract] auto-press held back: the hand has not left ' .. cls)
            end
        end
        if not autoLatch then
            if autoStart <= 0.0 then autoStart = now end
            if now - autoStart < AUTO_HOLD_MAX then
                press = true
            else
                autoLatch = true    -- held long enough; done until the hand leaves
            end
        end
    end

    -- FIRED, once per object rather than once per frame: the press is a LEVEL held for as long as
    -- the gesture lasts, so pulsing on it would run the motor for the whole hold.
    if press then
        local okk, fkey = false, nil
        if target then okk, fkey = pcall(function() return tostring(target:GetEntityID().hash) end) end
        fkey = (okk and fkey) or 'unknown'
        if fkey ~= hapticFiredKey then
            hapticFiredKey = fkey
            haptic(pressHand, HAPTIC_FIRE_AMP, HAPTIC_FIRE_MS)
        end
    else
        hapticFiredKey = nil
    end

    -- FRAME-BY-FRAME TRACE OF THE DEVICE PATH, no rate limit, bounded to 120 lines. Everything that
    -- decides whether a device presses itself is on one line, so the answer needs no interpreting:
    -- whether the object is seen at all, both palm distances, which radius applies, what is armed,
    -- whether the latch is holding, and what the press came out as.
    if isDevice and deviceTraceCount < 120 then
        deviceTraceCount = deviceTraceCount + 1
        spdlog.info(string.format(
            '[VRInteract] DEVICE trace %03d: %s touchOnly=%s radius=%.2f | R origin=%s surface=%s | '
            .. 'L origin=%s surface=%s | armedR=%s armedL=%s latch=%s press=%s gR=%s gL=%s',
            deviceTraceCount, cls, tostring(touchOnly), touchOnly and RADIUS_TOUCH or RADIUS_IN,
            dR and string.format('%.2f', dR) or '--', hitR and string.format('%.2f', hitR) or '--',
            dL and string.format('%.2f', dL) or '--', hitL and string.format('%.2f', hitL) or '--',
            tostring(armedR), tostring(armedL), tostring(autoLatch), tostring(press),
            tostring(slot(SLOT_GRIP_R) > 0.5), tostring(slot(SLOT_GRIP_L) > 0.5)))
    end

    publish(mask, press)

    -- ON the object. What is left to say is whether the press went out, and for an ItemObject that
    -- is entirely down to the grip -- so the grip is printed rather than inferred.
    local gripRnow, gripLnow = slot(SLOT_GRIP_R) > 0.5, slot(SLOT_GRIP_L) > 0.5
    local line = string.format('ON IT: %s -> %s  press=%s  mask=%d grip R=%s L=%s | R origin %s surface %s',
                       cls, isDevice and 'DEVICE (touch)' or 'ITEM (needs grip)', tostring(press),
                       mask,
                       tostring(gripRnow), tostring(gripLnow),
                       (dR or dL) and string.format('%.2f', dR or dL) or '--',
                       (hitR or hitL) and string.format('%.2f', hitR or hitL) or '--')
    -- A SQUEEZE MUST NEVER FALL IN THE RATE LIMIT'S HOLE. note() drops repeats and anything inside
    -- 0.4 s, which is right for a distance that changes every frame and wrong for the one instant
    -- this whole gesture turns on: a 200 ms grip can land entirely between two permitted lines, and
    -- then the log shows `grip L=false` for a squeeze that happened. That exact blind spot has
    -- already cost this project two sessions. On a CHANGE of grip the line goes out unconditionally.
    if gripRnow ~= onItGripR or gripLnow ~= onItGripL then
        onItGripR, onItGripL = gripRnow, gripLnow
        spdlog.info('[VRInteract] ' .. line)
    else
        note(line)
    end

    if vrDebug() then
        local line = string.format('%d/%d item=%s press=%s', mask, wheel, tostring(isItem), tostring(press))
        if line ~= lastLog then
            lastLog = line
            spdlog.info('[VRInteract] ' .. line)
        end
    end
end)

registerForEvent('onShutdown', function()
    -- Leaving the mask up would keep a grip claimed by a mod that is no longer running. The press
    -- expires on its own (the plugin holds a deadline, not a flag), but the mask does not.
    publish(0, false)
end)
