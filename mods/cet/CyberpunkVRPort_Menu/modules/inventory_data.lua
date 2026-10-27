-- InventoryDataManagerV2 is the game's equipment/attachment adapter. Keep its full ItemIDs:
-- two copies of a weapon record can have different tiers, modifiers and installed parts.
local Images=require("modules/gameplay_data")
local I={}
I.clothes={"Head","Face","OuterChest","InnerChest","Legs","Feet","Outfit"}
local function loc(value)
  local text=tostring(value or "")
  if text=="" then return "" end
  local result=GetLocalizedText(text)
  return result~="" and result or text
end
I.loc=loc
function I.open()
  local m=InventoryDataManagerV2.new();m:Initialize(Game.GetPlayer())
  return {manager=m,cache={},comparison=ItemPreferredComparisonResolver.Make(m),ui=Game.GetScriptableSystemsContainer():Get(CName.new("UIScriptableSystem"))}
end
function I.close(ctx) if ctx then ctx.manager:UnInitialize() end end
function I.invalidate(ctx) ctx.cache={};ctx.equippedCache={};ctx.comparison=ItemPreferredComparisonResolver.Make(ctx.manager);ctx.manager:MarkToRebuild();ctx.manager:ClearInventoryItemDataCache() end
function I.key(id) return tostring(ItemID.GetCombinedHash(id)) end
function I.item(ctx,data)
  if InventoryItemData.IsEmpty(data) then return nil end
  local id=InventoryItemData.GetID(data)
  local icon=InventoryItemData.GetIconPath(data)
  if icon=="" then
    icon=NameToString(IconsNameResolver.GetIconsNameResolver():TranslateItemToIconName(ItemID.GetTDBID(id),InventoryItemData.GetIconGender(data)==ItemIconGender.Male))
  end
  local raw=InventoryItemData.GetGameItemData(data)
  local kind=InventoryItemData.GetItemType(data)
  return {id=id,key=I.key(id),data=data,name=loc(InventoryItemData.GetName(data)),
    description=loc(InventoryItemData.GetDescription(data)),quality=NameToString(InventoryItemData.GetQuality(data)),
    image=icon~="" and icon~="None" and Images.image(TweakDBID.new("UIIcon."..icon)) or nil,
    type=kind,area=InventoryItemData.GetEquipmentArea(data),equipped=InventoryItemData.IsEquipped(data),
    typeName=loc(InventoryItemData.GetLocalizedItemType(data)),
    typeIcon=Images.image(TweakDBID.new(NameToString(UIItemsHelper.GetWeaponTypeIcon(kind)))),
    new=ctx.ui:IsInventoryItemNew(id),
    allowed=InventoryGPRestrictionHelper.CanEquip(data,Game.GetPlayer()),
    iconic=raw and raw:HasTag(CName.new("IconicWeapon")) or false,
    melee=raw and raw:HasTag(CName.new("MeleeWeapon")) or false,
    blocked=raw and raw:HasTag(CName.new("UnequipBlocked")) or false,
    dps=InventoryItemData.GetDPS(data),ammo=InventoryItemData.GetAmmo(data),
    quantity=InventoryItemData.GetQuantity(data),mod=RPGManager.IsWeaponMod(kind),
    stats=InventoryItemData.GetPrimaryStats(data)}
end
function I.inspect(ctx,item)
  local request=UIScriptableSystemInventoryInspectItem.new();request.itemID=item.id
  ctx.ui:QueueRequest(request);item.new=false
end
function I.compare(ctx,item,baseline)
  if baseline then ctx.comparison:ForceComparedItem(baseline.data) else ctx.comparison:DisableForceComparedItem() end
  return ctx.comparison:GetItemComparisonState(item.data)
end
function I.refreshWeight()
  local player=Game.GetPlayer();if not player then return false,"player unavailable" end
  local previous=player.curInventoryWeight
  -- The incremental gameplay counter can be stale after preview-item activity.
  -- Use the same native calculation as player initialization (including stack
  -- quantities and Preview exclusions). It adds to the counter, so reset first.
  local ok,err=pcall(function()
    player.curInventoryWeight=0
    player:CalculateEncumbrance()
    player:EvaluateEncumbrance()
  end)
  if not ok then player.curInventoryWeight=previous;return false,err end
  return true
end
function I.resources()
  local p=Game.GetPlayer();if not p then return nil end
  local bb=Game.GetBlackboardSystem():Get(GetAllBlackboardDefs().UI_PlayerStats)
  return {money=Game.GetTransactionSystem():GetItemQuantity(p,ItemID.FromTDBID(TweakDBID.new("Items.money"))),
    weight=math.floor(bb:GetFloat(GetAllBlackboardDefs().UI_PlayerStats.currentInventoryWeight)),
    capacity=math.floor(Game.GetStatsSystem():GetStatValue(p:GetEntityID(),gamedataStatType.CarryCapacity))}
end
-- An equipped item's InventoryItemData conversion is ~0.5 ms; ten slots are drawn by every click, so
-- they are kept until I.invalidate (a loadout change seen by the signature, or the menu's own action).
function I.equipped(ctx,area,slot)
  ctx.equippedCache=ctx.equippedCache or {}
  local key=area..":"..tostring(slot or 0)
  local hit=ctx.equippedCache[key]
  if hit~=nil then return hit or nil end
  local item=I.item(ctx,ctx.manager:GetItemDataEquippedInArea(gamedataEquipmentArea[area],slot or 0))
  ctx.equippedCache[key]=item or false
  return item
end
function I.slots(ctx,item)
  local out,seen={},{}
  if not item then return out end
  for _,part in ipairs(InventoryItemData.GetAttachments(item.data)) do
    local key=tostring(part.SlotID)
    if not seen[key] then
      seen[key]=true
      local installed=I.item(ctx,part.ItemData)
      out[#out+1]={id=part.SlotID,key=key,name=loc(part.SlotName),item=installed,
        removable=installed and RPGManager.CanPartBeUnequipped(part.ItemData,part.SlotID) or false}
    end
  end
  return out
end
-- The game's own dual wielding: a left-hand gun beside the FIRST weapon slot only (Weapon slot 3, the
-- partner of slot 0) once the Cool perk Cool_Central_Milestone_2 is bought -- the game's equipment screen
-- offers no other. Only pistols the game lets dual wield may go there (gameItemData.CanDualWield, the
-- DualWield tag), and only beside another one.
I.DUAL_SLOT=0
function I.dualWield()
  local ok,owned=pcall(function()
    return PlayerDevelopmentSystem.GetData(Game.GetPlayer()):IsNewPerkBoughtAnyLevel(gamedataNewPerkType.Cool_Central_Milestone_2)
  end)
  return ok and owned==true
end
-- Left-hand guns beside slots 2 and 3, which an earlier version of this menu offered and the game never
-- does: taken off, or they stay equipped where no screen shows them. True when something was removed.
function I.clearStrayLeft(ctx)
  local removed=false
  for slot=I.DUAL_SLOT+4,5 do
    if I.equipped(ctx,"Weapon",slot) then
      pcall(function() ctx.manager:UnequipItem(gamedataEquipmentArea.Weapon,slot,false) end)
      removed=true
    end
  end
  if removed then I.invalidate(ctx) end
  return removed
end
function I.canDual(item)
  if not item then return false end
  local ok,can=pcall(function() return InventoryItemData.GetGameItemData(item.data):CanDualWield(Game.GetPlayer()) end)
  return ok and can==true
end
local rank={Legendary=5,Epic=4,Rare=3,Uncommon=2,Common=1}
local function sort(items)
  table.sort(items,function(a,b)
    if a.equipped~=b.equipped then return a.equipped end
    if a.quality~=b.quality then return (rank[a.quality] or 0)>(rank[b.quality] or 0) end
    if a.name~=b.name then return a.name<b.name end
    return a.key<b.key
  end)
  return items
end
function I.items(ctx,area)
  if ctx.cache[area] then return ctx.cache[area] end
  local out={}
  for _,data in ipairs(ctx.manager:GetPlayerInventoryData(gamedataEquipmentArea[area],false,{})) do
    local item=I.item(ctx,data);if item then out[#out+1]=item end
  end
  ctx.cache[area]=sort(out);return out
end
function I.parts(ctx,item,slot)
  local out={}
  if not item or not slot then return out end
  for _,data in ipairs(ctx.manager:GetPlayerInventoryPartsForItem(item.id,slot.id)) do
    local part=I.item(ctx,data)
    if part then
      part.allowed=ctx.manager:CanInstallPart(data)
      if part.mod and slot.item and not item.iconic then part.allowed=false end
      out[#out+1]=part
    end
  end
  return sort(out)
end
local function owned(id) return Game.GetTransactionSystem():GetItemData(Game.GetPlayer(),id)~=nil end
function I.canChange(ctx,area,slot)
  local item=I.equipped(ctx,area,slot)
  if item and (item.blocked or not item.allowed) then return false end
  if area~="Weapon" and area~="Outfit" and I.equipped(ctx,"Outfit",0) then return false end
  return true
end
function I.equip(ctx,area,slot,id)
  if not owned(id) or not I.canChange(ctx,area,slot) then return false end
  local item=I.item(ctx,ctx.manager:GetItemDataFromIDInLoadout(id))
  if not item or item.area~=gamedataEquipmentArea[area] or not item.allowed then return false end
  -- The fullscreen adapter forces the weapon into the hand. A world inventory must only change
  -- the loadout; drawing it would correctly trip the menu's existing armed-player close gate.
  local request=GameplayEquipRequest.new();request.owner=Game.GetPlayer();request.itemID=id
  request.slotIndex=slot or 0;request.forceEquipWeapon=false;request.blockUpdateWeaponActiveSlots=true
  Game.GetScriptableSystemsContainer():Get(CName.new("EquipmentSystem")):QueueRequest(request)
  return true
end
function I.unequip(ctx,area,slot)
  if not I.equipped(ctx,area,slot) or not I.canChange(ctx,area,slot) then return false end
  ctx.manager:UnequipItem(gamedataEquipmentArea[area],slot or 0,false);return true
end
function I.install(ctx,weaponID,slotID,partID,confirmed)
  if not owned(weaponID) or not owned(partID) then return false end
  -- Re-read compatibility and the occupied slot at click time, including tier restrictions.
  I.invalidate(ctx)
  local item=I.item(ctx,ctx.manager:GetItemDataFromIDInLoadout(weaponID))
  if not item or not item.allowed or item.blocked then return false end
  for _,slot in ipairs(I.slots(ctx,item)) do if slot.id==slotID then
    for _,part in ipairs(I.parts(ctx,item,slot)) do if part.id==partID and part.allowed then
      if part.mod and slot.item and not confirmed then return "confirm" end
      ctx.manager:InstallPart(weaponID,partID,slotID);return true
    end end
  end end
  return false
end
function I.remove(ctx,weaponID,slotID)
  if not owned(weaponID) then return false end
  I.invalidate(ctx)
  local item=I.item(ctx,ctx.manager:GetItemDataFromIDInLoadout(weaponID))
  if not item or not item.allowed or item.blocked then return false end
  for _,slot in ipairs(I.slots(ctx,item)) do if slot.id==slotID and slot.removable then
    local request=RemoveItemPart.new();request.obj=Game.GetPlayer();request.baseItem=weaponID;request.slotToEmpty=slotID
    Game.GetScriptableSystemsContainer():Get(CName.new("ItemModificationSystem")):QueueRequest(request)
    return true
  end end
  return false
end
function I.signature(ctx)
  local data=EquipmentSystem.GetData(Game.GetPlayer());local values={}
  for slot=0,5 do values[#values+1]=I.key(data:GetItemInEquipSlot(gamedataEquipmentArea.Weapon,slot)) end
  for _,area in ipairs(I.clothes) do values[#values+1]=I.key(data:GetItemInEquipSlot(gamedataEquipmentArea[area],0)) end
  local _,items=Game.GetTransactionSystem():GetItemList(Game.GetPlayer())
  -- The VR belt creates hidden helper items while the menu is open. Counting
  -- every transaction item rebuilt the whole panel once per second. Track
  -- only the native UI inventory, with stable IDs independent of struct padding.
  ctx.visibleInventory=ctx.visibleInventory or Game.GetScriptableSystemsContainer():Get(CName.new("UIInventoryScriptableSystem"))
  local visible={}
  for _,raw in ipairs(items) do
    local id=raw:GetID()
    if not ItemID.HasFlag(id,gameEItemIDFlag.Preview) and ctx.visibleInventory:GetPlayerItem(id)~=nil then
      visible[#visible+1]=I.key(id)..":"..tostring(raw:GetQuantity())
    end
  end
  table.sort(visible)
  values[#values+1]=table.concat(visible,",")
  return table.concat(values,"|")
end
-- I.signature as a job producer (modules/jobs.lua: returns a step function): 700+ backpack items cost
-- ~10 ms read at once, so eighty a step. Done with true and the stamp.
function I.signatureJob(ctx)
  local values,items,visible,i
  return function()
    if not items then
      local data=EquipmentSystem.GetData(Game.GetPlayer());values={}
      for slot=0,5 do values[#values+1]=I.key(data:GetItemInEquipSlot(gamedataEquipmentArea.Weapon,slot)) end
      for _,area in ipairs(I.clothes) do values[#values+1]=I.key(data:GetItemInEquipSlot(gamedataEquipmentArea[area],0)) end
      local _;_,items=Game.GetTransactionSystem():GetItemList(Game.GetPlayer())
      items=items or {};visible={};i=0
      ctx.visibleInventory=ctx.visibleInventory or Game.GetScriptableSystemsContainer():Get(CName.new("UIInventoryScriptableSystem"))
      return false
    end
    local last=math.min(#items,i+80)
    while i<last do
      i=i+1
      local raw=items[i];local id=raw:GetID()
      if not ItemID.HasFlag(id,gameEItemIDFlag.Preview) and ctx.visibleInventory:GetPlayerItem(id)~=nil then
        visible[#visible+1]=I.key(id)..":"..tostring(raw:GetQuantity())
      end
    end
    if i<#items then return false end
    table.sort(visible)
    values[#values+1]=table.concat(visible,",")
    return true,true,table.concat(values,"|")
  end
end

function I.wardrobe()
  local set=EquipmentSystem.GetActiveWardrobeSet(Game.GetPlayer())
  return set and not ClothingSet.IsEmpty(set) and set or nil
end
function I.clearWardrobe(ctx)
  if not I.wardrobe() or not ctx.manager:IsWardrobeEnabled() then return false end
  local request=UnequipWardrobeSetRequest.new();request.owner=Game.GetPlayer()
  Game.GetScriptableSystemsContainer():Get(CName.new("EquipmentSystem")):QueueRequest(request)
  return true
end
return I
