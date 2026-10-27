-- Run the actual old/new highlight helpers with lexical mocks, never live writes.
return function(candidate, reference)
    assert(loadstring(candidate)) -- Catch the complete module's Lua local limit too.
    local function extract(source)
        local first=assert(source:find('local function updateHighlight(dt)',1,true))
        local last=assert(source:find('local THROW_CFG_VERSION',first,true))
        return source:sub(first,last-1)
    end
    local factories={}
    for _,source in ipairs({reference,candidate}) do
        local text='return function(CFG,S,PERFORMANCE,hipsModelFrame,palmPoint,elementCentre,markable,toHipsFrame,qrot,OFFSET_GROUP,applyLit,reassertDevices,VRPalmModelPos)\n'
            ..extract(source)..'\nreturn updateHighlight\nend'
        factories[#factories+1]=assert(loadstring(text))()
    end
    local totals={0,0};local comparisons=0
    for scenario=1,128 do
        local runs={}
        for mode,factory in ipairs(factories) do
            local state={comps={},hlAcc=0,litHand={},lit=nil}
            local cfg={highlight=true,highlight_rate_hz=20,highlight_radius_m=0.14,highlight_release=1.35}
            local hands={[0]={x=0,y=0,z=0},[1]={x=0.2,y=0,z=0}}
            local centres,allowed={},{}
            local reads,applied,probes=0,0,0
            local function apply(want)
                state.want=want;state.lit=next(want);applied=applied+1
            end
            local tick=factory(cfg,state,{legacyHighlightScan=false},
                function() if scenario==5 then return nil end return 0.01,0.02,0,0,0,0,1 end,
                function(side) return hands[side] end,
                function(host) local c=centres[host];if c then return c.x,c.y,c.z end end,
                function(host) return allowed[host] end,
                function(x,y,z) return x,y,z end,
                function(_,_,_,_,x,y,z) return -y,x,z end,
                {host3='group3',host4='group3'},apply,
                function() probes=probes+1;return probes end,function() end)
            local function prepare(step)
                state.comps={};centres={};allowed={}
                for i=1,17 do
                    local host='host'..i
                    local enabled=(i+scenario+step)%4~=0
                    local throws=(i==13 and step==3)
                    state.comps[host]={IsEnabled=function() reads=reads+1;if throws then error('detached fixture') end return enabled end}
                    if i%11~=0 then centres[host]={x=(i-8)*0.017+scenario*0.0001,y=step*0.003,z=0} end
                    allowed[host]=i%9~=0
                end
                if scenario%7==0 then hands[0]=nil end
                if scenario%11==0 then hands[1]=nil end
                if hands[0] then hands[0].x=(step-2)*0.021 end
                if hands[1] then hands[1].x=(step-2)*0.09 end
                cfg.highlight=not(scenario%13==0 and step>=3)
                cfg.probe_devices=scenario%19==0
                if scenario%17==0 then state.comps={} end
            end
            runs[mode]={state=state,tick=tick,prepare=prepare,counts=function() return reads,applied,probes end}
        end
        for step=1,6 do
            for _,run in ipairs(runs) do run.prepare(step);run.tick(step%2==0 and 0.06 or 0.01) end
            local a,b=runs[1].state,runs[2].state
            assert(a.handDist==b.handDist and a.lit==b.lit,'Distance or lit selection changed')
            for side=0,1 do
                assert(a.litHand[side]==b.litHand[side],'Per-hand hysteresis changed')
                local ah,bh=a.handSide and a.handSide[side],b.handSide and b.handSide[side]
                assert((ah==nil)==(bh==nil),'Tracking-loss state changed')
                if ah then assert(ah.best==bh.best and ah.d==bh.d,'Closest target changed') end
            end
            for k,v in pairs(a.want or {}) do assert((b.want or {})[k]==v,'Highlight union changed') end
            for k,v in pairs(b.want or {}) do assert((a.want or {})[k]==v,'Highlight union changed') end
            local ar,aa,ap=runs[1].counts();local br,ba,bp=runs[2].counts()
            assert(aa==ba and ap==bp and br<=ar,'Cadence or probe forwarding changed')
            comparisons=comparisons+1
        end
        for mode,run in ipairs(runs) do local reads=run.counts();totals[mode]=totals[mode]+reads end
    end
    assert(totals[2]<totals[1]*0.65,'Component-read reduction missing')
    return {cases=comparisons,legacy_reads=totals[1],shared_reads=totals[2],result='PASS'}
end
