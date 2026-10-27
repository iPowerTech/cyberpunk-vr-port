return function(source)
    local events, observers, overrides, sent, tops, errors = {}, {}, {}, {}, {}, {}
    local state, paused, present, axis = 10, false, true, -.6
    local player={GetEntityID=function() return {hash=1} end,GetPlayerStateMachineBlackboard=function()
        return {GetInt=function() return state end}
    end}
    local position={x=-1862.615,y=-2354.831,z=19.754}
    local matrix={W={x=-1862.695,y=-2354.753,z=37.354},X={x=-.6985,y=-.7156,z=0},
        Y={x=.7156,y=-.6985,z=0},Z={x=0,y=0,z=1}}
    local target={GetClassName=function() return {value='Ladder'} end,GetWorldPosition=function() return position end,
        GetComponents=function() return {{GetName=function() return {value='generic_ladder_f_finisher_rail'} end,
            GetLocalToWorld=function() return matrix end}} end}
    local lookups=0
    local game={GetPlayer=function() return present and player or nil end,
        GetTargetingSystem=function() return {GetLookAtObject=function() lookups=lookups+1;return target end} end,
        SetVRLadderTopRails=function(p,at,w,x,y,z) tops[#tops+1]={player=p,at=at,w=w,x=x,y=y,z=z};return 1 end,
        GetSystemRequestsHandler=function() return {IsGamePaused=function() return paused end} end,
        SetVRLadderState=function(p,s,pause,desc) sent[#sent+1]={player=p,state=s,paused=pause,description=desc} end,
        GetVRLadderDebug=function() return axis end}
    local init=assert(loadstring("return function(registerForEvent,ObserveAfter,Override,Game,CName,GetAllBlackboardDefs,print)\n"..source.."\nend"))()
    init(function(k,f) events[k]=f end,function(c,m,f) observers[c..'.'..m]=f end,
        function(c,m,f) overrides[c..'.'..m]=f end,game,{new=function(s) return s end},
        function() return {PlayerStateMachine={LocomotionDetailed=1}} end,function(e) errors[#errors+1]=e end)
    events.onInit()
    local descriptor={tag='current ladder',position=position,topHeightFromPosition=17.2}
    local context={GetConditionScriptableParameter=function() return descriptor end,GetTemporaryScriptableParameter=function() return nil end}
    observers['LadderEvents.OnUpdate']({},.016,context,{owner=player})
    events.onUpdate();assert(sent[#sent].state==10 and sent[#sent].description==descriptor)
    assert(#tops==1 and tops[1].w==matrix.W and tops[1].at==position,'measured top rail transform was not sent')
    local sameTarget=target;target=nil;events.onUpdate()
    assert(#tops==2 and lookups==1,'looking away lost cached finisher geometry')
    local action=overrides['gamestateMachineGameScriptInterface.GetActionValue']
    assert(action({owner=player},{value='MoveY'},function() return 0 end)==-.6,'descent axis was clamped')
    axis=.5;assert(action({owner=player},{value='MoveY'},function() return 0 end)==.5,'ascent axis missing')
    assert(action({owner=player},{value='MoveY'},function() return -.7 end)==-.7,'manual axis was overridden')
    assert(action({owner=player},{value='Jump'},function() return 0 end)==0,'unrelated action changed')
    paused=true;events.onUpdate();assert(sent[#sent].paused==1 and sent[#sent].description==descriptor)
    paused=false;state=11;events.onUpdate();assert(sent[#sent].state==11)
    state=12;events.onUpdate();assert(sent[#sent].state==12)
    state=13;events.onUpdate();assert(sent[#sent].description==nil,'ladder jump retained geometry')
    local topCount=#tops;state=10;target=sameTarget
    descriptor={position={x=100,y=100,z=0},topHeightFromPosition=4}
    observers['LadderEvents.OnUpdate']({},.016,context,{owner=player});events.onUpdate()
    assert(#tops==topCount,'look-at target from a different ladder supplied the finisher')
    observers['LadderEvents.OnUpdate']({},.016,context,{owner=player});observers['PlayerPuppet.OnGameAttached']()
    state=10;events.onUpdate();assert(sent[#sent].description==nil,'new save retained old ladder')
    present=false;events.onUpdate();assert(sent[#sent].state==-1 and sent[#sent].player==nil)
    events.onShutdown();assert(sent[#sent].state==-1 and sent[#sent].paused==1)
    assert(#errors==0,'bridge swallowed a Lua error: '..tostring(errors[1]))
    return {ok=true,publications=#sent,topPublications=#tops,lookups=lookups}
end
