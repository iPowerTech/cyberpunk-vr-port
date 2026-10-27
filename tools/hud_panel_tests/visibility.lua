-- Pure Lua fixtures, run in CET's Lua VM without calling the live Game object.
return function(source)
    local makeModule=assert(loadstring('return function(Game,CName,NameToString,Vector2,os)\n'..source..'\nend'))()
    local function scene(class)
        local stats={reads=0,invalidations=0,publishes=0}
        local clock={second=1}
        local function vec(x,y) return {X=x,Y=y} end
        local info={offset=vec(0,0),size=vec(800,100)}
        local panel={name='mainPanel'}
        local scanPanel={name='scanner_overlay'}
        local border={name='border'}
        local window={size=vec(1920,1080)}
        local root={visible=false,opacity=1,size=vec(1920,1080)}
        local function read() stats.reads=stats.reads+1 end
        function root:IsVisible() return self.visible end
        function root:GetOpacity() return self.opacity end
        function root:GetSize() read();return self.size end
        function root:GetName() return 'Root' end
        function root:GetParentWidget() return window end
        function root:GetUserData() return info end
        function root:FlagForVisualInvalidation() stats.invalidations=stats.invalidations+1 end
        function root:GetWidgetByPathName(name)
            read();if name=='mainPanel' then return panel elseif name=='border' then return border end
        end
        function panel:GetName() return self.name end
        function border:GetName() return self.name end
        function scanPanel:GetName() return self.name end
        function border:GetWidgetByPathName(name) read();if name=='scanner_overlay' then return scanPanel end end
        function root:GetChildPosition(child) read();return child==panel and vec(0,-255) or vec(0,0) end
        function root:GetChildSize(child) read();return child==panel and vec(1400,150) or vec(1920,1080) end
        function border:GetChildPosition() read();return vec(-960,-540) end
        function border:GetChildSize() read();return vec(3840,2160) end
        function window:IsA(name) return name=='inkVirtualWindow' end
        function window:GetChildPosition() read();return vec(960,540) end
        function window:GetSize() read();return self.size end
        function window:SetSize(x,y) self.size=vec(x,y) end
        function window:FlagForVisualInvalidation() stats.invalidations=stats.invalidations+1 end
        local controller={GetClassName=function() return class end,GetRootWidget=function() return root end}
        local layer={GetGameControllers=function() return {controller} end,GetVirtualWindow=function() return root end}
        local game={GetInkSystem=function() return {GetLayer=function() return layer end} end,
            VRHudPanelUpdate=function() stats.publishes=stats.publishes+1;return 1 end}
        local mod=makeModule(game,{new=function(s) return s end},function(s) return s end,
            {new=function() return {} end},{time=function() return clock.second end})
        return {mod=mod,root=root,stats=stats,info=info,window=window,clock=clock}
    end
    local function tick(s)
        s.mod.tick();assert(not s.mod.error and not s.mod.phoneError,s.mod.error or s.mod.phoneError)
    end
    local function compare(a,b)
        for _,key in ipairs({'offset','size'}) do
            assert(a.info[key].X==b.info[key].X and a.info[key].Y==b.info[key].Y,'Visible crop changed: '..key)
        end
        assert(a.window.size.X==b.window.size.X and a.window.size.Y==b.window.size.Y,'Visible window size changed')
    end
    local cases=0
    for _,class in ipairs({'HudPhoneGameController','SubtitlesGameController','gameuiScannerGameController'}) do
        local optimized,legacy=scene(class),scene(class);legacy.mod.refreshHiddenBounds=true
        tick(optimized);tick(legacy)
        assert(optimized.stats.invalidations==2,'New hidden source must clear once')
        local reads,invalidations=optimized.stats.reads,optimized.stats.invalidations
        for _=1,10 do tick(optimized);tick(legacy) end
        assert(optimized.stats.reads==reads and optimized.stats.invalidations==invalidations,'Hidden source still does layout/redraw work')
        optimized.root.visible=true;legacy.root.visible=true;tick(optimized);tick(legacy);compare(optimized,legacy)
        optimized.root.visible=false;tick(optimized)
        assert(optimized.stats.invalidations>invalidations,'Hide transition did not clear old content')
        invalidations=optimized.stats.invalidations;tick(optimized)
        assert(optimized.stats.invalidations==invalidations,'Steady hidden source keeps invalidating')
        optimized.root.visible=true;legacy.root.visible=true
        optimized.root.opacity=0;legacy.root.opacity=0;tick(optimized);tick(legacy)
        optimized.root.opacity=1;legacy.root.opacity=1;tick(optimized);tick(legacy);compare(optimized,legacy)
        optimized.clock.second=2;legacy.clock.second=2;tick(optimized);tick(legacy);compare(optimized,legacy)
        assert(optimized.stats.publishes==legacy.stats.publishes+2,'Capture publication must continue while hidden')
        cases=cases+1
    end
    return 'PASS '..cases..' source types: hidden idle, first show, hide clear, fade and controller rescan'
end
