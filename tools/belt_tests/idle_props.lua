-- Exercise the production helpers with lexical mocks, never live game objects.
return function(candidate, reference)
    assert(loadstring(candidate)) -- also checks the complete module's local limit
    local function between(source, first, last)
        local a=assert(source:find(first,1,true))
        local b=assert(source:find(last,a+1,true))
        return source:sub(a,b-1)
    end
    local function lookupFactory(source)
        return assert(loadstring('return function(PERFORMANCE,Game,transactions,TweakDBID)\n'
            ..between(source,'local propS =','local function propMesh(')
            ..'\nreturn propObj,propS\nend'))()
    end
    local function lookup(source, legacy)
        local calls, object, fail=0,nil,false
        local api={GetItemInSlot=function() calls=calls+1;if fail then error('retry') end;return object end}
        local fn,state=lookupFactory(source)({legacyEmptyPropPoll=legacy},
            {GetPlayer=function() return {} end},function() return api end,{new=function(v)return v end})
        return fn,state,function()return calls end,function(value,throws)object=value;fail=throws or false end
    end
    local old,_,oldCalls=lookup(reference,true)
    local new,_,newCalls=lookup(candidate,false)
    local disabled={on=false,slot='test'}
    for frame=1,200 do for slot=1,7 do old(slot,disabled,frame*.01);new(slot,disabled,frame*.01) end end
    assert(oldCalls()==1400 and newCalls()<=35,'empty-slot polling was not bounded')
    local cases=1
    do
        local fn,state,calls,set=lookup(candidate,false)
        assert(fn(1,disabled,1)==nil and calls()==1)
        assert(fn(1,disabled,1.1)==nil and calls()==1)
        local obj={};set(obj)
        -- Enabling cannot wait for the disabled slot's negative-cache expiry.
        assert(fn(1,{on=true,slot='test'},1.11)==obj and calls()==2)
        assert(state.fresh[1] and state.obj[1]==obj)
        assert(fn(1,disabled,1.2)==obj and calls()==2)
        set(nil);assert(fn(1,disabled,1.7)==nil and calls()==3)
        assert(fn(1,{on=false,slot='changed'},1.71)==nil and calls()==4)
        set(nil,true);assert(fn(2,disabled,2)==nil)
        local before=calls();fn(2,disabled,2.01);assert(calls()==before+1,'failed lookup cached')
        set(nil);fn(3,disabled,3);before=calls()
        state.objAt[3]=nil;fn(3,disabled,3.01);assert(calls()==before+1,'explicit invalidation ignored')
        cases=cases+7
    end
    -- Legacy A/B retains the old lookup cadence, including empty active slots.
    for _,active in ipairs({false,true}) do
        local a,_,ac=lookup(reference,true)
        local b,_,bc=lookup(candidate,true)
        for i=1,40 do a(1,{on=active,slot='same'},i*.01);b(1,{on=active,slot='same'},i*.01) end
        assert(ac()==bc());cases=cases+1
    end
    do
        local fn,_,calls=lookup(candidate,false)
        for i=1,20 do fn(1,{on=true,slot='active'},i*.01) end
        assert(calls()==20,'active attachment detection delayed');cases=cases+1
    end
    local function updateFactory(source)
        return assert(loadstring('return function(CFG,Game,propObj,propItem,propS,propUnmark,playerPlane,planeS,propAllowSlot,propPlace,propLight,propEmissive,propGlow,PERFORMANCE,overlay)\n'
            ..between(source,'local function updateProps(now)','local function writeOffsetFile(')
            ..'\nreturn updateProps\nend'))()
    end
    local function run(source, scenario, legacy, overlay)
        local trace={}
        local function note(x)trace[#trace+1]=x end
        local player={VRPortDropItem=function()note('drop')end,VRPortHoldItem=function()note('hold-weapon')end,VRPortHoldItemScene=function()note('hold-scene')end}
        local obj=scenario.occupied and {} or nil
        local cfg={props={{on=scenario.active,slot='slot',up=0,back=0,right=0,auto=true}},prop_mark=1,prop_radius_m=.1,prop_glow_level=1}
        local state={obj={},mesh={},objAt={},placed={},at={},item={[1]=scenario.changed and 'old' or 'tag'},plane={[1]=scenario.moved and 0 or 2},want={},fresh={[1]=scenario.fresh},glow={},glowComp={},emis={},lit={},dist={[1]=.05},claimAt={},flip={}}
        local tick=updateFactory(source)(cfg,{GetPlayer=function()return player end},function()note('lookup');return obj end,
            function()note('item');return scenario.noItem and nil or 'item','tag' end,state,
            function()note('unmark')end,function()note('plane');return 2 end,{},
            function()note('allow')end,function()note('place');return true end,
            function()note('light')end,function()note('emissive');return true end,
            function()note('glow');return true end,{legacyEmptyPropPoll=legacy},overlay)
        tick(10)
        return table.concat(trace,','),state
    end
    for _,active in ipairs({false,true}) do
        for _,occupied in ipairs({false,true}) do
            for _,changed in ipairs({false,true}) do
                for _,moved in ipairs({false,true}) do
                    for _,fresh in ipairs({false,true}) do
                        local scenario={active=active,occupied=occupied,changed=changed,moved=moved,fresh=fresh}
                        local expected=run(reference,scenario,true,false)
                        local actual=run(candidate,scenario,false,false)
                        if active or occupied then assert(actual==expected,'active/cleanup sequence changed: '..actual..' / '..expected)
                        else assert(actual=='lookup','idle slot still resolves item or plane') end
                        assert(run(candidate,scenario,true,false)==expected,'legacy behavior changed')
                        assert(run(candidate,scenario,false,true)==expected,'debug-panel item label changed')
                        cases=cases+1
                    end
                end
            end
        end
    end
    return {result='PASS',cases=cases,legacy_empty_reads=oldCalls(),cached_empty_reads=newCalls()}
end
