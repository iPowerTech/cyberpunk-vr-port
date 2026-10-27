local S=dofile('mods/cet/CyberpunkVRPort_Stereo/modules/world_touch_sources.lua')
NameToString=function(s)return s end
local function screen(x,enabled,interactive)
    return {IsA=function(_,name)return name=='WorldWidgetComponent' end,
        IsEnabled=function()return enabled~=false end,
        sceneWidgetProperties={isInteractable=interactive~=false},
        GetLocalToWorld=function()return {W={x=x,y=0,z=0}} end,
        GetName=function()return 'terminalui' end}
end
local function controller(id,components,device,dropPoint)
    local owner={IsA=function(_,name)return (name=='Device' and device~=false) or (name=='DropPoint' and dropPoint==true) end,
        GetEntityID=function()return {hash=id} end,GetComponents=function()return components end}
    return {IsA=function(_,name)return name=='DeviceInkGameControllerBase' end,GetOwnerEntity=function()return owner end}
end
local near=screen(.7)
local panel=controller('terminal-independent-of-door',{near,screen(8),screen(.5,false),screen(.5,true,false)})
local world={controller('far-vending',{screen(12)}),false,panel,panel,controller('player-helper',{screen(0)},false)}
-- An expired weak-handle slot before the nearby terminal, as in the live ink list.
world[2]=nil
setmetatable(world,{__len=function()return 5 end}) -- native array size includes expired slots
local system={GetWorldWidgets=function()return world end}
local found=S.resolve(system,{x=0,y=0,z=0})
assert(#found==1 and found[1].component==near)
assert(found[1].owner=='terminal-independent-of-door')
assert(found[1].dropPoint==false)
local drop={GetWorldWidgets=function()return {controller('mailbox',{near},true,true)} end}
assert(S.resolve(drop,{x=0,y=0,z=0})[1].dropPoint==true)
-- Leaving the panel / unloaded widgets cannot retain the previous candidate.
assert(#S.resolve(system,{x=100,y=0,z=0})==0)
assert(#S.resolve({GetWorldWidgets=function()return {} end},{x=0,y=0,z=0})==0)
assert(#S.resolve(nil,nil)==0)
print('PASS: independent door terminals, sparse weak handles, distance, disabled screens and owner deduplication')
