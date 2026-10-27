-- State bridge tests with lexical API parameters, without real game mutation.
return function(source)
    local events, observers, overrides, sent, messages = {}, {}, {}, {}, {}
    local state, paused, present, fail = 2, false, true, false
    local submerged, headUnder, highLevel = true, false, 6
    local syntheticForward = .8
    local player = { IsUnderwater=function() return submerged end, HasHeadUnderwater=function() return headUnder end,
        GetEntityID = function() return {hash=1} end, GetPlayerStateMachineBlackboard = function()
        return { GetInt = function(_,id) return id==2 and highLevel or state end }
    end }
    local game = {
        GetPlayer = function() if fail then error("temporary failure") end; return present and player or nil end,
        GetSystemRequestsHandler = function() return { IsGamePaused = function() return paused end } end,
        SetVRSwimmingState = function(p, s, f, pause, wet) sent[#sent + 1] = { player = p, state = s, fast = f, paused = pause, waterContext=wet } end,
        GetVRSwimmingDebug = function(index) return index==20 and syntheticForward or index==23 and 1 or 0 end,
    }
    local init = assert(loadstring("return function(registerForEvent, ObserveAfter, Override, Game, GetAllBlackboardDefs, print)\n"
        .. source .. "\nend", "@swimming_state_bridge_test"))()
    init(function(name, fn) events[name] = fn end,
        function(class, method, fn) observers[class .. "." .. method] = fn end,
        function(class, method, fn) overrides[class .. "." .. method] = fn end,
        game, function() return { PlayerStateMachine = { Swimming = 1, HighLevel = 2 } } end,
        function(message) messages[#messages + 1] = message end)
    local function tick(expectedState, expectedFast)
        events.onUpdate()
        local last = sent[#sent]
        assert(last.state == expectedState and last.fast == expectedFast, "wrong swimming state/fast feedback")
        assert(last.paused == (paused and 1 or 0), "pause did not suspend input separately from posture")
        assert(last.waterContext == ((present and (highLevel==6 or submerged or headUnder)) and 1 or 0), "water presence was lost in a PSM transition")
        assert(last.player == (present and player or nil), "publisher used a stale player")
    end
    events.onInit()
    local action = overrides['gamestateMachineGameScriptInterface.GetActionValue']
    local pressed = overrides['gamestateMachineGameScriptInterface.IsActionJustPressed']
    assert(action({owner=player},{value='MoveY'},function() return .25 end)==.8, 'script decision missed swimming forward input')
    assert(action({owner=player},{value='MoveY'},function() return 1 end)==1, 'gesture replaced stronger native input')
    assert(action({owner=player},{value='Reload'},function() return .25 end)==.25, 'unrelated action changed')
    syntheticForward=0
    assert(action({owner=player},{value='MoveY'},function() return -.6 end)==-.6, 'inactive swimming swallowed ladder descent/backwards input')
    syntheticForward=.8
    local other={GetEntityID=function() return {hash=2} end}
    assert(action({owner=other},{value='MoveY'},function() return .25 end)==.25, 'another actor received a swimming gesture')
    assert(pressed({owner=player},{value='Dive'},function() return false end), 'surface Dive transition missed gesture')
    tick(2, -1)
    observers["SwimmingFastDivingEvents.OnEnter"](); tick(2, 1)
    paused = true; tick(2, 1) -- suspend input, retain the actual fast state through the pause
    paused = false; tick(2, 1)
    observers["SwimmingFastDivingEvents.OnExit"](); tick(2, 0)
    state = 1; observers["SwimmingSurfaceEvents.OnEnter"](); tick(1, 0)
    observers["SwimmingSurfaceFastEvents.OnEnter"](); tick(1, 1)
    observers["SwimmingSurfaceFastEvents.OnExit"](); tick(1, 0)
    state = 0; submerged=false;headUnder=false;tick(0,-1) -- high-level water persists through PSM gap
    highLevel=1;headUnder=true;tick(0,-1) -- physical head submersion also retains posture
    state = 2;submerged=true;headUnder=false;tick(2,-1)
    state = 3; submerged=false;headUnder=false;highLevel=1;tick(3, -1)
    state = 0; tick(0, -1)
    observers["PlayerPuppet.OnGameAttached"](); state = 2; tick(2, -1)
    observers["SwimmingDivingEvents.OnEnter"](); tick(2, 0)
    present = false; tick(-1, 0)
    fail = true; local previous = #sent
    events.onUpdate(); events.onUpdate()
    assert(#sent == previous and #messages == 1, "failed bridge refreshed stale state or flooded the log")
    fail = false; present = true; tick(2, 0)
    events.onShutdown()
    assert(sent[#sent].player == nil and sent[#sent].state == -1 and sent[#sent].fast == -1,
        "shutdown did not revoke swimming control")
    return { ok = true, publications = #sent, errorsLogged = #messages }
end
