-- Inject production source at -- OVERLAY_MODULE. All game objects are lexical mocks.
local command,wanted,rootVisible,paused=0,false,false,false
local opened,closed,activated=0,0,{}
local data={
 {label="Resume",eventName="OnClosePauseMenu",action="PauseMenuAction : OpenSubMenu (0)"},
 {label="Save",eventName="None",action="PauseMenuAction : Save (2)"},
 {label="Load",eventName="OnSwitchToLoadGame",action="PauseMenuAction : OpenSubMenu (0)"},
 {label="Settings",eventName="OnSwitchToSettings",action="PauseMenuAction : OpenSubMenu (0)"},
 {label="Credits",eventName="OnCreditsPicker",action="PauseMenuAction : OpenSubMenu (0)"},
 {label="Main menu",eventName="None",action="PauseMenuAction : ExitToMainMenu (4)"},
 {label="Exit",eventName="None",action="PauseMenuAction : ExitGame (3)"}
}
local items={};for i=1,#data do local d=data[i];items[i]={GetData=function()return d end} end
local list={GetNumChildren=function()return #items end,GetWidgetByIndex=function(_,i)return {GetController=function()return items[i+1] end}end}
local root={GetWidgetByPathName=function()return list end}
local pause={GetClassName=function()return "PauseMenuGameController" end,GetRootWidget=function()return root end,
    OnMenuItemActivated=function(_,i,item)assert(item==items[i+1]);activated[#activated+1]=i end}
local ingame={GetClassName=function()return "gameuiInGameMenuGameController" end,
    SpawnMenuInstanceEvent=function(_,event)
        if event=="OnOpenPauseMenu" then opened=opened+1;rootVisible=true;paused=true
        elseif event=="OnClosePauseMenu" then closed=closed+1;rootVisible=false;paused=false
        else error("unexpected native event") end
    end}
local Game={
    GetInkSystem=function()return {GetLayer=function()return {GetGameControllers=function()return rootVisible and {ingame,pause} or {ingame}end}end}end,
    GetSystemRequestsHandler=function()return {
        IsGamePaused=function()return paused end,RequestSavesCountSync=function()return 2 end,
        PauseGame=function()error("overlay must never pause the game")end,
        UnpauseGame=function()error("overlay must not unpause another owner")end}end,
    VROverlayCommand=function()local c=command;command=0;return c end,
    VROverlayMenu=function(ready,available,labels)return wanted and ready and available and #labels>0 end
}
local CName={new=function(v)return v end}
local NameToString=function(v)return v end
local GetLocalizedText=function(v)return v end
local module=(function()
-- OVERLAY_MODULE
end)()
command=1;wanted=true;module.tick()
assert(module.active and not paused and opened==0,"ordinary overlay entered native pause")
command=2;wanted=false;module.tick()
assert(not module.active and not paused and opened==0 and closed==0,"ordinary close changed game pause")
rootVisible=true;paused=true;command=1;wanted=true;module.tick();module.tick()
assert(module.active and not paused and not rootVisible and closed==1,"opening from ESC retained native presentation")
command=103;wanted=false;module.tick();module.tick()
assert(opened==1 and paused and activated[1]==3,"Settings did not hand off to the native handler")
rootVisible=false;paused=false;command=1;wanted=true;module.tick()
command=100;wanted=false;module.tick()
assert(not module.active and not paused and closed==1,"Resume must only close the overlay")
command=1;wanted=true;module.tick();module.shutdown()
assert(not module.active and not paused,"shutdown changed pause state")
print("PASS unpaused overlay: ordinary open/close, ESC exit, native Settings handoff, Resume, shutdown")
