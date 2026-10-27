local I=require("modules/inventory_data")
local Sort=require("modules/item_sort")
local D={open=require("modules/trade_data").open,close=I.close}
local function transferable(raw)
  local kind=RPGManager.GetItemType(raw:GetID())
  return kind~=gamedataItemType.Con_Inhaler and kind~=gamedataItemType.Con_Injector and kind~=gamedataItemType.Gad_Grenade
end
local function playerItem(ctx,raw)
  -- Query the native map by ID: CET cannot round-trip inkHashMap.GetValues' script_ref array.
  -- This keeps the same inventory/cyberware/quest visibility rules as the fullscreen stash.
  return ctx.inventory:GetPlayerItem(raw:GetID())
end
local function visible(ctx,raw,side)
  if not raw or ItemID.HasFlag(raw:GetID(),gameEItemIDFlag.Preview) then return false end
  if side=="deposit" then return playerItem(ctx,raw)~=nil and not raw:HasAnyOfTags(UIInventoryItemsManager.GetStashBlacklistedTags()) end
  return not InventoryDataManagerV2.IsItemBlacklisted(raw)
end
local function item(ctx,visit,raw,side)
  if not visible(ctx,raw,side) then return nil end
  local ui=side=="deposit" and playerItem(ctx,raw) or UIInventoryItem.Make(visit.object,raw,ctx.inventory:GetInventoryItemsManager())
  local v=I.item(ctx,ctx.manager:GetInventoryItemData(raw));if not v then return nil end
  v.ui=ui;v.raw=raw;v.side=side;v.key=side..":"..tostring(ItemID.GetCombinedHash(v.id));v.quantity=raw:GetQuantity()
  v.equipped=side=="deposit" and ui:IsEquipped();v.category=tostring(ui:GetFilterCategory())
  v.canTransfer=transferable(raw);v.qualityValue=InventoryItemData.GetQualityF(v.data)
  v.weight=raw:GetStatValueByType(gamedataStatType.Weight);v.slots=I.slots(ctx,v)
  v.comparison=ctx.compare:IsComparable(ui) and ctx.compare:GetItemComparisonState(ui) or ItemComparisonState.Default
  return v
end
function D.snapshot(ctx,visit)
  local out={deposit={},withdraw={},byKey={}}
  ctx.inventory:FlushTempData()
  local _,items=Game.GetTransactionSystem():GetItemList(Game.GetPlayer())
  for _,side in ipairs({"deposit","withdraw"}) do
    for _,raw in ipairs(side=="deposit" and items or visit.manager:GetStorageItems()) do
      if side=="withdraw" and visible(ctx,raw,side) then raw:ReinitializePlayerStats(Game.GetPlayer():GetEntityID()) end
      local v=item(ctx,visit,raw,side)
      if v then out[side][#out[side]+1]=v;out.byKey[v.key]=v end
    end
  end
  return out
end
function D.sorted(items,filter,order)
  local out={}
  for _,v in ipairs(items) do if filter=="all" or v.category==filter then out[#out+1]=v end end
  return Sort.apply(out,order)
end
function D.quote(ctx,visit,v,quantity)
  if not visit or not v then return {allowed=false,max=0,reason="closed"} end
  local p=Game.GetPlayer();local source=v.side=="deposit" and p or visit.object
  local raw=Game.GetTransactionSystem():GetItemData(source,v.id)
  if not visible(ctx,raw,v.side) or not transferable(raw) then return {allowed=false,max=0,reason="protected"} end
  local count=math.min(raw:GetQuantity(),Game.GetTransactionSystem():GetItemQuantity(source,v.id))
  local amount=math.max(1,math.floor(tonumber(quantity) or 1))
  local equipped=v.side=="deposit" and EquipmentSystem.GetInstance(p):IsEquipped(p,v.id)
  if equipped and raw:HasTag(CName.new("UnequipBlocked")) then return {allowed=false,max=count,reason="protected"} end
  return {allowed=count>=amount,max=count,quantity=amount,confirm=equipped,raw=raw,source=source,target=v.side=="deposit" and visit.object or p}
end
function D.transact(ctx,visit,v,quantity,confirmed)
  local q=D.quote(ctx,visit,v,quantity)
  if not q.allowed then return false end
  if q.confirm and not confirmed then return "confirm" end
  -- Native transfer preserves the complete ItemID, seed, attachments and item state.
  visit.manager:TransferItem(q.source,q.target,q.raw,q.quantity)
  return true
end
function D.signature(ctx,visit)
  -- Weight alone misses transfers of zero-weight items and stack quantity changes.
  local ts=Game.GetTransactionSystem();local values={}
  for index,owner in ipairs({Game.GetPlayer(),visit.object}) do
    local _,items=ts:GetItemList(owner);local side={}
    for _,raw in ipairs(items) do
      -- Invisible helper entities (e.g. the VR belt) can change while a world menu is open.
      -- They must not invalidate the visible list or dismiss an item confirmation.
      if visible(ctx,raw,index==1 and "deposit" or "withdraw") then
        side[#side+1]=tostring(ItemID.GetCombinedHash(raw:GetID()))..":"..raw:GetQuantity()
      end
    end
    table.sort(side);values[#values+1]=table.concat(side,"|")
  end
  return table.concat(values,"\n")
end
return D
