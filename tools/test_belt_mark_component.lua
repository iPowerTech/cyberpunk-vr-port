-- Execute the production helper with a renderer that faults if Toggle overlaps
-- a queued appearance rebuild. No live components or game state are accessed.
return function(source)
    local module = assert((loadstring or load)(source))()
    local cases = 0
    local function fixture(appearance)
        local state = {loads=0, toggles=0, queued=false, enabled=false}
        local component = {meshAppearance={value=appearance or 'lit'}}
        function component:ChangeAppearance(name, wait)
            state.loads=state.loads+1;state.queued=true;self.meshAppearance.value=name
            if state.fail then return false end
            if wait then state.queued=false end
            return true
        end
        function component:IsEnabled() return state.enabled end
        function component:Toggle(on)
            assert(not state.queued, 'Toggle raced a renderer rebuild')
            state.toggles=state.toggles+1;state.enabled=on
        end
        local current=component
        local function resolve() return current end
        local function apply(on,color) return module.apply(resolve,'grenade_lit',on,color or 'lit',function(x)return x end) end
        return state,component,apply,function(c)current=c end
    end
    do
        local s,c=fixture()
        c:ChangeAppearance('lit')
        assert(not pcall(function()c:Toggle(true)end), 'fixture must reproduce old race');cases=cases+1
    end
    do
        local s,c,apply=fixture()
        for i=1,500 do assert(apply(true));assert(apply(true));assert(apply(false)) end
        assert(s.loads==0 and s.toggles==1000, 'unchanged hover must not reload appearance');cases=cases+1
    end
    do
        local s,c,apply=fixture('lit2')
        assert(apply(true));assert(s.loads==1 and s.toggles==1 and not s.queued)
        assert(apply(false));assert(apply(true));assert(s.loads==1);cases=cases+1
    end
    do
        local s,c,apply,replace=fixture();local s2,c2=fixture()
        replace(c2);c.Toggle=function()error('stale cached component touched')end
        assert(apply(true));assert(s.toggles==0 and s2.toggles==1);cases=cases+1
    end
    do
        local s,c,apply,replace=fixture('lit2')
        local original=c.ChangeAppearance
        c.ChangeAppearance=function(self,name,wait)original(self,name,wait);replace(nil);return true end
        assert(not apply(true));assert(s.toggles==0);cases=cases+1
    end
    do
        local s,c,apply=fixture('lit2');s.fail=true
        assert(not apply(true));assert(not apply(true));assert(s.toggles==0)
        s.fail=false;assert(apply(true));assert(s.toggles==1 and s.loads==3);cases=cases+1
    end
    do
        local s,c,apply,replace=fixture();replace(nil)
        assert(not apply(true));assert(apply(false));assert(s.toggles==0);cases=cases+1
    end
    do
        local s,c,apply=fixture();c.IsEnabled=function()return nil end
        assert(not apply(true));assert(s.toggles==0);cases=cases+1
    end
    do
        assert(not module.apply(function()error('unavailable owner')end,'x',true,'lit',tostring));cases=cases+1
    end
    do
        local s,c,apply=fixture('lit2');s.fail=true
        assert(not apply(true));s.fail=false
        assert(apply(false));assert(not s.queued and s.toggles==0);cases=cases+1
    end
    return cases
end
