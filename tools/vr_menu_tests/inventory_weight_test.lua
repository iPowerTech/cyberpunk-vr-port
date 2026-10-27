package.preload["modules/gameplay_data"]=function() return {} end
local I=dofile("mods/cet/CyberpunkVRPort_Menu/modules/inventory_data.lua")
local boardWeight,calculations=0,0
local player={curInventoryWeight=0,
  CalculateEncumbrance=function(s) calculations=calculations+1;s.curInventoryWeight=s.curInventoryWeight+168.8 end,
  EvaluateEncumbrance=function(s) boardWeight=s.curInventoryWeight end}
Game={GetPlayer=function() return player end}
assert(I.refreshWeight() and math.abs(player.curInventoryWeight-168.8)<.001 and boardWeight==player.curInventoryWeight)
assert(I.refreshWeight() and math.abs(player.curInventoryWeight-168.8)<.001,"reopening must not double-count the additive native calculation")
player.CalculateEncumbrance=function() error("unavailable") end
assert(not I.refreshWeight() and math.abs(player.curInventoryWeight-168.8)<.001,"failed calculation must preserve the previous value")
assert(calculations==2)
print("PASS: native weight refresh, no double counting, UI publication and failure preservation")
