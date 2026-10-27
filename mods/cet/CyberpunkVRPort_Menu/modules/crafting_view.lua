-- The crafting section: the game's CRAFTING and UPGRADING screens as two tabs. Recipes or upgradable iconic
-- weapons on the left with the game's filters; on the right the game's tooltip of what the pick makes (for an
-- upgrade, the weapon now and one upgrade further), its components -- had / needed -- and the button.
local K=require("modules/ink")
local L=require("modules/locale")
local D=require("modules/crafting_data")
local I=require("modules/inventory_data")
local Images=require("modules/gameplay_data")
local Tooltip=require("modules/item_tooltip")
return function(C,U)
local V={}
local W,H=1780,1305
local LIST_X,LIST_W,LIST_Y,LIST_H=24,876,262,820
local CARD_W,CARD_H,ROW_H=426,170,182
local DX,DW=920,836
local colors={Common=K.GREY,Uncommon={.2,.85,.5},Rare={.25,.6,1},Epic={.75,.3,1},Legendary={1,.62,.16}}
local GOOD,SHORT={.25,.95,.55},K.RED
local function tr(ru,en) return L.tr(ru,en) end
local function loc(key,ru,en) local t=I.loc(key);if t~="" and t~=key then return t end;return tr(ru,en) end
local function state(kit)
  local g=C.state(kit)
  g.crafting=g.crafting or {tab="craft",filters={craft="AllItems",upgrade="AllItems"},selected={},quantity=1,tips={},ctx=I.open()}
  return g.crafting
end
local function icon(p,x,y,w,h,id,col,opacity)
  local image=Images.image(TweakDBID.new(id))
  if image then return K.image(p,x,y,w,h,image.atlas,image.part,col or K.CYAN,opacity or 1) end
end
local function card(kit,s,c,it,view,v,x)
  local col=colors[v.quality] or K.CYAN;local selected=s.selected[s.tab]==v.key
  local bg=K.roundFill(c,x,4,CARD_W,CARD_H,col,v.craftable and .06 or .02,4)
  local edge=K.cutFrame(c,x,4,CARD_W,CARD_H,selected and K.YELLOW or K.CYAN,selected and .85 or .35,10,1)
  K.rect(c,x+2,8,5,CARD_H-8,col,v.craftable and 1 or .4)
  U.picture(kit,c,x+14,22,CARD_W-28,100,v.image)
  K.text(c,x+14,126,CARD_W-28,32,K.clip(v.name,32),24,v.craftable and col or K.GREY)
  if v.new then K.image(c,x+CARD_W-20,13,9,9,K.ATLAS.phone,"circle",K.CYAN,1) end
  if v.typeIcon then K.image(c,x+CARD_W-44,128,26,26,v.typeIcon.atlas,v.typeIcon.part,K.YELLOW,1) end
  local b=kit.button("p1",kit.at(view.x,it.y,view),x,4,CARD_W,CARD_H,function()
    s.selected[s.tab]=v.key;s.quantity=1;s.armed=nil
    if s.tab=="craft" then D.inspect(v) end
    C.redraw(kit)
  end,function(on)
    bg:SetTintColor(K.color(on and K.YELLOW or col));bg:SetOpacity(on and .14 or v.craftable and .06 or .02)
    K.tint(edge,(on or selected) and K.YELLOW or K.CYAN,on and 1 or selected and .85 or .35)
  end);b.key="crafting:"..s.tab..":"..v.key
end
local function tabs(kit,p,s)
  for i,t in ipairs({{"craft","UI-PanelNames-CRAFTING","Создание","Crafting"},{"upgrade","UI-PanelNames-UPGRADING","Улучшение","Upgrading"}}) do
    local x=LIST_X+(i-1)*(LIST_W/2+6);local w=LIST_W/2-6;local on=s.tab==t[1]
    local bg=K.roundFill(p,x,100,w,60,on and K.YELLOW or K.NAVY,on and .14 or .18,4)
    local label=K.text(p,x,100,w,60,K.upper(loc(t[2],t[3],t[4])),29,on and K.YELLOW or K.TEXT,"center")
    if on then K.rect(p,x+14,155,w-28,2,K.YELLOW,1) end
    local b=kit.button("p1",kit.at(kit.P1X,kit.P1Y),x,100,w,60,function()
      if s.tab~=t[1] then s.tab=t[1];s.quantity=1;s.armed=nil;C.state(kit).resetList=true;C.redraw(kit) end
    end,function(h) bg:SetTintColor(K.color((h or on) and K.YELLOW or K.NAVY));label:SetTintColor(K.color((h or on) and K.YELLOW or K.TEXT)) end)
    b.key="craftingTab:"..t[1]
  end
end
local function filters(kit,p,s,list)
  local names=s.tab=="craft" and D.CRAFT_FILTERS or D.UPGRADE_FILTERS
  local caption=K.text(p,LIST_X+#names*73+8,178,LIST_W-#names*73-8,56,"",23,K.GREY)
  for n,name in ipairs(names) do
    local f=ItemFilterCategory[name];local x=LIST_X+(n-1)*73;local on=s.filters[s.tab]==name
    local label=I.loc(NameToString(ItemFilterCategories.GetLabelKey(f)))
    if on then caption:SetText(label) end
    local bg=K.roundFill(p,x,178,66,56,on and K.YELLOW or K.NAVY,on and .12 or .18,3)
    icon(p,x+9,183,48,46,ItemFilterCategories.GetIcon(f),on and K.YELLOW or K.CYAN)
    local b=kit.button("p1",kit.at(kit.P1X,kit.P1Y),x,178,66,56,function()
      s.filters[s.tab]=name;s.armed=nil;C.state(kit).resetList=true;C.redraw(kit)
    end,function(h) bg:SetTintColor(K.color((h or on) and K.YELLOW or K.NAVY));bg:SetOpacity(h and .18 or on and .12 or .18);if h then caption:SetText(label) end end)
    b.key="craftingFilter:"..name
  end
end
-- The bottom of the detail column, centred as the game's screens have it: the components, the quantity (crafting),
-- the button, and under it why it is blocked.
local ING_Y,QTY_Y,BTN_Y,WHY_Y=850,1044,1114,1186
local function ingredients(kit,p,list,y)
  K.text(p,DX,y,DW,40,K.upper(loc("UI-ResourceExports-CraftingMaterials","Компоненты","Components")),24,K.GREY,"center")
  local x0=DX+(DW-(#list*168-12))/2
  for i,g in ipairs(list) do
    local x=x0+(i-1)*168;local enough=g.have>=g.need
    K.roundFill(p,x,y+44,156,136,K.NAVY,.18,4);K.cutFrame(p,x,y+44,156,136,enough and K.CYAN or K.RED,.45,8,1)
    if g.image then K.image(p,x+23,y+52,110,72,g.image.atlas,g.image.part,K.WHITE,1) end
    K.text(p,x+4,y+130,148,40,(g.have>9999 and "9999+" or tostring(g.have)).." / "..g.need,26,enough and GOOD or SHORT,"center")
  end
end
local function tip(kit,s,host,ui,x,y,w,h,compare)
  local t=Tooltip.spawn(kit,host,ui,ItemDisplayContext.Backpack,x,y,w,h,compare)
  if t then s.tips[#s.tips+1]=t end
end
local function craftDetail(kit,p,s,v)
  if not s.dry or s.dry.key~=v.key then s.dry={key=v.key,dry=D.dry(v)} end
  local dry=s.dry.dry;local q=D.craftQuote(v,dry)
  s.quantity=math.max(1,math.min(s.quantity or 1,math.max(1,q.max)))
  K.text(p,DX,100,DW,56,K.clip(v.name,40),36,colors[v.quality] or K.CYAN)
  local host=K.canvas(p,0,0,W,1200)
  tip(kit,s,host,dry.ui,DX+12,172,DW-24,680)
  ingredients(kit,p,q.ingredients,ING_Y)
  if q.max>1 and not v.weapon then
    local x=DX+(DW-430)/2
    U.action(kit,p,kit.P1X,x,QTY_Y,80,"−",function() s.quantity=math.max(1,s.quantity-1);C.redraw(kit) end,s.quantity>1,60).key="craftQuantityMinus"
    K.text(p,x+84,QTY_Y,110,60,tostring(s.quantity),32,K.CYAN,"center")
    U.action(kit,p,kit.P1X,x+198,QTY_Y,80,"+",function() s.quantity=math.min(q.max,s.quantity+1);C.redraw(kit) end,s.quantity<q.max,60).key="craftQuantityPlus"
    U.action(kit,p,kit.P1X,x+290,QTY_Y,140,tr("МАКС.","MAX"),function() s.quantity=q.max;C.redraw(kit) end,true,60,28).key="craftQuantityMax"
  end
  if q.reason then K.text(p,DX,WHY_Y,DW,40,I.loc(q.reason),26,K.RED,"center") end
  U.action(kit,p,kit.P1X,DX+(DW-396)/2,BTN_Y,396,K.upper(loc("UI-Crafting-CraftItem","Изготовить","Craft")),function()
    D.craft(v,s.quantity);kit.toast(tr("Изготовлено: ","Crafted: ")..v.name..(s.quantity>1 and (" x"..s.quantity) or ""))
    s.quantity=1;s.refreshAt=kit.S.time+.4;C.redraw(kit)
  end,q.can,68,30).key="craftApply"
end
-- What the upgraded bars are compared with: the bars now, an unchanged one replaced by an empty bar. The tooltip
-- marks an unchanged bar past its scale as worse (SgnF(0) is -1); against an empty bar it draws no arrow.
local function comparedWith(nowBars,nextBars)
  local out={}
  for _,b in ipairs(nowBars.Values) do
    local same=false
    for _,n in ipairs(nextBars.Values) do
      if EnumInt(n.Type)==EnumInt(b.Type) and math.abs(n.Value-b.Value)<1e-4 then same=true end
    end
    out[#out+1]=same and UIInventoryItemWeaponBar.Make(gamedataItemType.Invalid,b.Type,0,1,true) or b
  end
  local bars=UIInventoryItemWeaponBars.new();bars.Values=out
  return bars
end
local function upgradeDetail(kit,p,s,v)
  local q=D.upgradeQuote(v)
  K.text(p,DX,100,DW,56,K.clip(v.name,40),36,colors[v.quality] or K.CYAN)
  local host=K.canvas(p,0,0,W,1200)
  local half=(DW-8)/2
  -- the weapon now, and one upgrade further: tooltip data snapshots, the second one made while the item carries
  -- the extra upgrade, its bars compared with the first (upgradePanelController.swift UpdateTooltipRightAndShow)
  -- from item data made now: an InventoryItemData keeps the stats it was made with (DPS 115 where the upgraded
  -- item has 129)
  local function snapshot()
    local d=s.ctx.manager:GetMinimalTooltipDataForInventoryItem(s.ctx.manager:GetInventoryItemData(v.game),false,nil,false)
    d.displayContext=InventoryTooltipDisplayContext.Upgrading
    return d
  end
  local okNow,now=pcall(snapshot)
  if okNow and now then
    -- a stats manager reads the item's stats when first asked: each snapshot's bars and stat list are read
    -- while the item is at that snapshot's level
    local function fetch(d) local stats=d:GetStatsManager();stats:SizeTooltipStats();return stats:GetWeaponBars() end
    local _,nowBars=pcall(fetch,now)
    local next=D.withUpgrade(v,function()
      local d=snapshot();d.isEquipped=false
      local bars=fetch(d)
      if bars and nowBars then bars:SetComparedBars(comparedWith(nowBars,bars)) end
      return d
    end)
    local t=Tooltip.spawnData(kit,host,now,DX+8,172,half-12,680);if t then s.tips[#s.tips+1]=t end
    if next then t=Tooltip.spawnData(kit,host,next,DX+half+12,172,half-12,680);if t then s.tips[#s.tips+1]=t end end
  end
  ingredients(kit,p,q.ingredients,ING_Y)
  if q.reason then K.text(p,DX,WHY_Y,DW,40,q.reason=="blocked" and tr("Сейчас недоступно","Not available right now") or I.loc(q.reason),26,K.RED,"center") end
  U.action(kit,p,kit.P1X,DX+(DW-396)/2,BTN_Y,396,K.upper(loc("UI-Crafting-Upgrade","Улучшить","Upgrade")),function()
    if D.upgrade(v) then kit.toast(tr("Улучшено: ","Upgraded: ")..v.name) else kit.toast(kit.T.blocked) end
    s.refreshAt=kit.S.time+.4;C.redraw(kit)
  end,q.can,68,30).key="upgradeApply"
end
function V.draw(kit,p,p2)
  local s=state(kit);s.tips={}
  kit.glass(p,W,H,kit.P1X,kit.P1Y)
  local list
  if s.tab=="craft" then s.recipes=s.recipes or D.recipes();list=s.recipes
  else s.upgrades=s.upgrades or D.upgradable(s.ctx);list=s.upgrades end
  s.sig=s.sig or D.signature()
  U.title(p,K.upper(loc(s.tab=="craft" and "UI-PanelNames-CRAFTING" or "UI-PanelNames-UPGRADING",s.tab=="craft" and "Создание" or "Улучшение",s.tab=="craft" and "Crafting" or "Upgrading")),W)
  tabs(kit,p,s);filters(kit,p,s,list)
  K.rect(p,DX-10,100,1,1080,K.CYAN,.22)
  local shown={}
  for _,v in ipairs(list) do if D.matches(v,s.filters[s.tab]) then shown[#shown+1]=v end end
  local rows={}
  for first=1,#shown,2 do
    local group={};for i=first,math.min(#shown,first+1) do group[#group+1]=shown[i] end
    rows[#rows+1]={h=ROW_H,build=function(c,it,view) for i,v in ipairs(group) do card(kit,s,c,it,view,v,(i-1)*(CARD_W+12)) end end}
  end
  if #rows==0 then rows[1]={h=130,build=function(c) K.text(c,10,20,LIST_W-20,90,tr("Здесь пока ничего нет","Nothing here yet"),29,K.GREY,"center") end} end
  local host=K.canvas(p,LIST_X,0,LIST_W,1200)
  C.list(kit,host,"p1","p1",kit.P1X+LIST_X,LIST_Y,LIST_W,rows,LIST_H,kit.S.keepScroll):enableMask()
  local selected
  for _,v in ipairs(shown) do if v.key==s.selected[s.tab] then selected=v end end
  if not selected then
    s.selected[s.tab]=nil
    K.text(p,DX,500,DW,120,s.tab=="craft" and tr("Выберите чертёж","Select a recipe") or tr("Выберите оружие для улучшения","Select a weapon to upgrade"),32,K.GREY,"center")
    return
  end
  if s.tab=="craft" then craftDetail(kit,p,s,selected) else upgradeDetail(kit,p,s,selected) end
end
function V.update(kit)
  local s=C.state(kit).crafting;if not s then return end
  Tooltip.fit(s.tips)
  -- after a craft or an upgrade, and whenever the components change: read the lists again
  if kit.S.time-(s.poll or 0)<.5 and not (s.refreshAt and kit.S.time>=s.refreshAt) then return end
  s.poll=kit.S.time
  local sig=D.signature()
  if (s.refreshAt and kit.S.time>=s.refreshAt) or sig~=s.sig then
    s.refreshAt=nil;s.sig=sig;s.recipes=nil;s.upgrades=nil;s.dry=nil;I.invalidate(s.ctx);C.redraw(kit)
  end
end
function V.leave(kit) local g=C.state(kit);if g.crafting then I.close(g.crafting.ctx);g.crafting=nil end end
return V
end
