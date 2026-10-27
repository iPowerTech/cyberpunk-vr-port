package.preload["modules/gameplay_data"]=function() return {} end
local I=dofile("mods/cet/CyberpunkVRPort_Menu/modules/inventory_data.lua")
local function raw(key,quantity,preview)
  return {id={key=key,preview=preview},quantity=quantity,GetID=function(s) return s.id end,GetQuantity=function(s) return s.quantity end}
end
local equipped={key="weapon"}
local visible={weapon=true,shirt=true}
local items={raw("weapon",1),raw("shirt",1),raw("helper",1)}
ItemID={GetCombinedHash=function(id) return id.key end,HasFlag=function(id) return id.preview end}
gameEItemIDFlag={Preview=1};CName={new=function(v) return v end}
gamedataEquipmentArea={Weapon="Weapon",Head="Head",Face="Face",OuterChest="OuterChest",InnerChest="InnerChest",Legs="Legs",Feet="Feet",Outfit="Outfit"}
EquipmentSystem={GetData=function() return {GetItemInEquipSlot=function() return equipped end} end}
Game={GetPlayer=function() return {} end,
  GetTransactionSystem=function() return {GetItemList=function() return true,items end} end,
  GetScriptableSystemsContainer=function() return {Get=function() return {GetPlayerItem=function(_,id) return visible[id.key] and {} or nil end} end} end}
local ctx={};local base=I.signature(ctx)
items[#items+1]=raw("another-helper",1)
items[#items+1]=raw("shirt",100,true)
assert(I.signature(ctx)==base,"hidden helpers and preview items must not redraw the inventory")
items[1],items[2]=items[2],items[1]
assert(I.signature(ctx)==base,"transaction iteration order must not redraw")
items[1].quantity=2
assert(I.signature(ctx)~=base,"visible quantity changes must redraw")
items[1].quantity=1;equipped={key="different-slot-item"}
assert(I.signature(ctx)~=base,"equipment changes must redraw")
print("PASS: hidden helpers ignored; visible inventory/equipment changes retained")
