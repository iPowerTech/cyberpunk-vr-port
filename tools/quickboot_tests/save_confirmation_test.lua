local file=assert(io.open("mods/cet/CyberpunkVRPort_QuickBoot/init.lua","r"));local source=file:read("*a");file:close()
local body=assert(source:match("(local function saveConfirmation%(%)%s.-)%s+local function writeDetailedStatus"))
local probe=assert(load(body.."\nreturn saveConfirmation"))()
CName={new=function(v) return v end};NameToString=function(v) return v end
local function node(name,children,kind,visible)
  return {name=name,children=children or {},kind=kind,visible=visible~=false,
    GetName=function(s) return s.name end,IsVisible=function(s) return s.visible end,GetOpacity=function() return 1 end,
    IsA=function(s,t) return t==s.kind or t=="inkCompoundWidget" and #s.children>0 end,
    GetNumChildren=function(s) return #s.children end,GetWidgetByIndex=function(s,i) return s.children[i+1] end,
    GetController=function(s) if s.kind=="button" then return {IsA=function(_,t) return t=="inkButtonController" end} end end}
end
local title=node("ModLoadingTitle",nil,"inkTextWidget")
local confirm=node("buttonConfirm",nil,"button")
local cancel=node("buttonCancel",nil,"button")
local popup=node("popup",{title,confirm,cancel})
local root=node("window",{popup})
Game={GetInkSystem=function() return {GetLayer=function() return {GetVirtualWindow=function() return root end} end} end}
assert(probe().ready,"native mod warning should be recognized independently of localization")
title.name="DeleteSaveTitle";assert(not probe().ready,"unrelated confirmation must stay untouched");title.name="ModLoadingTitle"
confirm.visible=false;assert(not probe().ready,"hidden confirm button is not input-ready");confirm.visible=true
popup.visible=false;assert(not probe().ready,"a hidden ancestor blocks readiness");popup.visible=true
root.children={node("popup",{title}),node("popup",{confirm,cancel})}
assert(not probe().ready,"evidence from different popups must never be combined")
print("PASS: exact visible save-warning subtree, hidden controls and unrelated dialogs")
