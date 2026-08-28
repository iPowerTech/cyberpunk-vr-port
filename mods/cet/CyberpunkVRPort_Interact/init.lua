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
-- The engine finds interactables with a raycast from the CAMERA, and that is left exactly as it
-- was. This module never invents an interaction, never reaches something the game is not already
-- offering, and never fires on an object other than the focused one. Look away and the mask drops
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
local RAY_START    = 0.12   -- start the cast just ahead of the camera so it cannot hit the player
-- Devices press themselves. Off = everything, devices included, waits for a grip.
local AUTO_DEVICES = true
-- How long an automatic press is HELD, seconds. Not a nicety: most device interactions in this game
-- are hold bindings (open, loot, enter), so a one-frame press would fire only the tap ones and the
-- feature would look broken on exactly the crates it was built for. The hold also has to END on its
-- own -- a hand resting on a terminal must not sit on X forever -- so it stops here and does not
-- start again until the hand leaves or the game focuses something else.
local AUTO_HOLD_MAX = 1.2
-- Log a line per state change with the port-wide DEBUG box ticked.
local DEBUG_SLOT   = 156

-- ---- shared slots (see include/Utils/SharedSlots.hpp) -------------------------------------------
local SLOT_GRIP_R      = 49
local SLOT_GRIP_L      = 155
local SLOT_IN_VEHICLE  = 31
local SLOT_WHEEL_MASK  = 163    -- a hand at the steering wheel is not a hand on a crate
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

local function pointingHand()
    if RAY_HAND == 'left' then return false end
    if RAY_HAND == 'right' then return true end
    return not weaponOut()          -- auto: the free hand
end

-- WHERE THE HAND POINTS. Palm position plus the palm's own forward, both in world space, published by
-- the plugin. Returns nil when VRIK has not solved, and the caller then falls back to the line of
-- sight rather than losing the surface test altogether.
local function handRay()
    if RAY_FROM_HAND == 0 then return nil end
    if type(VRPalmWorldPos) ~= 'function' or type(VRPalmWorldRot) ~= 'function' then return nil end
    local right = pointingHand()
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

local function castHit(o, f)
    if not o or not f then return nil end
    local sys = nil
    if not pcall(function() sys = Game.GetSpatialQueriesSystem() end) or not sys then return nil end

    local from = Vector4.new(o.x + f.x * RAY_START, o.y + f.y * RAY_START, o.z + f.z * RAY_START, 1.0)
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
        return pos
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

-- ---- state --------------------------------------------------------------------------------------
local armedR, armedL = false, false      -- palm inside the radius (hysteresis applied)
-- An auto-press latches per target: a hand parked on an elevator button presses it ONCE. The latch
-- clears when the hand leaves or the game focuses something else -- so pressing it again is a
-- deliberate reach, not a matter of holding still for another second.
local autoLatch, autoLatchKey, autoStart = false, nil, 0.0
local lastLog = nil

local function palmWorld(right)
    if type(VRPalmWorldPos) ~= 'function' then return nil end
    local ok, p = pcall(VRPalmWorldPos, right and 1 or 0)
    if not ok or not p or (p.w or 0) < 0.5 then return nil end
    return p
end

local function near(p, t, armed)
    if not p then return false end
    local dx, dy, dz = p.x - t.x, p.y - t.y, p.z - t.z
    local d = math.sqrt(dx * dx + dy * dy + dz * dz)
    return d <= (armed and RADIUS_OUT or RADIUS_IN), d
end

local function publish(mask, press)
    if type(SetVRInteract) ~= 'function' then return end
    pcall(SetVRInteract, mask, press and 1 or 0)
end

local function clear()
    armedR, armedL = false, false
    autoLatch, autoLatchKey, autoStart = false, nil, 0.0
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
    -- THE HAND POINTS FIRST, the head only when the hand has nothing to say.
    local camO, camF = handRay()
    if not camO then camO, camF = cameraRay(player) end

    -- TWO POINTS TO BE NEAR, and either is enough, both judged by the SAME radius. The entity origin
    -- is the exact one whenever the origin is the thing (a shard, a bottle, a can). The surface the
    -- line of sight lands on carries everything the origin cannot reach -- panels, doors, terminals.
    -- Neither is a direction test: the hand has to be at a place, which is what stops a raised hand
    -- from opening a door across the room.
    local hit = castHit(camO, camF)
    local function onIt(palm, wheeled, wasArmed)
        if wheeled or not palm then return false, nil, nil end
        local byOrigin, d = false, nil
        if tp then byOrigin, d = near(palm, tp, wasArmed) end
        local byHit, dh = false, nil
        if hit then byHit, dh = near(palm, hit, wasArmed) end
        return (byOrigin or byHit), d, dh
    end

    local nearR, dR, hitR = onIt(pR, wheelR, armedR)
    local nearL, dL, hitL = onIt(pL, wheelL, armedL)
    armedR, armedL = nearR, nearL

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
    local isDevice, cls = false, '?'
    if target then
        pcall(function() cls = tostring(target:GetClassName()) end)
        for _, n in ipairs({ 'gameDeviceBase', 'Device', 'gameinteractionsvisDevice' }) do
            local ok, yes = pcall(function() return target:IsA(n) end)
            if ok and yes then isDevice = true break end
        end
        -- Belt and braces for anything whose base class the RTTI does not answer for: the class name
        -- of a device says so on its face (Terminal, Door, ElevatorPanel, VendingMachine...).
        if not isDevice and (string.find(cls, 'Device') or string.find(cls, 'Terminal')
                             or string.find(cls, 'Door') or string.find(cls, 'Elevator')) then
            isDevice = true
        end
    end
    local isItem = not isDevice

    local press = false
    if isItem or not AUTO_DEVICES then
        local gR = slot(SLOT_GRIP_R) > 0.5
        local gL = slot(SLOT_GRIP_L) > 0.5
        press = (armedR and gR) or (armedL and gL)
    else
        local okk, key = false, nil
        if target then okk, key = pcall(function() return tostring(target:GetEntityID().hash) end) end
        key = (okk and key) or 'unknown'
        local now = (os and os.clock and os.clock()) or 0.0
        -- A different object under the hand is a different press, however little the hand moved.
        if autoLatchKey ~= key then
            autoLatch, autoLatchKey, autoStart = false, key, now
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

    publish(mask, press)

    -- ON the object. What is left to say is whether the press went out, and for an ItemObject that
    -- is entirely down to the grip -- so the grip is printed rather than inferred.
    note(string.format('ON IT: %s -> %s  press=%s  mask=%d grip R=%s L=%s | R origin %s surface %s',
                       cls, isDevice and 'DEVICE (touch)' or 'ITEM (needs grip)', tostring(press),
                       mask,
                       tostring(slot(SLOT_GRIP_R) > 0.5), tostring(slot(SLOT_GRIP_L) > 0.5),
                       dR and string.format('%.2f', dR) or '--',
                       hitR and string.format('%.2f', hitR) or '--'))

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
