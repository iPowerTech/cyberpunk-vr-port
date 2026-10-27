-- Lexical fixture: no game globals or widgets are modified.
return function(source)
    local make=assert(loadstring('return function(Game,CName,NameToString,Vector2,os,inkHudEntryInfo)\n'..source..'\nend'))()
    local function vector(x,y)return {X=x,Y=y} end
    local second=1
    local window={size=vector(3840,2160)}
    function window:IsA(name)return name=='inkVirtualWindow' end
    function window:GetSize()return self.size end
    function window:SetSize(x,y)self.size=vector(x,y) end
    function window:GetChildPosition()return vector(0,0) end
    function window:FlagForVisualInvalidation()end
    local wrapper={size=vector(3840,2160),visible=true}
    function wrapper:GetName()return 'Root' end
    function wrapper:GetParentWidget()return window end
    function wrapper:IsVisible()return self.visible end
    function wrapper:GetOpacity()return 1 end
    function wrapper:GetSize()return self.size end
    function wrapper:GetUserData()return self.info end
    function wrapper:SetUserData(info)self.info=info end
    function wrapper:FlagForVisualInvalidation()end
    local child={GetName=function()return 'Root' end,GetParentWidget=function()return wrapper end}
    local controller={GetClassName=function()return 'hudSniperNestController' end,GetRootWidget=function()return child end}
    local layer={GetGameControllers=function()return {controller} end,GetVirtualWindow=function()return window end}
    local game={GetInkSystem=function()return {GetLayer=function()return layer end} end,VRHudPanelUpdate=function()return 1 end}
    local mod=make(game,{new=function(s)return s end},function(s)return s end,{new=function()return {} end},
        {time=function()return second end},{new=function()return {offset=vector(0,0),size=vector(0,0)} end})
    local function tick()mod.tick();assert(not mod.error and not mod.phoneError,mod.error or mod.phoneError) end
    tick()
    assert(wrapper.info and wrapper.info.size.X==3840 and wrapper.info.size.Y==2160,'Sniper window crop missing')
    assert(not child.info,'Capture data was attached to the nested sniper widget')
    wrapper.size=vector(2560,1440);tick()
    assert(wrapper.info.size.X==2560 and window.size.X==2560,'Sniper resize reused old bounds')
    wrapper.visible=false;tick();wrapper.visible=true;tick()
    assert(mod.count==1 and wrapper.info.size.Y==1440,'Hidden/visible transition lost the capture')
    second=2;controller.GetClassName=function()return 'UnrelatedController' end
    wrapper.info=nil;tick();assert(not wrapper.info,'Unrelated controller received sniper capture bounds')
    print('PASS sniper briefing wrapper, bounds, resize, visibility and unrelated controller')
end
