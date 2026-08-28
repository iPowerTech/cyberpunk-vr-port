-- CyberpunkVRPort -- jump without a thumb: both hands flick up, the way a person mimes a hop.
--
-- ================================================================================================
-- WHY IT EXISTS. X is Interact AND Reload, and on Touch controllers X is on the LEFT hand -- the
-- same thumb that holds the movement stick. Swapping X and A moves those onto the right thumb (see
-- the Controls tab), which fixes the reload but hands the same problem to Jump, and jumping is the
-- action most tied to moving. So Jump gets a gesture instead, and then no thumb has to leave a
-- stick for it.
--
-- IT MEASURES WHETHER OR NOT IT IS SWITCHED ON, and that is the point of the design. "Hands move up
-- fast" is already the shape of three gestures this port has -- the holster reach, the reload's
-- magazine, the basketball throw -- and a false positive here is not a nuisance: it is a jump in the
-- middle of a firefight, or off a ledge. So the detector always runs and always writes a line with
-- the numbers that made it fire, and the Controls-tab switch decides only whether the press goes
-- out. Play a session with it off, read the "would jump" lines against what you were actually
-- doing, move the thresholds, then turn it on. One session of that beats any amount of choosing
-- numbers in advance.
--
-- WHAT SEPARATES IT FROM THE THREE GESTURES IT COULD BE CONFUSED WITH, and the three tests are
-- independent, so any one of them failing is enough to throw the motion away:
--
--   * THE GRIP. Every one of the three is grip-driven -- the holster equip, the magazine, the ball --
--     and this stands down entirely while either grip is held. That, not the ceiling, is what keeps
--     a basketball throw from reading as a hop: the throw's whole rise happens with the grip closed,
--     and the grip only opens at the top, where a gesture can no longer begin.
--   * PARALLEL. Both hands rise together, within a few centimetres of each other, and the gap
--     between them stays roughly constant. A reload brings one hand TO the other: the heights
--     differ and the separation collapses.
--   * CONTINUITY. One unbroken rise inside a short window. A holster reach is a longer, one-handed
--     travel with a pause at the shoulder.
--
-- HEIGHTS ARE MEASURED FROM THE HEAD, never in world Z. On stairs, in a lift, or on any moving
-- platform the floor moves under the player, and a world-height threshold would fire on its own.
-- Head-relative also roughly scales to the player: belly and shoulders are a fixed distance below
-- the eyes for most people, which is what these numbers are.
-- ================================================================================================

-- ---- settings ---------------------------------------------------------------------------------
local ENABLED = true
-- ARMED FROM THE CONTROLS TAB, not from a constant here. The detector runs either way -- that is
-- how its thresholds get read out of a normal session -- and the switch only decides whether the
-- press goes out. Slot 168, published by the plugin beside the DEBUG one.
local SLOT_ARMED = 168

-- The band, in metres BELOW the head. Negative because hands live below eyes. MEASURED, twice, and
-- the second measurement changed the shape of this list.
--
-- SEATED  : rest -0.41..-0.47, rise 0.22..0.30 in 132..152 ms, ending -0.15..-0.23
-- STANDING: rest -0.44..-0.81, rise 0.36..0.39 in 212..280 ms, ending -0.21..-0.27
--
-- The same gesture, and a different motion: standing it starts from arms-down and travels half as
-- far again, for twice as long. Anything tuned on one posture alone rejects the other.
--
-- THE BAND IS NO LONGER THE DISCRIMINATOR, and tying it to two absolute heights was the mistake.
-- Seated rest sits HIGH (-0.35 at one point) so the start threshold has to be high; standing hops
-- END low (-0.27) so the arrival threshold has to be low -- and with both absolute they collide, and
-- whichever posture loses fails silently. The rise, the parallelism and the continuity are what
-- actually say "hop"; the band is now only a sanity bound, keeping a gesture from starting when the
-- hands are already up.
local START_BELOW = -0.22   -- must begin below shoulder height, and nothing tighter than that
local END_ABOVE   = -0.30   -- and finish at least here
-- THE CEILING IS NOT THE BASKETBALL GUARD, and believing it was cost a wrong proposal. The ball is
-- held by a GRIP and thrown by releasing it (vrport_basketball.reds), and this detector already
-- stands down while either grip is held -- so the whole rise of a throw is invisible to it, and by
-- the time the grip opens at the top the hands are far too high for a gesture to start. What the
-- ceiling is actually for is a two-handed reach far overhead, which is why it sits ABOVE the head:
-- the measured hop keeps travelling to head height after it has been recognised.
local CAP_ABOVE   = 0.10
local MIN_RISE    = 0.18    -- under the seated minimum of 0.22, well under the standing 0.36
-- 0.32, and this number is now the main thing keeping a KNOWN false positive out. Taking the headset
-- off reads as a hop by every other test -- both hands rise together, in the band, without breaking
-- -- and the log caught exactly that: 368 ms, against 149..280 ms for eleven measured real hops
-- across both postures. The 400..600 ms events in the same logs all carried a rise of 0.11..0.19,
-- i.e. they were never hops at all and MIN_RISE was already rejecting them; raising this to 0.60 for
-- their sake bought nothing and let the headset through. 40 ms of margin over the slowest real one.
local MAX_MS      = 0.32
-- How alike the two hands must be.
local PARALLEL_M  = 0.12    -- measured worst case 0.05, so this is untouched
-- THE GAP IS ASYMMETRIC, and that fell out of the data rather than being designed. Every real hop
-- OPENS the hands as they rise -- +0.05 seated, +0.14 standing -- while the two motions this has to
-- reject both CLOSE them: the headset coming off went 0.40 -> 0.24, and a reload brings one hand to
-- the other by definition. A single symmetric threshold cannot separate +0.14 from -0.16 without
-- sitting between them, which is a two-centimetre margin on both sides. Two bounds do it with room
-- to spare, and the closing one is the tight one because nothing legitimate here closes.
local SEP_OPEN_MAX  = 0.25
local SEP_CLOSE_MAX = 0.08
-- 0.025, RAISED FROM the hardcoded 0.01: hop #5 standing died on a single centimetre of give-back
-- after 349 ms of travel and 0.31 m of rise. Over a longer motion a hand hesitates; that is not the
-- gesture breaking, and one centimetre cannot tell the two apart.
local DIP_TOL     = 0.025
local COOLDOWN    = 0.60    -- seconds, so one gesture is one jump

-- ---- shared slots (see include/Utils/SharedSlots.hpp) -------------------------------------------
local SLOT_GRIP_R     = 49
local SLOT_GRIP_L     = 155
local SLOT_IN_VEHICLE = 31

local function slot(i)
    if type(GetVRSharedSlot) ~= 'function' then return 0.0 end
    local ok, v = pcall(GetVRSharedSlot, i)
    return (ok and v) or 0.0
end

local function now()
    return (os and os.clock and os.clock()) or 0.0
end

-- Hand and head positions, all in world space, from the natives the port already publishes.
-- Returns nil when VRIK has not solved -- the gesture simply does not run rather than running on
-- stale numbers, which would be the one failure mode nobody could see.
local function pose()
    if type(VRPalmWorldPos) ~= 'function' or type(VRViewWorldPos) ~= 'function' then return nil end
    local okh, head = pcall(VRViewWorldPos)
    if not okh or not head or (head.w or 0) < 0.5 then return nil end
    local okr, pr = pcall(VRPalmWorldPos, 1)
    local okl, pl = pcall(VRPalmWorldPos, 0)
    if not okr or not okl or not pr or not pl then return nil end
    if (pr.w or 0) < 0.5 or (pl.w or 0) < 0.5 then return nil end
    return head, pr, pl
end

local function sep2d(a, b)
    local dx, dy = a.x - b.x, a.y - b.y
    return math.sqrt(dx * dx + dy * dy)
end

-- ---- the rise ----------------------------------------------------------------------------------
--
-- A tiny state machine rather than a window of samples: the gesture is one unbroken climb, so all it
-- has to remember is where the climb started and whether every frame since has still been climbing.
local rising      = false
local startAt     = 0.0
local startR, startL = 0.0, 0.0
local startSep    = 0.0
local peak        = -10.0     -- highest either hand reached during this rise
local lastR, lastL = nil, nil
local cooldownAt  = -10.0
local nearMisses  = 0
local fired       = 0

local function reset()
    rising = false
    lastR, lastL = nil, nil
end

-- Bounded: a near miss is interesting while the thresholds are being chosen and noise afterwards.
local function nearMiss(why, hr, hl, dt, rise)
    if nearMisses >= 40 then return end
    nearMisses = nearMisses + 1
    spdlog.info(string.format(
        '[VRJump] near miss (%s): R=%.2f L=%.2f rise=%.2f in %.0f ms', why, hr, hl, rise, dt * 1000.0))
end

-- ---- LIVENESS AND THE BAND ---------------------------------------------------------------------
--
-- A SILENT LOG HAS TWO MEANINGS AND THEY ARE OPPOSITE. Either nothing in a whole session looked like
-- a hop -- which is the result this was built to get -- or the detector never saw a usable hand pose
-- and measured nothing at all. From outside, both are an empty file.
--
-- So: one line the first time poses arrive, one line the first time they do not, and then a periodic
-- report of where the hands actually LIVE relative to the head. Those numbers are what the band
-- thresholds have to be built from; the ones in the settings above are an estimate for a 1.75 m
-- player and nothing more. Bounded, so a long session cannot turn this into the log.
local sawPose, sawNoPose = false, false
local wasArmed = false
local bandAt, bandLines = -10.0, 0
local seenMinR, seenMaxR = 10.0, -10.0
local seenMinL, seenMaxL = 10.0, -10.0

registerForEvent('onUpdate', function()
    if not ENABLED then return end

    local head, pr, pl = pose()
    if not head then
        if not sawNoPose then
            sawNoPose = true
            spdlog.info('[VRJump] no hand pose available -- VRIK has not solved, nothing is measured')
        end
        reset()
        return
    end
    if not sawPose then
        sawPose = true
        spdlog.info('[VRJump] hand poses arriving; the detector is measuring')
    end

    -- A hand on a grip is spoken for -- holster, magazine, ball, wheel all live there -- and a
    -- vehicle has its own gestures entirely.
    if slot(SLOT_IN_VEHICLE) > 0.5 or slot(SLOT_GRIP_R) > 0.5 or slot(SLOT_GRIP_L) > 0.5 then
        reset()
        return
    end

    local armed = slot(SLOT_ARMED) > 0.5
    if armed ~= wasArmed then
        wasArmed = armed
        spdlog.info(armed and '[VRJump] ARMED -- gestures now press A'
                          or  '[VRJump] disarmed -- measuring only')
    end
    local t = now()
    local hr = pr.z - head.z          -- negative: below the eyes
    local hl = pl.z - head.z
    local gap = sep2d(pr, pl)

    -- Where the hands have been, so the band can be set from the player rather than from an average.
    if hr < seenMinR then seenMinR = hr end
    if hr > seenMaxR then seenMaxR = hr end
    if hl < seenMinL then seenMinL = hl end
    if hl > seenMaxL then seenMaxL = hl end
    if (t - bandAt) > 5.0 and bandLines < 200 then
        bandAt, bandLines = t, bandLines + 1
        spdlog.info(string.format(
            '[VRJump] band: R now %.2f (seen %.2f..%.2f)  L now %.2f (seen %.2f..%.2f)  gap %.2f  armed=%s',
            hr, seenMinR, seenMaxR, hl, seenMinL, seenMaxL, gap, tostring(armed)))
    end

    if not rising then
        -- START: both hands low, and both moving up since the last frame.
        local movingUp = (lastR ~= nil) and (hr > lastR + 0.002) and (hl > lastL + 0.002)
        if movingUp and hr <= START_BELOW and hl <= START_BELOW and (t - cooldownAt) > COOLDOWN then
            rising, startAt = true, t
            startR, startL, startSep = lastR, lastL, gap
            peak = math.max(hr, hl)
        end
        lastR, lastL = hr, hl
        return
    end

    -- CONTINUING. Any of these ends the rise; the ones worth understanding say why.
    peak = math.max(peak, hr, hl)
    local dt = t - startAt
    local rise = math.min(hr - startR, hl - startL)

    if peak > CAP_ABOVE then
        nearMiss('above the shoulders -- a throw', hr, hl, dt, rise)
        reset() cooldownAt = t
        return
    end
    if math.abs(hr - hl) > PARALLEL_M then
        nearMiss('hands not parallel', hr, hl, dt, rise)
        reset()
        return
    end
    local spread = gap - startSep
    if spread > SEP_OPEN_MAX then
        nearMiss('hands flew apart', hr, hl, dt, rise)
        reset()
        return
    end
    if spread < -SEP_CLOSE_MAX then
        nearMiss('hands closed on each other', hr, hl, dt, rise)
        reset()
        return
    end
    -- BROKEN CLIMB. A real hop does not pause: one frame of falling back ends it.
    if hr < lastR - DIP_TOL or hl < lastL - DIP_TOL then
        if rise > MIN_RISE * 0.5 then nearMiss('the climb broke', hr, hl, dt, rise) end
        reset()
        return
    end
    if dt > MAX_MS then
        if rise > MIN_RISE * 0.5 then nearMiss('too slow', hr, hl, dt, rise) end
        reset()
        return
    end

    lastR, lastL = hr, hl

    -- ARRIVED: high enough, far enough, fast enough, together.
    if hr >= END_ABOVE and hl >= END_ABOVE and rise >= MIN_RISE then
        fired = fired + 1
        cooldownAt = t
        reset()
        spdlog.info(string.format(
            '[VRJump] %s #%d: R=%.2f L=%.2f rise=%.2f in %.0f ms peak=%.2f gap=%.2f',
            armed and 'JUMP' or 'would jump', fired, hr, hl, rise, dt * 1000.0, peak, gap))
        if armed and type(SetVRJump) == 'function' then
            pcall(SetVRJump, 1)
        end
    end
end)
