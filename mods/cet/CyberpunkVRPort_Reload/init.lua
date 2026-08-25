-- CyberpunkVRPort_Reload -- the hand-driven physical reload, as its own CET mod.
--
-- Split out of CyberpunkVRPort_HandCollision, where it was built. They are two different jobs on one
-- pair of hands: collision pushes the hands OUT of geometry every frame, the reload holds one hand ON a
-- weapon part. CET keeps exactly ONE callback per event per mod, so while they shared a folder they
-- also had to share one onUpdate.
--
-- EVERYTHING ABOVE THE onUpdate IS AN EXACT EXTRACT of the pre-split init.lua, line ranges marked. It
-- is not a rewrite and must not become one: the first attempt at this split retyped the helpers, and
-- three of them changed behaviour invisibly -- a slot-probe list retyped from memory found no component
-- on a revolver, the holder fallback was dropped, and the probe order was changed so the reload was
-- handed a different slot component than every weapon config had been tuned against.
--
-- Two dependencies here are easy to miss and both are load-bearing: ensureCapture, because the bone
-- snapshot the reload reads only exists while SetVRDiagCapture is on, and ensureDeadband, because the
-- plugin ships a 0.02 m deadband that would eat most of a magnet hold.
--
-- capsules.lua and weapon_silverhand.lua are copies of the collision mod's generated data files: CET
-- cannot require across mods, and the holder guard and the slot probe read them.
--
-- WHAT CROSSES THE MOD BOUNDARY: the wrist this mod owns, published to shared slot [162] through
-- SetVRReloadOwnedHand, so the collision mod can leave that wrist out of its solve and out of its
-- per-frame release. Two CET mods are two sandboxes with no view of each other's tables.

-- DEBUG LOGGING, one flag for every CET bridge: the plugin publishes the launcher's DEBUG state to
-- shared slot [156]. Without it these per-second reports produced megabytes a session for users who
-- never asked for them -- measured 3.4 MB from this mod alone in one sitting. Nothing is deleted: tick
-- DEBUG and every line comes back.
local function dbgOn()
    if type(GetVRSharedSlot) ~= 'function' then return false end
    return GetVRSharedSlot(156) > 0.5
end

-- ---- from init.lua 75-79: the capsule geometry, and the slot names per side
local CAPS = require('capsules')

local HAND_SLOT = { [0] = 'vrp_hand_l', [1] = 'vrp_hand_r' }
local FORE_SLOT = { [0] = 'vrp_fore_l', [1] = 'vrp_fore_r' }
local ARM_SLOT  = { [0] = 'vrp_arm_l',  [1] = 'vrp_arm_r' }

-- ---- from init.lua 84-84: DEAD -- the deadband the reload holds are published against
local DEAD     = 0.002   -- under this the hand is where tracking wants it; publish no hold

-- ---- from init.lua 270-270: push, for the capsule lists below
local function push(l, c) local k = l.n + 1; l.n = k; l[k] = c end

-- ---- from init.lua 292-299: qrot, used by readWeapon to place the weapon slots
local function qrot(qi, qj, qk, qr, x, y, z)
    local tx = 2.0 * (qj * z - qk * y)
    local ty = 2.0 * (qk * x - qi * z)
    local tz = 2.0 * (qi * y - qj * x)
    return x + qr * tx + (qj * tz - qk * ty),
           y + qr * ty + (qk * tx - qi * tz),
           z + qr * tz + (qi * ty - qj * tx)
end

-- ---- from init.lua 311-319: worldFrame: world -> model through the PLAYER transform
local function worldFrame()
    local pl = Game.GetPlayer()
    if not pl then return nil end
    local ok, base = pcall(function() return pl:GetWorldPosition() end)
    if not ok or not base then return nil end
    local ok2, q = pcall(function() return pl:GetWorldOrientation() end)
    if not ok2 or not q then return nil end
    return base.x, base.y, base.z, -q.i, -q.j, -q.k, q.r    -- conjugate: world -> model
end

-- ---- from init.lua 321-332: WCAPS: the weapon slot table, and what readWeapon probes with
-- THE WEAPON, PART BY PART.
--
-- Its collision is a cooked triangle mesh per moving part, and those parts live on the WEAPON's own rig -- which
-- the player's bone snapshot does not reach, covering 95 player bones. The way in is SLOTS: a slot bound to a
-- part's bone reports that bone's live world transform to script, and moves when the part moves. Measured on the
-- Silverhand: FX_slots/Muzzle sits 0.283 m from the gun origin, exactly the barrel's length, and
-- fx_ejection_port at 0.097 m, on the slide. The appearance carries a vrp_<bone> slot per part.
local WCAPS = nil
do
    local ok, t = pcall(function() return require('weapon_silverhand') end)
    if ok then WCAPS = t end
end

-- ---- from init.lua 334-351: the reload module itself
-- PHYSICAL RELOAD, hand-driven. Kept in its own reload/ directory (with per-weapon configs under reload/weapons/)
-- so adding a gun is a data file, not a change here. It reuses this module's model-space hand data and the
-- weapon's slots, and drives the parts through the plugin's VRRigWrite. Loaded soft so a missing file never
-- breaks collision.
-- Reload sub-modules pick up edits only on a full game restart: CET's sandbox hides `package`, so the require
-- cache cannot be dropped from here (trying to crashed init.lua on load).
local Reload = nil
do
    local ok, t = pcall(function() return require('reload/reload') end)
    if ok then
        Reload = t
        pcall(function() spdlog.info('[Reload] module loaded') end)
    else
        -- LOUD failure: a swallowed require error looks exactly like "the grip stopped working" and costs a
        -- debugging round each time. The error names the file and line.
        pcall(function() spdlog.info('[Reload] LOAD FAILED: ' .. tostring(t)) end)
    end
end

-- ---- from init.lua 358-358: slotComp / slotEnt
local slotComp, slotEnt = nil, nil

-- ---- from init.lua 360-388: heldWeapon
-- HOLD ON TO THE WEAPON ONCE FOUND. GetActiveWeapon() is not reliable frame to frame: measured in one session it
-- answered with 58 capsules at 23:32 and nothing at all at 23:36 with the gun still in hand, and it had already
-- answered "no weapon" several times during this work while a pistol was plainly held. Re-resolving every frame
-- therefore drops the weapon's collision at random, and it is what made the rig census record nothing.
--
-- So the last weapon is kept and re-validated cheaply -- can its world position still be read -- and only given
-- up after a run of failures, which is what holstering actually looks like.
local lastW, wMiss = nil, 0
local W_GIVE_UP = 30       -- frames of failure before the weapon is considered gone (~0.7 s at 45 fps)

local function heldWeapon()
    local pl = Game.GetPlayer()
    if not pl then lastW, wMiss = nil, 0 return nil end
    local ok, w = pcall(function() return pl:GetActiveWeapon() end)
    if ok and w then
        lastW, wMiss = w, 0
        return w
    end
    if lastW then
        -- still there? a held entity answers for its position; a despawned one does not
        local ok2, p = pcall(function() return lastW:GetWorldPosition() end)
        if ok2 and p then
            wMiss = wMiss + 1
            if wMiss < W_GIVE_UP then return lastW end
        end
        lastW, wMiss = nil, 0
    end
    return nil
end

-- ---- from init.lua 390-410: the weapon rig arrays readWeapon fills, WGR* included
-- THE WEAPON'S OWN RIG, armed for the physical reload. The parts of a gun -- magazine, slide, hammer -- are moved
-- by the weapon's animation and nothing in script can touch them, but the plugin's pose hook runs on EVERY
-- skeleton's apply and can write parent-local transforms. VRWeaponRigArm points it at this weapon's skeleton the
-- same way the player's is armed. This is a READ for now: if the slide's local translation is seen to move while a
-- vanilla reload plays, then writing it is possible by construction.
--
-- Slot order is fixed by the plugin: mag_std, front_slider, back_slider, rotator, weapon_trigger, hammer, bullet,
-- bullet_reload, barrel.
local RIG_PARTS = { 'mag_std', 'front_slider', 'back_slider', 'rotator', 'weapon_trigger',
                    'hammer', 'bullet', 'bullet_reload', 'barrel' }
local rigEnt, rigFound, rigVia, rigOff = nil, -1, '-', -1
local censusAt = nil
local censusStarted = false
local WGX, WGY, WGZ, WGDX, WGDY, WGDZ, WGN = {}, {}, {}, {}, {}, {}, 0
-- The weapon ENTITY's own origin, in model space. This is the grip: measured, it sits 0.072 m from the wrist
-- holding it. It is NOT taken from a capsule any more -- capsule 1 used to be the grip slice back when the frame
-- bone was cut into three, but the recursive decomposition emits chunks in traversal order, so capsule 1 can be
-- anywhere on the gun, including the muzzle. A muzzle is far from both hands, and while aiming the LEFT one is
-- often nearer, so the holder latched to the wrong hand and the solver then spent every frame shoving the hand
-- that actually holds the pistol off its own pistol. In the log: "stopped by R = vrp_hand_r>barrel".
local WGRX, WGRY, WGRZ = 0.0, 0.0, 0.0

-- ---- from init.lua 412-486: readWeapon: resolves slotComp and reads the slots
-- Reads the live slots into the flat arrays above, in MODEL space. Returns the count, 0 if there is no weapon.
local function readWeapon()
    WGN = 0
    if not WCAPS then return 0 end
    local pl = Game.GetPlayer()
    if not pl then return 0 end
    local w = heldWeapon()
    if not w then slotComp, slotEnt = nil, nil return 0 end
    local bx, by, bz, ii, ij, ik, ir = worldFrame()
    if not bx then return 0 end

    local key = tostring(w:GetEntityID().hash)
    if key ~= slotEnt then slotComp, slotEnt = nil, key end
    if key ~= rigEnt then
        rigEnt = key
        rigFound, rigVia, rigOff = -1, '-', -1
        -- IDENTIFY THE WEAPON'S RIG BY CENSUS, not by searching memory.
        --
        -- Disassembling the pose function settled that it is a remap copy serving SEVERAL rigs, with the fourth
        -- argument selecting the remap table -- and the hook already receives that argument. So the hook records
        -- every (trackBuf, a4) it sees that is not the player's, and the entries that appear when a weapon is
        -- drawn are the weapon's four rigs. Recording arguments is safe; the previous attempt walked a
        -- component's memory looking for the rig and crashed the game.
        --
        -- The bone indices need no search either: they come from the rig assets, read offline.
        --   frame rig    0 barrel_plug 1 barrel 2 muzzle_slot 3 fx_muzzle 4 pos_ironsight 5 front_slider
        --                6 back_slider 7 safelock 8 mag_slot 9 bullet_pull 10 bullet 11 bullet_reload
        --                12 hammer 13 rotator 14 ammo_mover 15 weapon_trigger
        --   magazine rig 0 mag_plug 1 magazine 2 magazine_reload 3 mag_std 4 mag_stdr
        censusAt = 0.0
        -- NOT reset here. Resetting at the draw is what made the flag useless: every entry after it, environment
        -- rigs included, was stamped "born with a weapon out" and total=28 born=28 said nothing. The census starts
        -- with the module instead, so whatever appears once a gun is drawn stands out on its own.
    end

    -- RESOLVE THE SLOT COMPONENT BY ASKING. A weapon carries several: measured, UI_Slots and SlotComponent both
    -- answer found=false for these slots and only FX_slots answers, so the right one is the one that does.
    --
    -- This block was accidentally cut by an edit to the census code above, and the symptom was misleading: every
    -- part failed to resolve, WGN came out 0, and the log printed gun=0 -- which reads as "no weapon in hand" and
    -- sent me looking at GetActiveWeapon, which was innocent. The log now separates the two.
    if not slotComp then
        local cs = w:GetComponents()
        for i = 1, #cs do
            if string.find(tostring(cs[i]:GetClassName()), "SlotComponent") then
                local o, found = pcall(function() return cs[i]:GetSlotTransform(CName.new(WCAPS[1].slot)) end)
                if o and found then slotComp = cs[i] break end
            end
        end
        if not slotComp then return 0 end
    end

    local n = 0
    for i = 1, #WCAPS do
        local c = WCAPS[i]
        local o, found, tr = pcall(function() return slotComp:GetSlotTransform(CName.new(c.slot)) end)
        if o and found and tr then
            local wp = nil
            pcall(function() wp = WorldPosition.ToVector4(tr.Position) end)
            local q = tr.Orientation
            if wp and q then
                -- authored in the bone's frame: rotate into the world, then convert into model space
                local ox, oy, oz = qrot(q.i, q.j, q.k, q.r, c.cx, c.cy, c.cz)
                local axw, ayw, azw = qrot(q.i, q.j, q.k, q.r, c.ax, c.ay, c.az)
                local mx, my, mz = qrot(ii, ij, ik, ir, wp.x + ox - bx, wp.y + oy - by, wp.z + oz - bz)
                local dx, dy, dz = qrot(ii, ij, ik, ir, axw, ayw, azw)
                n = n + 1
                WGX[n], WGY[n], WGZ[n] = mx, my, mz
                WGDX[n], WGDY[n], WGDZ[n] = dx * c.half, dy * c.half, dz * c.half
            end
        end
    end
    WGN = n
    return n
end

-- ---- from init.lua 488-500: ensureCapture and ensureDeadband -- both load-bearing for the reload
local capOn = false
local function ensureCapture()
    if capOn then return end
    if pcall(function() SetVRDiagCapture(1) end) then capOn = true end
end

-- Cut the plugin's deadband to the module's own threshold, so the hold published is the hold applied.
local deadSet = false
local function ensureDeadband()
    if deadSet then return end
    if type(VRHandStopDeadband) ~= 'function' then return end
    if pcall(function() VRHandStopDeadband(DEAD) end) then deadSet = true end
end

-- ---- from init.lua 546-552: fingerSide, used by the capsule split below
-- Finger capsules: slot named vrp_f_<finger><joint>_<l|r>.
local function fingerSide(slot)
    if slot:sub(1, 6) ~= 'vrp_f_' then return nil end
    local tail = slot:sub(-2)
    if tail == '_l' then return 0 elseif tail == '_r' then return 1 end
    return nil
end

-- ---- from init.lua 554-573: HAND / OBST / RIDE: the holder guard tests HAND[side]
-- Which body capsules may stop the rig hanging off the wrist of `side`. Excluded: everything that wrist's own IK
-- places -- upper arm, forearm, hand -- because a capsule that moves when the wrist is pushed is a feedback loop
-- (cause 1). And every hand and finger capsule of EITHER side, because they all hang off a hand bone that carries
-- a clamp; those come back through ridingSet, placed from a raw target instead.
local OBST, HAND, RIDE = {}, {}, {}
for side = 0, 1 do
    OBST[side] = { n = 0 }
    RIDE[side] = { n = 0 }
    for i = 1, #CAPS do
        local c = CAPS[i]
        c.uid = i
        if c.slot ~= ARM_SLOT[side] and c.slot ~= FORE_SLOT[side]
           and c.slot ~= HAND_SLOT[0] and c.slot ~= HAND_SLOT[1]
           and fingerSide(c.slot) == nil then
            push(OBST[side], c)
        end
        if c.slot == HAND_SLOT[side] then HAND[side] = c end
        if c.slot == HAND_SLOT[side] or fingerSide(c.slot) == side then push(RIDE[side], c) end
    end
end

-- ---- from init.lua 742-817: the holder chain, latch and hysteresis included
-- WHICH HAND HOLDS THE WEAPON, latched for the life of that weapon instance. Decided per frame as "nearer wrist"
-- it flipped whenever the free hand came to the grip -- which is where the trigger is -- and each flip swapped
-- which hand was frozen. A weapon does not change hands while equipped.
local HOLDER_DROP = 0.30    -- m: beyond this from the grip, the latched hand plainly is not holding it
local HOLDER_EDGE = 0.05    -- m: the challenger must be this much nearer before the LEFT hand is believed
local holderSide, holderEnt = nil, nil
local dbgDL, dbgDR = -1.0, -1.0

-- WHICH HAND HOLDS THE GUN, asked of the game rather than guessed from geometry.
--
-- The distance test below picks whichever wrist is nearer the grip, and that is wrong exactly when it matters: a
-- hand reaching for the MAGAZINE passes closer to the grip than the hand holding the weapon. The holder then
-- flipped to the free hand -- the gun visibly snapped across to it ("пистолет встает под левую руку") and the
-- reload's magnet, which always drives the hand that is NOT the holder, started pulling the real gun hand.
--
-- The game already knows the answer: a held weapon is attached to `ItemAttachmentSlots.WeaponRight` or
-- `.WeaponLeft`, and `GetItemInSlot` returns it. Verified live -- the active weapon's entity id came back from
-- WeaponRight and from nowhere else. Cached per weapon, because it cannot change without the weapon changing.
local slotHolder, slotHolderFor = nil, nil
local function holderFromSlot()
    local pl = Game.GetPlayer()
    local w = pl and pl:GetActiveWeapon()
    if not w then slotHolder, slotHolderFor = nil, nil; return nil end
    local okId, wid = pcall(function() return w:GetEntityID().hash end)
    if not okId then return nil end
    if slotHolderFor == wid then return slotHolder end
    local ts = Game.GetTransactionSystem()
    if not ts then return nil end
    local found = nil
    for _, e in ipairs({ { 1, 'AttachmentSlots.WeaponRight' }, { 0, 'AttachmentSlots.WeaponLeft' } }) do
        local ok, it = pcall(function() return ts:GetItemInSlot(pl, TweakDBID.new(e[2])) end)
        if ok and it then
            local ok2, h = pcall(function() return it:GetEntityID().hash end)
            if ok2 and h == wid then found = e[1]; break end
        end
    end
    if found ~= nil then slotHolder, slotHolderFor = found, wid end
    return found
end

local function holderOf(rl, rr)
    -- the game's own answer wins; the geometry below is only for when it has none to give
    local s = holderFromSlot()
    if s ~= nil then
        holderSide, holderEnt = s, slotEnt
        local function d0(v)
            if not v then return -1.0 end
            local dx, dy, dz = v.x - WGRX, v.y - WGRY, v.z - WGRZ
            return math.sqrt(dx * dx + dy * dy + dz * dz)
        end
        dbgDL, dbgDR = d0(rl), d0(rr)
        return s
    end
    if slotEnt ~= holderEnt then holderSide, holderEnt = nil, slotEnt end
    local function dist(v)
        local dx, dy, dz = v.x - WGRX, v.y - WGRY, v.z - WGRZ
        return math.sqrt(dx * dx + dy * dy + dz * dz)
    end
    dbgDL = rl and dist(rl) or -1.0
    dbgDR = rr and dist(rr) or -1.0

    if holderSide ~= nil then
        local d = (holderSide == 0) and dbgDL or dbgDR
        if d >= 0.0 and d <= HOLDER_DROP then return holderSide end
        holderSide = nil
    end

    -- Right by default, and the left has to WIN by a margin. Cyberpunk holds a weapon in the right hand; the
    -- left is only ever a support hand, and it can sit as close to the grip as the right one does. A tie going
    -- the wrong way costs the whole feature, since the true holder then gets pushed off its own weapon.
    if rl and rr then
        holderSide = (dbgDL + HOLDER_EDGE < dbgDR) and 0 or 1
    elseif rr then holderSide = 1
    elseif rl then holderSide = 0 end
    return holderSide
end

-- ---- from init.lua 819-821: logAcc, PX/PY/PZ, RAW
local logAcc = 0.0
local PX, PY, PZ = {}, {}, {}      -- the resolved wrist per side, reused
local RAW = {}

-- ---- the frame -----------------------------------------------------------------------------------
--
-- The preamble is the pre-split onUpdate's, verbatim (init.lua 836-849): the same native guards, the
-- same capture and deadband calls, the same raw-hand reads and the same holder resolution -- then the
-- reload call as it stood, with lastW and slotComp exactly as that code produced them.
--
-- What is NOT here is the collision mod's own work: the bone-cache generation, the capsule pool, the
-- hand releases, the weapon/body query toggles and the solve. Those stayed in that mod.
local dbgNote2 = '-'

local function publishOwnedHand(hand)
    if type(SetVRReloadOwnedHand) ~= 'function' then return end
    pcall(function() SetVRReloadOwnedHand((hand == 0 or hand == 1) and hand or -1) end)
end

registerForEvent('onUpdate', function(dt)
    if type(VRHandRawModel) ~= 'function' or type(VRHandStopModel) ~= 'function' then return end
    if type(VRBoneModelPos) ~= 'function' or type(VRBoneModelRot) ~= 'function' then return end
    if type(SetVRDiagCapture) ~= 'function' then return end
    ensureCapture()
    ensureDeadband()

    local rl, rr = VRHandRawModel(0), VRHandRawModel(1)
    if rl and rl.w < 0.5 then rl = nil end
    if rr and rr.w < 0.5 then rr = nil end
    if not rl and not rr then return end
    RAW[0], RAW[1] = rl, rr
    if rl then PX[0], PY[0], PZ[0] = rl.x, rl.y, rl.z end
    if rr then PX[1], PY[1], PZ[1] = rr.x, rr.y, rr.z end

    local holder = nil
    local haveGun = readWeapon() > 0
    if haveGun then
        holder = holderOf(rl, rr)
        if holder ~= nil and (not RAW[holder] or not HAND[holder]) then holder = nil end
    end

    if Reload then
        local okF, errF = pcall(function() Reload.frame(lastW, slotComp, holder, dt) end)
        dbgNote2 = okF and '-' or ('FRAME-ERR: ' .. tostring(errF))
    end

    -- Published every frame, releases included: the collision mod has to see a wrist let go as
    -- promptly as it sees it taken.
    publishOwnedHand(Reload and Reload.ownedHand or nil)

    logAcc = logAcc + (dt or 0.016)
    if logAcc >= 1.0 and dbgOn() then
        logAcc = 0.0
        pcall(function()
            spdlog.info(string.format('[Reload] gun=%s slots=%s holder=%s own=%s dL=%.3f dR=%.3f %s',
                haveGun and 'yes' or 'no', slotComp and 'yes' or 'no', tostring(holder),
                tostring(Reload and Reload.ownedHand), dbgDL, dbgDR, dbgNote2))
        end)
        if Reload and Reload.dbg then pcall(function() spdlog.info('[Reload] ' .. Reload.dbg()) end) end
    end
end)

-- ---- from init.lua 1073-1169: the magazine tuner, moved with the feature it tunes
local overlayOpen = false
registerForEvent('onOverlayOpen',  function() overlayOpen = true end)
registerForEvent('onOverlayClose', function() overlayOpen = false end)

registerForEvent('onDraw', function()
    if not overlayOpen or not Reload or not Reload.tune then return end
    local t = Reload.tune
    pcall(function()
        ImGui.Begin('VR magazine tuner')
        ImGui.Text('Hold a magazine and shape the hand. Print writes the numbers to the log.')

        local held, ch = ImGui.Checkbox('hold a magazine (no reload needed)', t.hold)
        if ch then t.hold = held end
        ImGui.Separator()

        ImGui.Text('WHERE IT SITS IN THE HAND  (added to the measured holdOffset, metres)')
        local v
        v, ch = ImGui.SliderFloat('x  along the hand', t.dx, -0.15, 0.15); if ch then t.dx = v end
        v, ch = ImGui.SliderFloat('y', t.dy, -0.15, 0.15);                 if ch then t.dy = v end
        v, ch = ImGui.SliderFloat('z', t.dz, -0.15, 0.15);                 if ch then t.dz = v end
        ImGui.Text('HOW IT IS TURNED  (degrees, on top of the measured rotation)')
        v, ch = ImGui.SliderFloat('pitch', t.rx, -180.0, 180.0); if ch then t.rx = v end
        v, ch = ImGui.SliderFloat('yaw',   t.ry, -180.0, 180.0); if ch then t.ry = v end
        v, ch = ImGui.SliderFloat('roll',  t.rz, -180.0, 180.0); if ch then t.rz = v end
        ImGui.Separator()

        ImGui.Text('FINGERS  0 = tracking, 1 = the recorded pose, above 1 curls HARDER than the animation')
        for _, f in ipairs(t.joints or {}) do
            if ImGui.TreeNode(f.name) then
                for k, j in ipairs(f.idx) do
                    v, ch = ImGui.SliderFloat(f.label[k] .. '##' .. f.name, t.w[j], 0.0, 2.5)
                    if ch then t.w[j] = v; t.rev = (t.rev or 0) + 1 end
                end
                local all = 0.0
                for _, j in ipairs(f.idx) do all = all + t.w[j] end
                all = all / #f.idx
                v, ch = ImGui.SliderFloat('ALL##' .. f.name, all, 0.0, 2.5)
                if ch then
                    for _, j in ipairs(f.idx) do t.w[j] = v end
                    t.rev = (t.rev or 0) + 1
                end
                ImGui.TreePop()
            end
        end
        if ImGui.Button('every joint 1') then
            for i = 1, 19 do t.w[i] = 1.0 end; t.rev = (t.rev or 0) + 1
        end
        ImGui.SameLine()
        if ImGui.Button('every joint 0') then
            for i = 1, 19 do t.w[i] = 0.0 end; t.rev = (t.rev or 0) + 1
        end
        ImGui.Separator()

        ImGui.Text('POSE STAGE  (-1 follows the magazine, otherwise pins to that depth in metres)')
        v, ch = ImGui.SliderFloat('stage depth', t.stage, -1.0, 0.15); if ch then t.stage = v end
        ImGui.Separator()

        -- HOW DEEP IT GOES BEFORE IT CLICKS IN. A fraction of the weapon's own measured insertion run; -1 leaves the
        -- config's value alone. This is a judgement made by looking rather than a number in the recording -- the
        -- animation lets go at whatever depth ITS magazine mesh reaches, and another mesh reads differently there.
        ImGui.Text('WELL  (-1 keeps the weapon config, otherwise the fraction of the run at which it catches)')
        v, ch = ImGui.SliderFloat('seat depth', t.seat or -1.0, -1.0, 1.0); if ch then t.seat = v end
        ImGui.Separator()

        -- HOW FAR AHEAD THE HELD MAGAZINE IS PLACED, in frames of the player's own travel. The entity is put
        -- where the game state has the hand while the hand itself is drawn a frame further on, so at a walk the
        -- two separate by a frame of speed and close again at a standstill. Walk with a magazine in hand and
        -- turn this until it rides the palm: 0 shows the lag whole, and past the truth it runs on ahead.
        ImGui.Text('HELD ENTITY LEAD  (frames of the player\'s travel; 0 = off)')
        v, ch = ImGui.SliderFloat('lead frames', t.lead or 0.0, 0.0, 4.0); if ch then t.lead = v end
        ImGui.Separator()

        if ImGui.Button('PRINT to the log') then
            pcall(function()
                spdlog.info(string.format(
                    '[MagTune] holdOffset += { %.5f, %.5f, %.5f }   rot += { %.2f, %.2f, %.2f } deg',
                    t.dx, t.dy, t.dz, t.rx, t.ry, t.rz))
                local w = {}
                for i = 1, 19 do w[i] = string.format('%.2f', t.w[i]) end
                spdlog.info('[MagTune] joints = { ' .. table.concat(w, ', ') .. ' }')
                spdlog.info(string.format('[MagTune] seatAt = %s',
                    ((t.seat or -1) >= 0) and string.format('%.3f', t.seat) or 'config'))
                spdlog.info(string.format('[MagTune] lead = %.2f frames', t.lead or 0.0))
            end)
        end
        ImGui.SameLine()
        if ImGui.Button('reset') then
            t.dx, t.dy, t.dz, t.rx, t.ry, t.rz = 0, 0, 0, 0, 0, 0
            t.seat = -1.0
            t.lead = 2.0
            for i = 1, 19 do t.w[i] = 1.0 end
            t.rev = (t.rev or 0) + 1
            t.stage = -1.0
        end
        ImGui.End()
    end)
end)
-- ---- ADAPTED from init.lua 1171-1178 -- not a verbatim extract, unlike everything above --------
--
-- Two differences, both deliberate: the wrist release is published to the collision mod, and the
-- VRHandStopDeadband(0.02) restore stayed in that mod, whose setting it is.
registerForEvent('onShutdown', function()
    -- Release the wrist and say so, or the collision mod would keep excluding a hand nobody holds.
    publishOwnedHand(nil)
    pcall(function() VRHandStopModel(0, false, Vector4.new(0, 0, 0, 0)) end)
    pcall(function() VRHandStopModel(1, false, Vector4.new(0, 0, 0, 0)) end)
    -- the wrist rotation lock is plugin-side state; left on through a mod reload it would freeze the hand's aim
    pcall(function() VRHandStopRot(0, 0, 0, 0, 0, 1) end)
    pcall(function() VRHandStopRot(1, 0, 0, 0, 0, 1) end)
end)
