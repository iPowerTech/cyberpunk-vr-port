-- Inject the production HUD module and Stereo init at the markers. The lexical
-- mocks model CET's two phases; they do not modify game globals or UI objects.
local report = print
local function runColdStart()
    local phase = "load"
    local CName, Vector2, inkHudEntryInfo, Game, NameToString
    local events, hotkeys = {}, {}
    local nativeCalls, initCalls, tickCalls, reloadCalls = 0, 0, 0, 0
    local print = function() end
    local selector = {
        init = function() assert(phase == "init"); initCalls = initCalls + 1 end,
        tick = function() assert(phase == "update"); tickCalls = tickCalls + 1 end,
        reload = function() reloadCalls = reloadCalls + 1 end,
        status = function() return "fixture" end,
    }
    local function loadHud()
-- HUD_MODULE
    end
    local require = function(name)
        assert(phase == "load", "late module load")
        if name == "modules/vrcam_select" then return selector end
        if name == "modules/vr_overlay" then return {tick=function()assert(phase=="update")end,shutdown=function()assert(phase=="shutdown")end} end
        if name == "modules/keypad_input" then return {tick=function()assert(phase=="update")end,shutdown=function()assert(phase=="shutdown")end} end
        assert(name == "modules/hud_panel", "unexpected dependency")
        return loadHud()
    end
    local registerForEvent = function(name, callback)
        assert(phase == "load", "late event registration")
        events[name] = callback
    end
    local registerHotkey = function(name, description, callback)
        assert(phase == "load", "late hotkey registration")
        hotkeys[name] = callback
    end
    local stereo = (function()
-- STEREO_INIT
    end)()
    assert(stereo and stereo.HudPanel and not stereo.ready, "Stereo failed during module load")
    assert(events.onInit and events.onUpdate and events.onShutdown, "missing lifecycle callback")
    assert(hotkeys.vrcam_reload_selection, "hotkey was not registered during load")
    events.onUpdate(0.01)
    assert(nativeCalls == 0 and tickCalls == 0, "game APIs called before initialization")

    -- Registration APIs leave the callback environment; game types appear now.
    require, registerForEvent, registerHotkey = nil, nil, nil
    CName = { new = function(value) return value end }
    NameToString = function(value) return value end
    local layer = {
        GetGameControllers = function() return {} end,
        GetVirtualWindow = function() return {} end,
    }
    Game = {
        GetInkSystem = function() return { GetLayer = function() return layer end } end,
        VRHudPanelUpdate = function() nativeCalls = nativeCalls + 1; return 23 end,
    }
    phase = "init"
    events.onInit()
    assert(stereo.ready and initCalls == 1, "Stereo did not initialize")
    phase = "update"
    events.onUpdate(0.01)
    assert(tickCalls == 1 and nativeCalls == 1 and stereo.HudPanel.count == 23,
        "HUD or VRCAM updates stopped after startup")
    assert(not stereo.HudPanel.error and not stereo.HudPanel.phoneError, "startup left an error")
    hotkeys.vrcam_reload_selection()
    assert(reloadCalls == 1, "registered hotkey callback failed")
    phase = "shutdown"
    events.onShutdown()
    assert(nativeCalls == 2, "shutdown did not restore native HUD")
end
runColdStart()
runColdStart()
report("PASS CET cold start twice: no game types at load, loader-only registrations, onInit, HUD/VRCAM updates, hotkey, shutdown")
