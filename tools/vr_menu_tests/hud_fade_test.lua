local H=dofile("mods/cet/CyberpunkVRPort_Menu/modules/hud_fade.lua")
local root={opacity=.8,GetOpacity=function(s) return s.opacity end,SetOpacity=function(s,v) s.opacity=v end}
CName={new=function(v) return v end}
Game={GetInkSystem=function() return {GetLayer=function() return {GetVirtualWindow=function() return root end} end} end}
H.update(false,.02);assert(root.opacity==.8,"closed menus must not override HUD opacity")
H.update(true,.07);assert(math.abs(root.opacity-.4)<.001,"halfway fade uses the original opacity")
H.update(false,0);assert(math.abs(root.opacity-.4)<.001,"reversing a fade must not jump")
H.update(false,.09);assert(root.opacity>.4 and root.opacity<.8)
H.update(false,.09);assert(math.abs(root.opacity-.8)<.001 and not H.root,"restore and release on close")
H.update(true,.1);H.update(true,.04);assert(root.opacity<.001)
H.restore();assert(root.opacity==.8 and not H.root,"shutdown/hot reload must restore the original HUD")
print("PASS: smooth HUD fade, reversal, original-opacity restoration and shutdown")
