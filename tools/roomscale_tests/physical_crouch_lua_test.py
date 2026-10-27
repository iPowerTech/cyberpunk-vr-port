"""Check effect ownership and idempotent CET requests without starting the game."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'build/lua-test-deps'))
from lupa import LuaRuntime
lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute('''
events={}; wanted=false; blocked=false; manual=false; record=true; effects={quest=true}; applies=0; removes=0
function registerForEvent(n,f) events[n]=f end
function GetVRPhysicalCrouch() return wanted and 1 or 0 end
function GetVRPhysicalCrouchCameraAnchor() return {x=0,y=0,z=1.6,w=1} end
function SetRootGraphFloatVariable() return 1 end
function SetVRPhysicalCrouchFeedback(a,b) feedbackApplied=a;feedbackActual=b end
function Observe(c,m,f) crouchEnter=f end
AnimationControllerComponent={SetInputVector=function(p,n,v)cameraAnchor=v end,SetInputFloat=function(p,n,v)cameraMode=v end}
TweakDBID={new=function(x)return x end};CName=TweakDBID
TweakDB={GetRecord=function() return record end}
spdlog={error=function(x)error(x)end}
player={GetEntityID=function()return {hash=42}end,GetPuppetPS=function()return {IsCrouch=function()return manual or effects['VRPort.PhysicalCrouch'] or false end}end}
system={HasStatusEffect=function(_,id,e)return effects[e] or false end,
 RemoveStatusEffect=function(_,id,e)effects[e]=nil;removes=removes+1 end}
Game={GetPlayer=function()return player end,GetStatusEffectSystem=function()return system end}
StatusEffectSystem={ObjectHasStatusEffectWithTag=function()return blocked end}
StatusEffectHelper={ApplyStatusEffect=function(p,e)effects[e]=true;applies=applies+1;crouchEnter({}, {}, {executionOwner=p}) end}
function tick()events.onUpdate(.06)end
''')
lua.execute((Path(__file__).resolve().parents[2]/'mods/cet/CyberpunkVRPort_PhysicalCrouch/init.lua').read_text())
lua.execute('''
events.onInit();tick();assert(applies==0)
wanted=true;for i=1,20 do tick() end;assert(applies==1 and effects['VRPort.PhysicalCrouch'])
assert(cameraMode==1 and cameraAnchor.z==1.6)
manual=true;wanted=false;tick();assert(manual and not effects['VRPort.PhysicalCrouch'] and effects.quest)
assert(cameraMode==1);crouchEnter({}, {}, {executionOwner=player});assert(cameraMode==0)
wanted=true;tick();blocked=true;tick();assert(not effects['VRPort.PhysicalCrouch'] and feedbackApplied==-1)
blocked=false;tick();assert(effects['VRPort.PhysicalCrouch']);record=false;tick();assert(not effects['VRPort.PhysicalCrouch'])
record=true;tick();assert(effects['VRPort.PhysicalCrouch']);events.onShutdown();assert(not effects['VRPort.PhysicalCrouch'] and effects.quest)
''')
print('CET ownership, held request, manual crouch, native restriction, missing record and shutdown passed')
