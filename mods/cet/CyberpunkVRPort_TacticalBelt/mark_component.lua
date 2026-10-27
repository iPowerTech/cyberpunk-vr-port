-- Codeware ChangeAppearance queues a renderer rebuild unless wait=true. Never
-- race Toggle against that job, and do not rebuild an unchanged highlight.
-- Resolve by name for each mutation: a garment can replace components without
-- changing the player's entity ID. No component handles are cached here.
local M = {}
local retry = setmetatable({}, {__mode='k'}) -- no ownership of retired components

function M.apply(resolve, name, on, appearance, cname)
    local ok, component = pcall(resolve, name)
    if not ok then return false end
    if component == nil then return not on end

    if on or retry[component] then
        local read, current = pcall(function()
            local value = component.meshAppearance
            return tostring(value.value or value)
        end)
        if not read then return false end
        if current ~= appearance or retry[component] then
            retry[component] = true
            local changed, accepted = pcall(function()
                return component:ChangeAppearance(cname(appearance), true)
            end)
            if not changed or accepted ~= true then return false end
            retry[component] = nil
            -- Loading can replace the garment even while the job is awaited.
            local found, latest = pcall(resolve, name)
            if not found or latest ~= component then return false end
        end
    end

    local read, enabled = pcall(function() return component:IsEnabled() end)
    if not read or type(enabled) ~= 'boolean' then return false end
    if enabled == on then return true end
    return pcall(function() component:Toggle(on) end)
end

return M
