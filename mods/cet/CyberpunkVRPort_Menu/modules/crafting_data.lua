-- Crafting and upgrading, the game's own way (UI/fullscreen/crafting: craftingPanelController.swift,
-- upgradePanelController.swift, craftingItemTemplateClassifier.swift): the craft book's recipes, the iconic
-- weapons that can still be upgraded, what each costs, and the requests that do it.
local I=require("modules/inventory_data")
local Images=require("modules/gameplay_data")
local D={}
D.CRAFT_FILTERS={"AllItems","RangedWeapons","MeleeWeapons","Consumables","Grenades","Attachments","Programs"}
D.UPGRADE_FILTERS={"AllItems","RangedWeapons","MeleeWeapons"}
local rank={Legendary=5,Epic=4,Rare=3,Uncommon=2,Common=1}
local function system() return Game.GetScriptableSystemsContainer():Get(CName.new("CraftingSystem")) end
local function player() return Game.GetPlayer() end
local function tag(rec,name) return rec:TagsContains(CName.new(name)) end
-- "LegendaryPlus" draws as "Legendary"
local function baseQuality(name) return (tostring(name or ""):gsub("Plus","")) end
D.baseQuality=baseQuality
local function image(tdb,rec)
  local icon=UIInventoryItemsManager.ResolveItemIconName(tdb,rec,false)
  return icon and icon~="" and icon~="None" and Images.image(TweakDBID.new(icon)) or nil
end
function D.ingredients(list)
  local out={}
  for _,g in ipairs(list or {}) do
    local rec=g.id
    if rec then
      out[#out+1]={name=GetLocalizedItemNameByCName(rec:DisplayName()),image=image(rec:GetID(),rec),need=g.quantity,have=g.inventoryQuantity}
    end
  end
  return out
end
-- craftingItemTemplateClassifier.swift FilterItem
function D.matches(v,filter)
  local rec=v.rec
  if filter=="AllItems" then return true end
  if filter=="RangedWeapons" then return tag(rec,"RangedWeapon") end
  if filter=="MeleeWeapons" then return tag(rec,"MeleeWeapon") end
  if filter=="Consumables" then return tag(rec,"Consumable") or tag(rec,"Ammo") end
  if filter=="Grenades" then return tag(rec,"Grenade") end
  if filter=="Attachments" then return tag(rec,"itemPart") and not tag(rec,"Fragment") and not tag(rec,"SoftwareShard") end
  if filter=="Programs" then return tag(rec,"SoftwareShard") or tag(rec,"QuickhackCraftingPart") end
  return true
end
local function order(a,b)
  local qa,qb=rank[a.quality] or 0,rank[b.quality] or 0
  if qa~=qb then return qa>qb end
  if a.name~=b.name then return a.name<b.name end
  return a.key<b.key
end
-- Every recipe the craft book offers (craftingPanelController.swift GetRecipesList).
function D.recipes()
  local cs=system();local book=cs:GetPlayerCraftBook();local out={}
  book:HideRecipesForOwnedItems()
  for _,rec in ipairs(book:GetCraftableItems()) do
    local rd=cs:GetRecipeData(rec)
    if rd and rd.label~="" then
      local tdb=rec:GetID()
      out[#out+1]={rec=rec,id=tdb,key=TDBID.ToStringDEBUG(tdb),name=rd.label,amount=rd.amount,
        quality=baseQuality(NameToString(UIItemsHelper.QualityEnumToName(rec:Quality():Type()))),
        craftable=cs:CanItemBeCrafted(rec),new=book:IsRecipeNew(tdb),image=image(tdb,rec),
        weapon=tag(rec,"Weapon") or tag(rec,"RangedWeapon") or tag(rec,"MeleeWeapon"),ammo=tag(rec,"Ammo")}
    end
  end
  table.sort(out,order)
  return out
end
-- The item a recipe makes, made for show only (craftingPanelController.swift DryMakeItem): what its tooltip
-- describes and what the crafting system prices.
function D.dry(v)
  local p=player()
  local params=ItemModParams.new();params.itemID=ItemID.FromTDBID(v.id);params.quantity=1
  local data=Inventory.CreateItemData(params,p)
  CraftingSystem.SetItemLevel(p,data);CraftingSystem.MarkItemAsCrafted(p,data)
  data:SetDynamicTag(CName.new("SkipActivityLog"))
  local manager=Game.GetScriptableSystemsContainer():Get(CName.new("UIInventoryScriptableSystem")):GetInventoryItemsManager()
  return {data=data,ui=UIInventoryItem.Make(p,data,manager)}
end
-- Whether it can be crafted now, how many at most, what it costs, and the game's reason when it cannot
-- (craftingPanelController.swift SetNotification).
function D.craftQuote(v,dry)
  local cs=system();local data=dry.data
  local q={can=cs:CanItemBeCrafted(data),max=math.max(0,cs:GetMaxCraftingAmount(data)),ingredients=D.ingredients(cs:GetItemCraftingCost(data))}
  if dry.ui:GetItemType()==gamedataItemType.Prt_Program then q.can=false;q.reason="LocKey#78498"
  elseif not cs:EnoughIngredientsForCrafting(data) then q.reason="LocKey#42797"
  elseif q.max==0 then q.reason="LocKey#81496" end
  if q.reason then q.can=false end
  return q
end
function D.craft(v,amount)
  local cs=system()
  local request=CraftItemRequest.new();request.target=player();request.itemRecord=v.rec;request.amount=math.max(1,math.floor(amount or 1))
  if v.ammo then request.bulletAmount=v.amount end
  cs:QueueRequest(request)
  pcall(function() cs:GetPlayerCraftBook():SetRecipeInspected(v.id) end)
  return true
end
function D.inspect(v) pcall(function() system():GetPlayerCraftBook():SetRecipeInspected(v.id) end);v.new=false end
-- The iconic weapons that can still be upgraded (upgradePanelController.swift GetUpgradableList).
function D.upgradable(ctx)
  local cs=system();local out={}
  ctx.manager:MarkToRebuild()
  for _,kind in ipairs(InventoryDataManagerV2.GetInventoryWeaponTypes()) do
    for _,data in ipairs(ctx.manager:GetPlayerIconicWeaponsByType(kind)) do
      local game=InventoryItemData.GetGameItemData(data)
      if game and not RPGManager.IsItemMaxTier(game) then
        local v=I.item(ctx,data)
        if v then
          v.game=game;v.rec=TweakDBInterface.GetItemRecord(ItemID.GetTDBID(v.id));v.quality=baseQuality(v.quality)
          v.craftable=cs:CanItemBeUpgraded(game)
          out[#out+1]=v
        end
      end
    end
  end
  table.sort(out,order)
  return out
end
function D.upgradeQuote(v)
  local cs=system()
  local recipe=cs:GetUpgradeRecipeData(v.id)
  local q={can=cs:CanItemBeUpgraded(v.game),ingredients=D.ingredients(recipe and recipe.ingredients)}
  -- "blocked": the menu's own words, the game has no key it can resolve for it
  if not q.can then q.reason=cs:EnoughIngredientsForUpgrading(v.game) and "blocked" or "LocKey#42797" end
  return q
end
function D.upgrade(v)
  if not system():CanItemBeUpgraded(v.game) then return false end
  local request=UpgradeItemRequest.new();request.owner=player();request.itemID=v.id
  system():QueueRequest(request)
  return true
end
-- Run fn with the item one upgrade further, as the upgrade screen previews it (ApplyQualityModifier): the
-- WasItemUpgraded stat raised by one for the call, then put back.
function D.withUpgrade(v,fn)
  local stats=Game.GetStatsSystem();local id=v.game:GetStatsObjectID()
  local old=v.game:GetStatValueByType(gamedataStatType.WasItemUpgraded)
  local function set(value)
    stats:RemoveAllModifiers(id,gamedataStatType.WasItemUpgraded,true)
    stats:AddSavedModifier(id,RPGManager.CreateStatModifier(gamedataStatType.WasItemUpgraded,gameStatModifierType.Additive,value))
  end
  set(old+1)
  local ok,result=pcall(fn)
  set(old)
  if not ok then print("[VRMenu] upgrade preview: "..tostring(result)) end
  return ok and result or nil
end
-- How many of each crafting component the player has: what changes when anything is crafted or upgraded.
function D.signature()
  local p=player();local ts=Game.GetTransactionSystem();local out={}
  for _,set in ipairs({UIInventoryHelper.GetCommonCraftingMaterials(),UIInventoryHelper.GetHackingCraftingMaterials()}) do
    for _,tdb in ipairs(set) do out[#out+1]=tostring(ts:GetItemQuantity(p,ItemID.CreateQuery(tdb))) end
  end
  return table.concat(out,",")
end
return D
