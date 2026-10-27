-- EquipmentEx's outfit system, read and driven from the VR menu. EquipmentEx is optional: without it every
-- system lookup is nil and the menu shows no wardrobe at all.
local I=require("modules/inventory_data")
local W={}
local function system(name)
  local ok,s=pcall(function() return Game.GetScriptableSystemsContainer():Get(CName.new(name)) end)
  if ok and s~=nil then return s end
end
local function outfits() return system("EquipmentEx.OutfitSystem") end
local function text(key) return GetLocalizedTextByKey(CName.new(key)) end
W.text=text
function W.available() return outfits()~=nil end
function W.blocked() local s=outfits();return s==nil or s:IsBlocked() end

-- The saved outfits in EquipmentEx's order; `active` is false while the game's own equipment shows.
function W.outfits()
  local s=outfits();if not s then return {active=false,list={}} end
  local list={}
  for _,name in ipairs(s:GetOutfits()) do list[#list+1]={name=name,label=NameToString(name),equipped=s:IsEquipped(name)} end
  return {active=s:IsActive(),list=list}
end
function W.load(name) local s=outfits();return s~=nil and s:LoadOutfit(name) end
function W.deactivate() local s=outfits();if s and s:IsActive() then s:Deactivate() end;return s~=nil end
function W.delete(name) local s=outfits();return s~=nil and s:DeleteOutfit(name) end
-- No keyboard in the headset: a new outfit is named "<Outfit> N", the first N free.
function W.save()
  local s=outfits();if not s or not s:IsActive() then return false end
  local base=text("UI-Inventory-Labels-Outfit");if base=="" then base="Outfit" end
  -- StringToName, not CName.new: it puts the name in the name pool, so NameToString reads it back later
  local n=1
  while s:HasOutfit(StringToName(base.." "..n)) do n=n+1 end
  return s:SaveOutfit(StringToName(base.." "..n),false)
end

-- Where the items come from: EquipmentEx's WardrobeItemSource, cycled in the order its own selector shows.
local SOURCES={{1,"UI-EquipmentEx-WardrobeItemSource-InventoryAndStash"},{2,"UI-EquipmentEx-WardrobeItemSource-InventoryOnly"},
  {0,"UI-EquipmentEx-WardrobeItemSource-WardrobeStore"}}
local function sourceIndex(view)
  local value=tonumber(EnumInt(view:GetItemSource()))
  for i,src in ipairs(SOURCES) do if src[1]==value then return i end end
  return 1
end
function W.source()
  local v=system("EquipmentEx.ViewManager");if not v then return "" end
  return text(SOURCES[sourceIndex(v)][2])
end
function W.nextSource()
  local v=system("EquipmentEx.ViewManager");if not v then return false end
  local nxt=SOURCES[sourceIndex(v)%#SOURCES+1]
  v:SetItemSource(Enum.new("EquipmentEx.WardrobeItemSource",nxt[1]))
  return true
end

local function slotKey(slot) return tostring(slot.hash)..":"..tostring(slot.length) end
-- The clothing EquipmentEx offers, grouped by its outfit slots in its slot order: { slot, key, name,
-- collapsed, equipped (item or nil), items }. Items are inventory_data items with `equipped` meaning worn
-- in the outfit.
function W.groups(ctx)
  local s,h,v=outfits(),system("EquipmentEx.InventoryHelper"),system("EquipmentEx.ViewManager")
  if not s or not h or not v then return {} end
  local byKey,order={},{}
  for _,slot in ipairs(s:GetOutfitSlots()) do
    local g={slot=slot,key=slotKey(slot),name=s:GetSlotName(slot),collapsed=v:IsCollapsed(slot),items={}}
    byKey[g.key]=g;order[#order+1]=g
  end
  for _,data in ipairs(h:GetAvailableItems({})) do
    local item=I.item(ctx,data)
    if item then
      local g=byKey[slotKey(s:GetItemSlot(item.id))]
      if g then
        item.equipped=s:IsEquipped(item.id);item.key=item.key..":"..g.key
        g.items[#g.items+1]=item
        if item.equipped then g.equipped=item end
      end
    end
  end
  local out={}
  for _,g in ipairs(order) do
    if #g.items>0 then
      table.sort(g.items,function(a,b) if a.name~=b.name then return a.name<b.name end return a.key<b.key end)
      out[#out+1]=g
    end
  end
  return out
end
function W.toggleSlot(group) local v=system("EquipmentEx.ViewManager");if v then v:ToggleCollapsed(group.slot) end end
-- Wears an item in the outfit, or takes it off when it is worn.
function W.toggle(item)
  local s=outfits();if not s then return false end
  if s:IsEquipped(item.id) then return s:UnequipItem(item.id) end
  return s:EquipItem(item.id)
end
return W
