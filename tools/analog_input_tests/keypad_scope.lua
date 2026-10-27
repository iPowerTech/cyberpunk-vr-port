local zoom, menuOpen, overlay, playerExists, ownerExists, blocked = true,false,false,true,true,false
local ownerId, targetId, calls, lastRoot = 7,7,0,nil
local parent={shown=true,IsVisible=function(self)return self.shown end,GetOpacity=function()return 1 end,GetParentWidget=function()return nil end}
local logic={kind="KeypadDeviceController",IsA=function(self,name)return self.kind==name end}
local root={shown=true,IsVisible=function(self)return self.shown end,GetOpacity=function()return 1 end,
    GetParentWidget=function()return parent end,GetController=function()return logic end}
local controller={deviceWidgetsData={{widget=root}},IsInteractivityBlocked=function()return blocked end}
local owner={cameraZoomActive=true,GetGameController=function()return controller end,GetEntityID=function()return ownerId end}
local player={GetEntityID=function()return 1 end}
local defs={UI_System={IsInMenu="menu"},PlayerStateMachine={IsUIZoomDevice="zoom",UIZoomDeviceID="owner"}}
local boards={Get=function()return {GetBool=function()return menuOpen end}end,
    GetLocalInstanced=function()return {GetBool=function()return zoom end,GetEntityID=function()return targetId end}end}
local Game={GetPlayer=function()if playerExists then return player end end,GetBlackboardSystem=function()return boards end,
    FindEntityByID=function(id)if ownerExists and id==ownerId then return owner end end,
    GetTargetingSystem=function()error("keypad classification must not depend on gaze")end}
local CName={new=function(name)return name end}
local GetAllBlackboardDefs=function()return defs end
local VRKeypadUpdate=function(widget)calls=calls+1;lastRoot=widget;return widget and 1 or 0 end
local M=(function()
-- KEYPAD_MODULE
end)()
assert(M.resolve(false)==root,"active keypad not recognized")
logic.kind="ComputerMenuController";assert(M.resolve(false)==nil,"ordinary screen claimed");logic.kind="KeypadDeviceController"
zoom=false;assert(M.resolve(false)==nil,"world keypad claims locomotion");zoom=true
targetId=8;assert(M.resolve(false)==nil,"wrong device owner claimed");targetId=7
root.shown=false;assert(M.resolve(false)==nil,"hidden keypad claimed");root.shown=true
parent.shown=false;assert(M.resolve(false)==nil,"hidden ancestor ignored");parent.shown=true
menuOpen=true;assert(M.resolve(false)==nil,"pause menu claimed");menuOpen=false
assert(M.resolve(true)==nil,"VR overlay input stolen")
blocked=true;assert(M.resolve(false)==nil,"blocked interaction claimed");blocked=false
owner.cameraZoomActive=false;assert(M.resolve(false)==nil,"closed device claimed");owner.cameraZoomActive=true
playerExists=false;assert(M.resolve(false)==nil,"loading state claimed");playerExists=true
M.tick(.049,false);assert(calls==0,"heartbeat throttling failed")
M.tick(.001,false);assert(M.active and lastRoot==root and M.error==nil,"active heartbeat failed")
zoom=false;M.tick(.05,false);assert(not M.active and lastRoot==nil,"closing keypad retained input");zoom=true
M.tick(.05,false);M.shutdown();assert(not M.active and lastRoot==nil,"shutdown retained input")
M.tick(0,false);assert(M.active,"zero game delta stopped keypad heartbeat");M.shutdown()
print("PASS keypad scope: owner, controller, gaze independence, visibility, menus, shutdown")
