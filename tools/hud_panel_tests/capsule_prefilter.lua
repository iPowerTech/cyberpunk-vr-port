-- Exercise the real capsule maintenance helpers with mock entities only.
return function(basketball,collision)
    local bStart=assert(basketball:find('local capsCache, capsAt, capsScanAt',1,true))
    local bEnd=assert(basketball:find("registerForEvent('onUpdate'",bStart,true))
    local cStart=assert(collision:find('local CAPS_PHYS_ON = false',1,true))
    local cEnd=assert(collision:find('-- Weapon query filtering',cStart,true))
    local cases=0
    for _,spec in ipairs({{basketball:sub(bStart,bEnd-1),'capsQueriesOff','queries'},
                           {collision:sub(cStart,cEnd-1),'bodyCapsulesForVehicle','toggles'}}) do
        local function scenario(native)
            local counters={scans=0,queries=0,toggles=0,prefilters=0}
            local function entity(hasCapsule)
                local e={hasCapsule=hasCapsule}
                local ordinary={GetName=function() return 'ordinary_component' end}
                local capsule={GetName=function() return 'VRPortBody_test' end}
                function capsule:Toggle(on) assert(on==false);counters.toggles=counters.toggles+1 end
                function capsule:CreatePhysicalBodyInterface(index)
                    assert(index==0)
                    return {SetIsQueryable=function(_,on) assert(on==false);counters.queries=counters.queries+1 end}
                end
                function e:GetComponents()
                    counters.scans=counters.scans+1
                    return self.hasCapsule and {ordinary,capsule} or {ordinary}
                end
                return e
            end
            local current=entity(false)
            local game={GetPlayer=function() return current end,GetMountedVehicle=function() return nil end}
            local predicate=native and function(e) counters.prefilters=counters.prefilters+1;return e.hasCapsule end or nil
            local factory=assert(loadstring('return function(Game,VRShouldScanBodyCapsules,spdlog)\n'..spec[1]..'\nreturn '..spec[2]..'\nend'))()
            local tick=factory(game,predicate,{info=function() end})
            for i=1,10 do tick(i/90) end
            assert(counters.scans==(native and 0 or 10),'Empty scan forwarding changed')
            assert(counters.queries==0 and counters.toggles==0,'Missing capsules changed physics')
            -- A replacement player/component appearing on the very next frame
            -- must be processed immediately; there is deliberately no TTL cache.
            current=entity(true);tick(11/90)
            assert(counters[spec[3]]==1,'New capsule was not handled on its first tick')
            local before=counters.scans;tick(12/90)
            assert(counters.scans==before,'Existing maintenance cadence/cache was lost')
            if spec[3]=='queries' then tick(1);assert(counters.queries==2,'Query filtering maintenance was lost') end
            cases=cases+1
        end
        scenario(true);scenario(false)
    end
    return 'PASS '..cases..' capsule prefilter cases: absent/present, next-tick replacement, maintenance and older-DLL fallback'
end
