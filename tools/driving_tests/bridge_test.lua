-- Run the production bridge against lexical mocks, without live game writes.
return function(source)
    local events, data, writes, profiles, messages = {}, {}, {}, {}, {}
    local state, current, failWrite = 4, "carA", nil
    local fields = { "turnUpdateInputSlowChangeSpeed", "turnUpdateInputFastChangeSpeed",
                     "wheelTurnMaxAddPerSecond", "wheelTurnMaxSubPerSecond" }
    local function id(name) return { hash = name, length = #name } end
    local ids = { carA = id("carA"), carB = id("carB"), modelA = id("modelA"), modelB = id("modelB") }
    data.carA_vehDriveModelData = ids.modelA; data.carB_vehDriveModelData = ids.modelB
    for _, model in ipairs({"modelA", "modelB"}) do
        for i, field in ipairs(fields) do data[model .. "_" .. field] = i end
    end
    local db = {
        GetFlat = function(_, field) return data[field] end,
        SetFlat = function(_, field, value)
            if failWrite == field then return false end
            writes[#writes + 1] = { field, value }; data[field] = value; return true
        end,
    }
    local settings = { GetVar = function(_, path, name)
        assert(path == "/controls")
        return { GetValue = function() return name == "Axis_DeadzoneInnerFix" and .35 or .9 end }
    end }
    local vehicle = { GetRecordID = function() return ids[current] end }
    local player = { GetMountedVehicle = function() return vehicle end }
    local game = {
        GetVRWheelControlState = function() return state end,
        GetSettingsSystem = function() return settings end,
        GetPlayer = function() return player end,
        SetVRWheelGamepadProfile = function(inner, outer)
            profiles[#profiles + 1] = { inner, outer }; return true
        end,
    }
    local init = assert(loadstring("return function(registerForEvent, Game, TweakDB, TweakDBID, print)\n" .. source .. "\nend"))()
    local mod = init(function(name, callback) events[name] = callback end, game, db,
        { new = function(parent, suffix) return parent.hash .. "_" .. suffix:sub(2) end },
        function(message) messages[#messages + 1] = message end)
    local function tick() events.onUpdate(.02) end
    local function original(model)
        for i, field in ipairs(fields) do assert(data[model .. "_" .. field] == i, "native rate not restored") end
    end
    tick(); assert(#writes == 0 and #profiles == 1, "idle driver changed steering rates")
    assert(profiles[1][1] == .35 and profiles[1][2] == .9, "profile does not match user range")
    state = 5; tick(); assert(mod.GetState().active and #writes == 4, "grab did not apply rates")
    for i = 1, 20 do tick() end
    assert(#writes == 4, "steady grip rewrote TweakDB each frame")
    state = 4; tick(); original("modelA"); assert(not mod.GetState().active)
    state = 7; tick(); current = "carB"; tick(); original("modelA")
    assert(mod.GetState().active and data.modelB_turnUpdateInputFastChangeSpeed == 100, "model switch failed")
    state = 0; tick(); original("modelB")
    state = 5; tick(); data.modelB_turnUpdateInputSlowChangeSpeed = 17
    state = 0; tick(); assert(data.modelB_turnUpdateInputSlowChangeSpeed == 17, "external edit overwritten")
    current = "carA"; state = 5; failWrite = "modelA_turnUpdateInputFastChangeSpeed"; tick()
    failWrite = nil; state = 0; tick(); original("modelA")
    assert(#messages == 1, "failed write not reported once")
    state = 5; tick(); events.onShutdown(); original("modelA")
    return { ok = true, writes = #writes, profileReads = #profiles }
end
