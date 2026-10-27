-- Development-only, named Boolean CVars actually hashed by native219730.
local options = {
    { "Rendering", "DLSSDSeparateParticleColor" },
    { "Rendering", "DLSSDTransparentGuide" },
    { "Developer/FeatureToggles", "CharacterSubsurfaceStochastic" },
}
local state = { enabled = false, baseline = nil, dirty = false, elapsed = 0, lastId = 0 }

local function readJson(path)
    local file = io.open(path, "r")
    if not file then return nil end
    local body = file:read("*a"); file:close()
    local ok, value = pcall(json.decode, body)
    return ok and type(value) == "table" and value or nil
end

local function writeStatus(id, status, extra)
    local value = { runId = state.runId, id = id, status = status, dirty = state.dirty, updatedAt = os.time() }
    for key, item in pairs(extra or {}) do value[key] = item end
    local file = io.open("settings_probe_status.json", "w")
    if file then file:write(json.encode(value)); file:close() end
end

local function values()
    local result = {}
    for _, option in ipairs(options) do
        local name = option[1] .. "/" .. option[2]
        local value = GameOptions.Get(option[1], option[2])
        if value ~= "true" and value ~= "false" then error("missing or non-Boolean cache CVar: " .. name) end
        result[name] = value == "true"
    end
    return result
end

local function set(request)
    for _, option in ipairs(options) do
        GameOptions.SetBool(option[1], option[2], request[option[1] .. "/" .. option[2]])
    end
    local confirmed = values()
    for name, value in pairs(request) do
        if confirmed[name] ~= value then error("cache CVar setter did not apply: " .. name) end
    end
    return confirmed
end

local function restore()
    if not state.baseline or not state.dirty then return end
    set(state.baseline)
    state.dirty = false
end

local function apply(request)
    if not state.baseline then error("begin required before changing cache CVars") end
    if type(request.values) ~= "table" then error("missing cache CVar values") end
    local current = values()
    for name in pairs(request.values) do
        if current[name] == nil then error("CVar outside probe whitelist") end
    end
    for name in pairs(current) do
        if type(request.values[name]) ~= "boolean" then error("all three Boolean CVar values required") end
    end
    state.dirty = true
    return set(request.values)
end

registerForEvent("onInit", function()
    local run = readJson("settings_probe_run.json")
    if not run or run.enabled ~= true or type(run.runId) ~= "string" or #run.runId > 80 or
        type(run.expiresAt) ~= "number" or run.expiresAt <= os.time() then return end
    state.enabled = true; state.runId = run.runId; state.expiresAt = run.expiresAt
    writeStatus(0, "ready")
end)

registerForEvent("onUpdate", function(delta)
    if not state.enabled then return end
    state.elapsed = state.elapsed + delta
    if state.elapsed < 0.1 then return end
    state.elapsed = 0
    if os.time() >= state.expiresAt then
        local ok, err = pcall(restore)
        writeStatus(state.lastId, ok and "expired_restored" or "expired_restore_failed", { error = not ok and tostring(err) or nil })
        state.enabled = false
        return
    end
    local request = readJson("settings_probe_command.json")
    if not request or request.runId ~= state.runId or type(request.id) ~= "number" or
        request.id % 1 ~= 0 or request.id <= state.lastId or request.id > 1000 then return end
    state.lastId = request.id
    local ok, result = pcall(function()
        if request.action == "restore" then
            restore(); return values()
        end
        local handler = Game.GetSystemRequestsHandler()
        if not Game.GetPlayer() or not handler or handler:IsPreGame() or handler:IsGamePaused() then
            error("cache CVar probe requires unpaused gameplay")
        end
        if request.action == "begin" then
            if state.baseline then error("cache CVar baseline already captured") end
            state.baseline = values(); return state.baseline
        elseif request.action == "inspect" then
            if not state.baseline then error("cache CVar baseline is missing") end
            return values()
        elseif request.action == "apply" then
            return apply(request)
        end
        error("unsupported cache CVar probe command")
    end)
    if ok then
        writeStatus(request.id, "applied", { values = result, baseline = state.baseline })
    else
        local restored, err = pcall(restore)
        writeStatus(request.id, "failed", { error = tostring(result), rollback = restored,
            rollbackError = not restored and tostring(err) or nil })
    end
end)

registerForEvent("onShutdown", function()
    if state.enabled then
        local ok, err = pcall(restore)
        writeStatus(state.lastId, ok and "shutdown_restored" or "shutdown_restore_failed", { error = not ok and tostring(err) or nil })
    end
end)
