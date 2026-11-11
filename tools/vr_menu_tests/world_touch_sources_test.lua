local S=dofile('mods/cet/CyberpunkVRPort_Stereo/modules/world_touch_sources.lua')
NameToString=function(s)return s end
CName={new=function(s)return s end}
local function screen(x,enabled,interactive)
    return {IsA=function(_,name)return name=='WorldWidgetComponent' end,
        IsEnabled=function()return enabled~=false end,
        sceneWidgetProperties={isInteractable=interactive~=false},
        GetLocalToWorld=function()return {W={x=x,y=0,z=0}} end,
        GetName=function()return 'terminalui' end}
end
local owners={}
local function controller(id,components,device,dropPoint,origin)
    local owner={IsA=function(_,name)return (name=='Device' and device~=false) or (name=='DropPoint' and dropPoint==true) end,
        GetEntityID=function()return {hash=id} end,components=components,
        GetWorldPosition=function()return {x=origin or 0,y=0,z=0} end}
    owners[id]=owner
    return {IsA=function(_,name)return name=='DeviceInkGameControllerBase' end,GetOwnerEntity=function()return owner end}
end
local near=screen(.7)
local panel=controller('terminal-independent-of-door',{near,screen(8),screen(.5,false),screen(.5,true,false)})
local world={controller('far-vending',{screen(12)}),false,panel,panel,controller('player-helper',{screen(0)},false)}
-- An expired weak-handle slot before the nearby terminal, as in the live ink list.
world[2]=nil
setmetatable(world,{__len=function()return 5 end}) -- native array size includes expired slots
local listed={}
-- The native filters an owner's components by class (VREntityComponentsOf); each call is one owner's listing.
Game={VRInkWorldControllers=function()return world end,
    FindEntityByID=function(id)return owners[id.hash] end,
    VREntityComponentsOf=function(owner,class)
        listed[#listed+1]=owner
        local out={}
        for _,c in ipairs(owner.components) do if c:IsA(class) then out[#out+1]=c end end
        return out
    end}
-- The world's controllers are walked at most once a second unless the eyes move 4 m: every case that changes
-- the world forgets the walk, as a new session does.
local function case(w) world=w;S.reset();listed={} end
local found=S.resolve({x=0,y=0,z=0})
assert(#found==1 and found[1].component==near)
assert(found[1].owner=='terminal-independent-of-door' and found[1].entity==owners['terminal-independent-of-door'])
assert(found[1].dropPoint==false)
-- The deduplicated owner was listed once; far-vending's origin is near (0), so it was listed too.
assert(#listed==2)
-- Leaving the panel / unloaded widgets cannot retain the previous candidate.
assert(#S.resolve({x=100,y=0,z=0})==0)
-- An owner beyond touch reach is skipped without listing its components.
case({controller('far-owner',{screen(40)},true,false,40)})
assert(#S.resolve({x=0,y=0,z=0})==0 and #listed==0)
assert(#S.resolve({x=40.5,y=0,z=0})==1 and #listed==1)
-- A lift's origin is not its car's (measured 93-94 m away from its panel): its screens are listed by their centre.
local lift=controller('lift',{screen(94.7),screen(140)},true,false,0)
local isA=lift:GetOwnerEntity().IsA
lift:GetOwnerEntity().IsA=function(self,name)return name=='LiftDevice' or isA(self,name) end
case({lift})
assert(#S.resolve({x=94,y=0,z=0})==1)
assert(#S.resolve({x=0,y=0,z=0})==0)
-- ...but not past 250 m, and only a lift: another device that far keeps the owner reach.
local tall=controller('tall-lift',{screen(300.3)},true,false,0)
local tallIsA=tall:GetOwnerEntity().IsA
tall:GetOwnerEntity().IsA=function(self,name)return name=='LiftDevice' or tallIsA(self,name) end
case({tall})
assert(#S.resolve({x=300,y=0,z=0})==0)
local far=controller('far-device',{screen(94.7)},true,false,0)
case({far})
assert(#S.resolve({x=94,y=0,z=0})==0)
case({controller('mailbox',{near},true,true)})
assert(S.resolve({x=0,y=0,z=0})[1].dropPoint==true)
case({})
assert(#S.resolve({x=0,y=0,z=0})==0)
assert(#S.resolve(nil)==0)
print('PASS: independent door terminals, sparse weak handles, distance, owner reach, lifts, disabled screens and owner deduplication')
