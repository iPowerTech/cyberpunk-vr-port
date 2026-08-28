-- CyberpunkVRPort -- does the INTERACTION follow the native look-at, and can the hand take it over?
--
-- ---- WHAT THE FIRST RUN ESTABLISHED ------------------------------------------------------------
--
-- The decompiled script bundle named a native worth asking:
--
--     InteractionManager.IsInteractionLookAtTarget(activatorOwner, hotSpotOwner) -> Bool
--
-- 210 samples answered it. All 73 `true` landed on one container at 1.52-1.63 m with the head 2.2 to
-- 9.4 degrees off it; an NPC and a vending machine at 11.7-44.6 m never returned true, not even when
-- the look-at named them and the head was 0.1 degrees off. Split by whether the look-at had just
-- named the object: 73 samples with a fresh look-at gave 72 true, and 90 samples after the look-at
-- dropped it gave 1 (a single sample of lag). The palm sat between 68 and 136 degrees off the whole
-- time and changed nothing.
--
--     So that native is "is this the TargetingSystem's current look-at object", plus a range gate. It
--     is not a query of its own, and it is not the thing that picks the interactable.
--
-- ---- AND WHAT IT OVERTURNED --------------------------------------------------------------------
--
-- HandLook concluded that interactions do not follow the look-at, by wrapping
-- PlayerPuppet.UpdateLookAtObject and forcing it to nothing. The decompiled source says that wrap
-- could never have proved it (player.script:779): the method only writes m_isAimingAtFriendly,
-- m_isAimingAtChild and m_distanceFromTargetSquared. It CONSUMES LookAtObjectChangedEvent -- it is not
-- the look-at. The probe counter rising proved the wrap ran, not that anything downstream was cut.
-- The look-at that matters lives in the native TargetingSystem and that experiment never touched it.
--
-- ---- THE EXPERIMENT THIS FILE NOW RUNS ---------------------------------------------------------
--
-- The engine offers a way to cut the right look-at, and the game itself uses it -- deviceBase,
-- vehicleComponent and locomotionTakedown all call it:
--
--     TargetingSystem.AddIgnoredLookAtEntity(instigator, entityId)   (and Remove... to undo)
--
-- So: take the object the look-at names, and make the look-at ignore it for a few seconds at a time,
-- on a timer, while watching whether the game is still OFFERING an interaction. Nobody has to watch
-- anything -- the choice hub is read from the blackboard and logged next to the ignore state, so the
-- log answers on its own:
--
--   * ignored=true and hub=false  -> interactions follow the native look-at. The hand can then take
--     them over from redscript with TargetFilter_Script + RegisterLookAtFilter, no engine hook.
--   * ignored=true and hub=true   -> the interaction system really does run a search of its own, and
--     the hunt goes back to Ghidra -- but this time on a negative that is worth something, because
--     this time the look-at that was cut is the one the engine actually uses.
--
-- ---- WHAT RUN 2 GAVE, AND WHAT IT COULD NOT ----------------------------------------------------
--
-- GAVE: the ignore reaches the interaction system. Of 124 ignored samples the native answered false
-- in 105 and true in 19, and those 19 are the 0.2 s of lag around each phase flip. So
-- AddIgnoredLookAtEntity takes the object out of whatever set IsInteractionLookAtTarget reads.
--
-- COULD NOT: say whether the PROMPT followed, because the hub was logged as a bare yes/no. Looking at
-- the container up close, a hub was up in 44 of 73 ignored samples and 51 of 77 released ones -- no
-- difference. That is not an answer, it is a confound: the blackboard hub is global and there were a
-- door and other props within reach, so another object's prompt is indistinguishable from this one's
-- surviving. Hence hubId/hubTitle below, and: RUN IT SOMEWHERE WITH ONE INTERACTABLE IN REACH.
--
-- THE IGNORE IS ALWAYS UNDONE. One entity is ignored at a time, it is released when the phase flips,
-- when the candidate changes, and on shutdown. An entity left ignored stays unlookable for the rest
-- of the session, which would look exactly like a bug in the port.

local ENABLED   = true
local SAMPLE_DT = 0.2     -- seconds between samples. Fast enough to catch the flip, slow enough that
                          -- a five minute session is a readable log rather than a wall.
local HOLD_SECS = 6.0     -- how long a candidate outlives the look-at that named it. It has to exceed
                          -- IGNORE_SECS: while the object is ignored the look-at stops naming it, and
                          -- a candidate dropped mid-test would end the test instead of measuring it.

-- ---- the ignore test ---------------------------------------------------------------------------
-- OFF. The question this was built to answer -- whether the prompt follows the look-at -- has been
-- overtaken: the ray the whole targeting pipeline is built from is now moved at its source by
-- src/Hooks/LookAtRayFromHand.cpp, so there is nothing left to establish by removing entities from
-- the look-at. Left switchable rather than deleted, because the measurement half of this file (what
-- the native answers, at what head angle, at what distance) is still the way to read what the hook
-- is doing -- and an ignore cycle running underneath the hook would fight it.
local IGNORE_TEST = false
local IGNORE_SECS = 3.0   -- ignored for this long, then released for this long, repeating. Long
                          -- enough that a prompt has time to disappear and come back rather than
                          -- flickering at the edge of the hub's own timing.

-- The camera panel's four counters (SetVRProbe). 0 belongs to HandLook, so these take 2 and 3.
local PROBE_SAMPLES = 2
local PROBE_TRUE    = 3

-- ---- shared slots (see include/Utils/SharedSlots.hpp) -------------------------------------------
local SLOT_IN_VEHICLE = 31

local function slot(i)
    if type(GetVRSharedSlot) ~= 'function' then return 0.0 end
    local ok, v = pcall(GetVRSharedSlot, i)
    return (ok and v) or 0.0
end

local function probe(idx, v)
    if type(SetVRProbe) ~= 'function' then return end
    pcall(SetVRProbe, idx, v)
end

local function now()
    return (os and os.clock and os.clock()) or 0.0
end

-- ---- the two directions that are candidates -----------------------------------------------------
--
-- THE FPP CAMERA COMPONENT, not the render view. The port's VRViewWorldPos sits about a metre below
-- the real camera -- the HandCollision module measured it -- and an angle taken from a metre too low
-- is wrong by tens of degrees at arm's length, which is the whole range this file is trying to read.
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

-- The palm pose the plugin publishes, same source the Interact module's hand ray uses. Returns nil
-- when VRIK has not solved; a missing hand costs the palm column and nothing else.
local function palmRay(right)
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
    return p, fwd
end

-- Degrees between a forward and the direction from that origin to a point. Returns nil rather than a
-- number when either vector is degenerate: a zero-length direction would come back as 0 degrees, i.e.
-- "perfectly aimed", which is the one wrong answer that would survive a reading of the log unnoticed.
local function angleTo(origin, fwd, target)
    if not origin or not fwd or not target then return nil end
    local dx, dy, dz = target.x - origin.x, target.y - origin.y, target.z - origin.z
    local dl = math.sqrt(dx * dx + dy * dy + dz * dz)
    local fl = math.sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z)
    if not (dl > 0.0001) or not (fl > 0.0001) then return nil end
    local c = (dx * fwd.x + dy * fwd.y + dz * fwd.z) / (dl * fl)
    if c > 1.0 then c = 1.0 elseif c < -1.0 then c = -1.0 end
    return math.deg(math.acos(c)), dl
end

-- ---- is the game OFFERING an interaction? -------------------------------------------------------
--
-- This is the half that makes the ignore test self-reading. Lifted from the Interact module, blackboard
-- field probed rather than hardcoded -- the name has moved between game versions and a wrong constant
-- would read as "no interaction, ever", which is precisely the answer this test is looking for and so
-- the one failure that could pass for a result.
local hubField, hubProbed = nil, false

-- AND IT HAS TO SAY *WHICH* HUB, which run 2 proved the hard way. A bare yes/no was read against an
-- ignore applied to ONE entity, and the two do not line up: the hub is global, so a prompt belonging
-- to the door two metres further on reads exactly like the ignored container's own prompt surviving.
-- 44 of 73 ignored samples had a hub up and nothing distinguished them. InteractionChoiceHubData
-- carries `id` (Int32) and `title`, and an id that persists across the ignore is the same prompt --
-- which is the whole question.
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
        spdlog.info(string.format('[VRInteractProbe] choice-hub blackboard field: %s',
                                  hubField or 'NOT FOUND -- the hub column is meaningless'))
    end
    if not hubField then return false end
    local ok, hub = pcall(function()
        return FromVariant(bb:GetVariant(defs.UIInteractions[hubField]))
    end)
    if not ok or not hub then return false end
    local n = 0
    pcall(function() n = #hub.choices end)
    if n <= 0 then return false end
    local id, title = nil, nil
    pcall(function() id = hub.id end)
    pcall(function() title = tostring(hub.title) end)
    return true, id, title
end

-- ---- the native under test ----------------------------------------------------------------------
--
-- PROBED ONCE AND NAMED IN THE LOG. A getter that does not exist and a method that does not exist
-- both fail here as `nil`, and both would otherwise read as "the test said false" -- which is one of
-- the outcomes this file is meant to distinguish. So the first sample says out loud whether the call
-- is even reachable, and an unreachable call stops the module instead of filling a log with answers
-- nobody asked for.
local mgrChecked, mgrOk = false, false

local function interactionManager()
    local ok, mgr = pcall(function() return Game.GetInteractionManager() end)
    if not ok or not mgr then return nil end
    return mgr
end

local function isInteractionLookAtTarget(player, obj)
    local mgr = interactionManager()
    if not mgr then return nil, 'no InteractionManager' end
    local ok, v = pcall(function() return mgr:IsInteractionLookAtTarget(player, obj) end)
    if not ok then return nil, tostring(v) end
    if type(v) ~= 'boolean' then return nil, 'non-boolean answer: ' .. tostring(v) end
    return v, nil
end

-- ---- the ignore, and its release ----------------------------------------------------------------
--
-- ONE ENTITY AT A TIME, AND THE ID IS KEPT, NOT RE-DERIVED. Releasing needs the same EntityID that was
-- added, and an object that has gone out of scope cannot be asked for its id any more -- so the id is
-- stored at the moment it is ignored. Everything else here exists to make sure the release happens:
-- on the phase flip, on a change of candidate, and on shutdown.
local ignoredId, ignoredName = nil, nil

local function targeting()
    local ok, ts = pcall(function() return Game.GetTargetingSystem() end)
    if not ok then return nil end
    return ts
end

local function releaseIgnored(player)
    if not ignoredId then return end
    local ts = targeting()
    if ts and player then
        pcall(function() ts:RemoveIgnoredLookAtEntity(player, ignoredId) end)
    end
    spdlog.info(string.format('[VRInteractProbe] released ignore on %s', tostring(ignoredName)))
    ignoredId, ignoredName = nil, nil
end

local function applyIgnore(player, obj, name)
    if ignoredId then return end
    local oki, id = pcall(function() return obj:GetEntityID() end)
    if not oki or not id then return end
    local ts = targeting()
    if not ts then return end
    local ok, err = pcall(function() ts:AddIgnoredLookAtEntity(player, id) end)
    if not ok then
        spdlog.info('[VRInteractProbe] AddIgnoredLookAtEntity failed: ' .. tostring(err))
        return
    end
    ignoredId, ignoredName = id, name
    spdlog.info(string.format('[VRInteractProbe] ignoring %s', tostring(name)))
end

-- ---- the held candidate -------------------------------------------------------------------------
local candidate, candidateAt, candidateName = nil, -1.0, '?'
local candidateHadHub = false
local lastAnswer, lastHub, lastIgnored, lastHubId = nil, nil, nil, nil
local lastLogAt, sampleAt, phaseAt = -1.0, -1.0, -1.0
local ignorePhase = false

local function objectName(obj)
    local ok, n = pcall(function() return obj:GetClassName().value end)
    if ok and n then return n end
    return '?'
end

registerForEvent('onUpdate', function()
    if not ENABLED then return end

    local t = now()
    if t - sampleAt < SAMPLE_DT then return end
    sampleAt = t

    local player = Game.GetPlayer()
    if not player then return end
    -- A dashboard is not a device the hand is reaching for, and the vehicle prompts are a different
    -- system. Keeping them out keeps the reading clean -- and an ignore must never be left behind on
    -- the way in, so it is released first.
    if slot(SLOT_IN_VEHICLE) > 0.5 then releaseIgnored(player) return end

    if not mgrChecked then
        mgrChecked = true
        mgrOk = interactionManager() ~= nil
        spdlog.info(string.format('[VRInteractProbe] InteractionManager reachable: %s',
                                  tostring(mgrOk)))
    end
    if not mgrOk then return end

    -- The look-at NAMES a candidate; it does not gate the measurement. Once named, the object is kept
    -- for HOLD_SECS so the head can leave it -- or the ignore can take it away -- and the answer can
    -- still be asked for.
    local okl, fresh = pcall(function()
        return Game.GetTargetingSystem():GetLookAtObject(player, false, false)
    end)
    if okl and fresh then
        local name = objectName(fresh)
        -- A NEW OBJECT RELEASES THE OLD ONE FIRST. Two ignores at once and neither result means
        -- anything, because the hub would then be answering about a set rather than an entity.
        if name ~= candidateName then
            if ignoredId then releaseIgnored(player) end
            candidateHadHub, phaseAt = false, -1.0
        end
        candidate, candidateAt, candidateName = fresh, t, name
    elseif candidate and (t - candidateAt) > HOLD_SECS then
        releaseIgnored(player)
        candidate, candidateHadHub, phaseAt = nil, false, -1.0
    end
    -- THE HUB IS READ BEFORE ANYTHING CAN RETURN EARLY. Run 3 spent a whole session on a container
    -- that never offered a prompt at all: hub=false in all 155 samples, and with the hub only sampled
    -- after the candidate checks there was no way to see that from the log. A prompt that never
    -- existed cannot be made to disappear, so the ignore test had nothing to measure.
    local hub, hubId, hubTitle = interactionOffered()

    if not candidate then
        if hub ~= lastHub or (t - lastLogAt) > 2.0 then
            lastHub, lastHubId, lastLogAt = hub, hubId, t
            spdlog.info(string.format('[VRInteractProbe] (no candidate) hub=%-5s hubId=%-6s hubTitle=%s',
                                      tostring(hub), tostring(hubId or '-'), tostring(hubTitle or '-')))
        end
        return
    end

    local okp, tp = pcall(function() return candidate:GetWorldPosition() end)
    if not okp or not tp then return end

    -- THE TEST ONLY STARTS ONCE THIS OBJECT HAS BEEN SEEN WITH A PROMPT UP. Otherwise the ignore
    -- cycle runs against an object the game was never offering, and hub=false throughout reads like a
    -- result when it is only the starting state.
    if hub and not candidateHadHub then
        candidateHadHub = true
        spdlog.info(string.format('[VRInteractProbe] armed on %s -- prompt seen (hubId=%s title=%s)',
                                  candidateName, tostring(hubId or '-'), tostring(hubTitle or '-')))
    end

    -- THE PHASE. Ignored for IGNORE_SECS, released for IGNORE_SECS, so the same object is measured in
    -- both states within a few seconds of each other -- the player does not have to hold still across
    -- a whole session for the two halves to be comparable.
    if IGNORE_TEST and candidateHadHub then
        if phaseAt < 0.0 then phaseAt = t end
        if t - phaseAt >= IGNORE_SECS then
            phaseAt = t
            ignorePhase = not ignorePhase
            if ignorePhase then applyIgnore(player, candidate, candidateName)
            else releaseIgnored(player) end
        end
    end

    local answer, err = isInteractionLookAtTarget(player, candidate)
    if answer == nil then
        -- Once, then stop: a broken call would otherwise write five lines a second forever. The ignore
        -- goes with it -- an idle module must not leave the world altered.
        releaseIgnored(player)
        spdlog.info('[VRInteractProbe] call failed, module idle: ' .. tostring(err))
        mgrOk = false
        return
    end

    probe(PROBE_SAMPLES, 1.0)
    if answer then probe(PROBE_TRUE, 1.0) end

    local co, cf = cameraRay(player)
    local headDeg, headDist = angleTo(co, cf, tp)
    local po, pf = palmRay(true)
    local palmDeg = angleTo(po, pf, tp)

    -- EVERY CHANGE IS LOGGED, and the steady state only every half second. The changes are the
    -- measurement; the steady lines are there so a run that never changed can still be told apart
    -- from a run where the module never sampled.
    local ign = (ignoredId ~= nil)
    local changed = (answer ~= lastAnswer) or (hub ~= lastHub) or (ign ~= lastIgnored)
        or (hubId ~= lastHubId)
    if changed or (t - lastLogAt) > 0.5 then
        lastAnswer, lastHub, lastIgnored, lastHubId, lastLogAt = answer, hub, ign, hubId, t
        spdlog.info(string.format(
            '[VRInteractProbe] %s ignored=%-5s hub=%-5s hubId=%-6s hubTitle=%-22s lookAtTarget=%-5s headDeg=%s palmRDeg=%s dist=%s held=%.1fs%s',
            candidateName,
            tostring(ign), tostring(hub),
            tostring(hubId or '-'), tostring(hubTitle or '-'),
            tostring(answer),
            headDeg  and string.format('%6.1f', headDeg)  or '  ----',
            palmDeg  and string.format('%6.1f', palmDeg)  or '  ----',
            headDist and string.format('%5.2f', headDist) or ' ----',
            t - candidateAt,
            changed and '   <-- CHANGE' or ''))
    end
end)

-- THE ONE THING THAT MUST NOT BE SKIPPED. An entity left in the ignore list stays unlookable for the
-- rest of the session -- no prompt, no scan, no aim assist on it -- and from the player's side that is
-- indistinguishable from the port having broken that object.
registerForEvent('onShutdown', function()
    local ok, player = pcall(function() return Game.GetPlayer() end)
    releaseIgnored(ok and player or nil)
end)
