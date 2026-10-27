local report=print
local function test()
    local player,loading=nil,true
    local preGame,requestsAvailable,sessionError=true,true,false
    local layerError=false
    local files={['vrcam.json']='{"component":"vrcam_2560x2560"}',['bridge/vrcam_enable.txt']='1'}
    local io={open=function(path,mode)
        if mode=='r' and files[path]==nil then return nil end
        return {read=function()return files[path]end,write=function(_,s)files[path]=s end,close=function()end}
    end}
    local print=function()end
    local CName={new=function(name)return name end}
    local controller={GetClassName=function()return {value='inkFastTravelLoadingControllerSupervisor'}end,
        GetRootWidget=function()return {IsVisible=function()return loading end}end}
    local layer={GetGameControllers=function()return {controller}end}
    local Game={GetSystemRequestsHandler=function()
        if sessionError then error('session status unavailable')end
        if not requestsAvailable then return nil end
        return {IsPreGame=function()return preGame end}
    end,GetInkSystem=function()
        if layerError then error('loading layer replacing')end
        return {GetLayer=function(_,name)assert(name=='inkLoadingLayer');return layer end}
    end,GetPlayer=function()
        if not player then return nil end
        local entity=player
        -- CET can create a fresh wrapper on every query of the same entity.
        return {native=entity,GetComponents=function()return entity.components end,
            GetEntityID=function()return {hash=entity.id}end}
    end}
    local function entity(id,bd)
        local e={id=id,components={}}
        for _,resolution in ipairs({'2560x2560','1024x1024'})do
            local c={name={value=(bd and 'vrcam_braindance_' or 'vrcam_')..resolution},
                virtualCameraName={value='vrcam_feed_'..resolution},isEnabled=false,calls=0}
            c.Toggle=function(self,on)self.isEnabled=on;self.calls=self.calls+1 end
            e.components[#e.components+1]=c
        end
        return e
    end
    local module=(function()
-- VRCAM_MODULE
    end)()
    module.init()
    local function tick(n)for _=1,(n or 1)do module.tick(.011)end end
    local preview=entity(1);preview.components[1].isEnabled=true
    player=preview;loading=false;tick()
    assert(module.loadingActive and not preview.components[1].isEnabled and module.prevPlayer==nil,
        'main-menu preview player enabled VRCAM before a session')
    assert(files['bridge/vrcam_active.txt']=='','main menu advertised an active second eye')
    player=entity(2);player.components[1].isEnabled=true;tick(7)
    assert(not player.components[1].isEnabled,'replacement menu preview kept an enabled RTT')
    sessionError=true;tick(7);assert(module.loadingActive,'unavailable session state enabled a menu camera');sessionError=false
    requestsAvailable=false;tick(7);assert(module.loadingActive,'missing request handler enabled a menu camera')
    requestsAvailable=true;preGame=false;loading=true;player=nil
    tick();assert(module.loadingActive and module.prevPlayer==nil,'initial loading retained a player')
    local first=entity(7);first.components[1].isEnabled=true;player=first;tick(7)
    assert(not first.components[1].isEnabled,'restored RTT ran before initial loading finished')
    loading=false;tick();assert(first.components[1].isEnabled and module.enabledCount==1,'first player did not resume')
    local calls=first.components[1].calls;tick(65)
    assert(first.components[1].calls==calls,'fresh CET wrappers retriggered camera binding')
    loading=true;tick()
    assert(not first.components[1].isEnabled and module.prevPlayer==nil and module.appliedId==nil,
        'save replacement retained the old camera/player or its applied identity')
    assert(files['bridge/vrcam_enable.txt']=='1','loading overwrote the requested camera setting')
    assert(files['bridge/vrcam_active.txt']=='','native bridge still advertised an active loading camera')
    local second=entity(7);second.components[1].isEnabled=true;player=second;tick(7)
    assert(not second.components[1].isEnabled,'same-ID replacement camera rendered during loading')
    layerError=true;tick();assert(module.loadingActive,'unavailable layer resumed RTT early');layerError=false
    loading=false;tick()
    assert(second.components[1].isEnabled and module.prevPlayer.native==second and not first.components[1].isEnabled,
        'same EntityID prevented rebinding the replacement player')
    player=nil;tick();assert(module.prevPlayer==nil and not second.components[1].isEnabled,'nil player retained old world RTT')
    local third=entity(7);player=third;tick();assert(third.components[1].isEnabled,'returning player kept a stale retry delay')
    loading=true;tick();files['bridge/vrcam_enable.txt']='0';player=entity(7);tick(7);loading=false;tick()
    assert(module.want==false and module.enabledCount==0,'load completion ignored the user disabled setting')
    files['bridge/vrcam_enable.txt']='1';tick(31);assert(module.enabledCount==1,'user enabling camera stopped working')
    local old=player;player=entity(8,true);tick(31)
    assert(module.onBdReplacer and player.components[1].isEnabled and not old.components[1].isEnabled,
        'loading guard broke replacer selection or previous-entity release')
    preGame=true;tick()
    assert(not player.components[1].isEnabled and module.prevPlayer==nil and files['bridge/vrcam_active.txt']=='',
        'return to main menu retained the gameplay RTT')
    preGame=false;tick()
    assert(player.components[1].isEnabled and module.enabledCount==1,'leaving pregame failed to restore the selected camera')
    report('PASS VRCAM main-menu preview, session readiness, initial/second load, same-ID replacement, camera release, disabled preference, wrapper identity and replacers')
end
test()
