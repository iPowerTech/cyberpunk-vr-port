-- Read the player's gamepad range and remove model input slew only during a VR
-- wheel grab. The model's steering limits, speed assists and tire physics stay
-- native. Shared drive-model flats are restored when the grip/driver changes.
local patch = nil
local profileTime = 0.5
local reportedError = false
local rates = {
    turnUpdateInputSlowChangeSpeed = 100.0,
    turnUpdateInputFastChangeSpeed = 100.0,
    wheelTurnMaxAddPerSecond = 10000.0,
    wheelTurnMaxSubPerSecond = 10000.0,
}

local function flat(id, name) return TweakDBID.new(id, "." .. name) end
local function key(id) return tostring(id.hash) .. ":" .. tostring(id.length) end
local function same(a, b) return type(a) == "number" and math.abs(a - b) < 0.00001 end

local function restore()
    if not patch then return end
    for name, entry in pairs(patch.values) do
        local id = flat(patch.id, name)
        -- An external edit made while held belongs to its author, not this mod.
        if same(TweakDB:GetFlat(id), entry.applied) then
            assert(TweakDB:SetFlat(id, entry.before) ~= false, "cannot restore steering rate")
        end
        patch.values[name] = nil
    end
    patch = nil
end

local function apply(id)
    local wanted = key(id)
    if patch and patch.key == wanted then return end
    restore()
    local values = {}
    for name, rate in pairs(rates) do
        local before = TweakDB:GetFlat(flat(id, name))
        -- Not all mounted vehicle types use the road-vehicle drive model.
        if type(before) ~= "number" then return end
        values[name] = { before = before, applied = math.max(before, rate) }
    end
    patch = { id = id, key = wanted, values = values }
    for name, entry in pairs(values) do
        local field = flat(id, name)
        assert(TweakDB:SetFlat(field, entry.applied) ~= false, "cannot apply steering rate")
        assert(same(TweakDB:GetFlat(field), entry.applied), "steering rate not accepted")
    end
end

registerForEvent("onUpdate", function(dt)
    local ok, err = pcall(function()
        local state = Game.GetVRWheelControlState()
        if state == 0 then restore(); profileTime = 0.5; return end
        profileTime = profileTime + math.max(0, dt or 0)
        if profileTime >= 0.5 then
            local settings = Game.GetSettingsSystem()
            local inner = settings:GetVar("/controls", "Axis_DeadzoneInnerFix"):GetValue()
            local outer = settings:GetVar("/controls", "Axis_DeadzoneOuter"):GetValue()
            assert(Game.SetVRWheelGamepadProfile(inner, outer), "invalid gamepad range")
            profileTime = 0
        end
        if state % 4 == 0 then restore(); return end
        local player = Game.GetPlayer()
        local vehicle = player and player:GetMountedVehicle()
        if not vehicle then restore(); return end
        local model = TweakDB:GetFlat(flat(vehicle:GetRecordID(), "vehDriveModelData"))
        if not model then restore(); return end
        apply(model)
    end)
    if not ok then
        pcall(restore)
        if not reportedError then print("[driving] wheel profile unavailable: " .. tostring(err)) end
        reportedError = true
    else
        reportedError = false
    end
end)

registerForEvent("onShutdown", function() pcall(restore) end)
return { GetState = function() return { active = patch ~= nil, model = patch and patch.key or nil } end }
