local K=require("modules/ink")
local L=require("modules/locale")
local D=require("modules/stash_data")
local I=require("modules/inventory_data")
local Images=require("modules/gameplay_data")
local Tooltip=require("modules/item_tooltip")
local Sort=require("modules/item_sort")
return function(C,U)
local V={};local W,H=1780,1305
local colors={Common=K.GREY,Uncommon={.2,.85,.5},Rare={.25,.6,1},Epic={.75,.3,1},Legendary={1,.62,.16}}
local filterNames={"AllItems","RangedWeapons","MeleeWeapons","Clothes","Consumables","Attachments","SoftwareMods","Programs","Cyberware","Junk","Quest"}
local function tr(kit,ru,en) return L.tr(ru,en) end
local function state(kit)
  local g=C.state(kit)
  g.stash=g.stash or {ctx=D.open(),quantity=1,comparison=true,filters={deposit="all",withdraw="all"},sorts={deposit="QualityAsc",withdraw="QualityAsc"}}
  return g.stash
end
local function action(kit,p,x,y,w,ru,en,fn,enabled,key)
  local b=U.action(kit,p,kit.P1X,x,y,w,tr(kit,ru,en),fn,enabled);b.key=key;return b
end
local function icon(p,x,y,w,h,id,col,opacity)
  local image=Images.image(TweakDBID.new(id))
  if image then return K.image(p,x,y,w,h,image.atlas,image.part,col or K.CYAN,opacity or 1) end
end
local function card(kit,s,c,it,view,v,x)
  local w,h=392,178;local col=colors[v.quality] or K.CYAN;local weapon=v.ui:IsWeapon()
  local bg=K.roundFill(c,x,4,w,h,col,.045,4)
  local edge=K.cutFrame(c,x,4,w,h,s.selected==v.key and K.YELLOW or K.CYAN,.35,10,1)
  K.rect(c,x+2,8,5,h-8,col,1)
  U.picture(kit,c,x+14,27,w-28,92,v.image)
  K.text(c,x+12,119,w-24,28,K.clip(v.name,29),23,colors[v.quality] or K.CYAN)
  if v.new and v.side=="deposit" then K.image(c,x+w-20,13,9,9,K.ATLAS.phone,"circle",K.CYAN,1) end
  if v.equipped then U.line(c,x+w-50,15,x+w-43,22,K.CYAN,1,3);U.line(c,x+w-43,22,x+w-31,8,K.CYAN,1,3) end
  if v.comparison==ItemComparisonState.Better or v.comparison==ItemComparisonState.Worse then
    local up=v.comparison==ItemComparisonState.Better;local sign=up and -1 or 1;local color=up and {.2,.95,.5} or K.RED
    U.line(c,x+w-29,40-sign*5,x+w-22,40+sign*3,color,1,3);U.line(c,x+w-22,40+sign*3,x+w-15,40-sign*5,color,1,3)
  end
  local tx=x+14
  if weapon then
    local attachments={}
    for n,part in ipairs(v.slots) do
      K.image(c,x+14+(n-1)*18,13,11,11,K.ATLAS.phone,"circle",K.RED,1)
      if not part.item then K.image(c,x+17+(n-1)*18,16,5,5,K.ATLAS.phone,"circle",K.BLACK,1) end
      if part.id==TweakDBID.new("AttachmentSlots.Scope") or part.id==TweakDBID.new("AttachmentSlots.PowerModule") then attachments[#attachments+1]=part end
    end
    if not v.melee then
      K.rect(c,tx,155,3,15,K.CYAN,1);K.rect(c,tx+6,155,3,15,K.CYAN,1)
      K.text(c,tx+15,149,65,28,v.ammo>999 and "999+" or tostring(v.ammo),22,K.CYAN);tx=tx+78
    end
    if v.typeIcon then K.image(c,tx,150,25,25,v.typeIcon.atlas,v.typeIcon.part,v.iconic and K.YELLOW or K.CYAN,1) end
    local avail=x+w-12-#attachments*29-(tx+28)
    K.text(c,tx+28,149,avail,28,K.clip(K.upper(v.typeName),math.max(7,math.floor(avail/10))),20,v.iconic and K.YELLOW or K.GREY)
    for n,part in ipairs(attachments) do
      icon(c,x+w-12-#attachments*29+(n-1)*29,149,25,25,NameToString(UIItemsHelper.GetSlotShadowIcon(part.id,gamedataItemType.Invalid,gamedataEquipmentArea.Invalid)),part.item and K.CYAN or K.GREY,part.item and 1 or .4)
    end
  else
    K.text(c,tx,149,275,28,K.clip(v.typeName,25),21,K.GREY)
    K.text(c,x+w-95,149,80,28,v.quantity>1 and ("x"..v.quantity) or "",22,K.CYAN,"right")
  end
  local b=kit.button("p1",kit.at(view.x,it.y,view),x,4,w,h,function()
    s.selected=v.key;s.quantity=1;s.confirm=nil
    if v.side=="deposit" then I.inspect(s.ctx,v) end
    C.redraw(kit)
  end,function(on)
    bg:SetTintColor(K.color(on and K.YELLOW or col));bg:SetOpacity(on and .13 or .045);K.tint(edge,(on or s.selected==v.key) and K.YELLOW or K.CYAN,on and 1 or .35)
    if on then s.hover=v.key;s.hoverAt=kit.S.time elseif s.hover==v.key then s.hover=nil;s.hoverAt=kit.S.time end
  end);b.key="stashItem:"..v.key
end
function V.draw(kit,p,p2)
  local s=state(kit);local visit=kit.stashSession();s.tips={};s.hover=nil;s.shown=nil
  kit.glass(p,W,H,kit.P1X,kit.P1Y);U.title(p,tr(kit,"ТАЙНИК ВИ","V'S STASH"),W)
  if not visit then K.text(p,30,210,W-60,160,tr(kit,"Откройте тайник, чтобы перемещать предметы.","Open your stash to transfer items."),34,K.CYAN,"center");return end
  s.snapshot=s.snapshot or D.snapshot(s.ctx,visit);s.stamp=D.signature(s.ctx,visit);s.poll=kit.S.time
  U.comparison(kit,p,H,s.comparison,function() s.comparison=not s.comparison;C.redraw(kit) end,"stashComparison")
  K.rect(p,882,110,1,910,K.CYAN,.22)
  s.counts={}
  for _,side in ipairs({"deposit","withdraw"}) do
    local x=side=="deposit" and 12 or 908;local items=s.snapshot[side];local categories={}
    for _,v in ipairs(items) do categories[v.category]=true end
    if s.filters[side]~="all" and not categories[s.filters[side]] then s.filters[side]="all" end
    local shown=D.sorted(items,s.filters[side],s.sorts[side]);s.counts[side]=#shown
    K.text(p,x+12,105,810,54,(side=="deposit" and tr(kit,"СНАРЯЖЕНИЕ ВИ","V'S EQUIPMENT") or tr(kit,"ХРАНИЛИЩЕ","STORAGE")).."  /  "..#items,32,K.CYAN)
    local caption=K.text(p,x+12,235,450,32,tr(kit,"ВСЕ ПРЕДМЕТЫ","ALL ITEMS"),23,K.GREY)
    local n=0
    for _,name in ipairs(filterNames) do
      local f=ItemFilterCategory[name];local key=name=="AllItems" and "all" or tostring(f)
      if key=="all" or categories[key] then
        local xx=x+10+n*73;n=n+1;local on=s.filters[side]==key
        local label=I.loc(NameToString(ItemFilterCategories.GetLabelKey(f)))
        if on then caption:SetText(label) end
        local bg=K.roundFill(p,xx,169,66,56,on and K.YELLOW or K.NAVY,on and .12 or .18,3)
        icon(p,xx+9,174,48,46,ItemFilterCategories.GetIcon(f),on and K.YELLOW or K.CYAN)
        local b=kit.button("p1",kit.at(kit.P1X,kit.P1Y),xx,169,66,56,function()
          s.filters[side]=key;s.selected=nil;s.confirm=nil;s.resetSide=side;C.redraw(kit)
        end,function(h) bg:SetTintColor(K.color((h or on) and K.YELLOW or K.NAVY));bg:SetOpacity(h and .18 or on and .12 or .18);if h then caption:SetText(label) end end);b.key="stashFilter:"..side..":"..name
      end
    end
    Sort.control(kit,p,s,"stashSort:"..side,x+528,232,244,s.sorts[side],function(value)
      s.sorts[side]=value;s.selected=nil;s.confirm=nil;s.resetSide=side
    end,function() C.redraw(kit) end)
    local rows={}
    for first=1,#shown,2 do
      local group={};for i=first,math.min(#shown,first+1) do group[#group+1]=shown[i] end
      rows[#rows+1]={h=190,build=function(c,it,view) for i,v in ipairs(group) do card(kit,s,c,it,view,v,(i-1)*404) end end}
    end
    if #rows==0 then rows[1]={h=130,build=function(c) K.text(c,10,20,790,90,tr(kit,"Нет предметов в этой категории","No items in this category"),29,K.GREY,"center") end} end
    local host=K.canvas(p,x,0,850,1100)
    C.list(kit,host,"p1",side=="deposit" and "p1" or "p2",kit.P1X+x,320,850,rows,645,s.offsets and s.offsets[side]):enableMask()
  end
  local selected=s.snapshot.byKey[s.selected];s.quote=nil
  if selected then
    local q=D.quote(s.ctx,visit,selected,s.quantity);s.quote=q
    K.text(p,26,1037,1710,47,K.clip(selected.name,78),32,colors[selected.quality] or K.CYAN)
    if q.max>1 then
      action(kit,p,24,1110,90,"−","−",function() s.quantity=math.max(1,s.quantity-1);s.confirm=nil;C.redraw(kit) end,s.quantity>1,"stashQuantityMinus")
      K.text(p,125,1110,150,68,tostring(s.quantity),32,K.CYAN,"center")
      action(kit,p,286,1110,90,"+","+",function() s.quantity=math.min(q.max,s.quantity+1);s.confirm=nil;C.redraw(kit) end,s.quantity<q.max,"stashQuantityPlus")
      action(kit,p,392,1110,180,"МАКС.","MAX",function() s.quantity=q.max;s.confirm=nil;C.redraw(kit) end,true,"stashQuantityMax")
    end
    local deposit=selected.side=="deposit"
    s.apply=action(kit,p,900,1110,850,s.confirm and "ПОДТВЕРДИТЬ ПЕРЕНОС" or deposit and "ПОЛОЖИТЬ В ТАЙНИК →" or "← ЗАБРАТЬ",s.confirm and "CONFIRM TRANSFER" or deposit and "STORE →" or "← TAKE",function()
      if s.pending then return end
      local result=D.transact(s.ctx,kit.stashSession(),selected,s.quantity,s.confirm==selected.key)
      if result=="confirm" then s.confirm=selected.key;C.redraw(kit)
      elseif result then s.confirm=nil;s.pending={at=kit.S.time,id=selected.id,side=selected.side,before=q.max,amount=s.quantity};s.apply.visible=false;kit.toast(tr(kit,"Перемещение…","Transferring…"))
      else kit.toast(kit.T.blocked) end
    end,q.allowed and not s.pending,"stashApply")
    if s.confirm then K.text(p,585,1100,300,100,tr(kit,"Экипированный предмет будет снят","The equipped item will be unequipped"),24,K.YELLOW,nil,true)
    elseif not q.allowed then K.text(p,24,1100,820,100,tr(kit,"Этот предмет сейчас нельзя переместить","This item cannot be transferred right now"),26,K.RED,nil,true) end
  else K.text(p,30,1080,W-60,100,tr(kit,"Выберите предмет, чтобы положить его в тайник или забрать","Select an item to store or take"),31,K.GREY,"center") end
  s.tipHost=K.canvas(p,0,0,W,1030)
  s.block=kit.button("p2",kit.at(kit.P1X,kit.P1Y),900,302,850,730,nil,nil);s.block.blocker=true;s.block.visible=false
  s.closeHost=K.canvas(p,0,0,60,50);s.closeHost:SetVisible(false)
  K.roundFill(s.closeHost,0,0,60,50,K.BLACK,1,7);U.line(s.closeHost,18,12,42,36,K.CYAN,1,3);U.line(s.closeHost,42,12,18,36,K.CYAN,1,3)
  s.close=kit.button("p2",kit.at(kit.P1X,kit.P1Y),0,0,60,50,function() s.selected=nil;s.hover=nil;s.confirm=nil;C.redraw(kit) end,nil);s.close.key="stashTooltipClose";s.close.visible=false
  Sort.popup(kit,p,s)
end
function V.update(kit)
  local s=C.state(kit).stash;if not s then return end
  local visit=kit.stashSession();if not visit then kit.openSection("inventory");return end
  Tooltip.fit(s.tips)
  local key=s.hover or s.selected
  if s.tipHost and key~=s.shown and kit.S.time-(s.hoverAt or 0)>.12 then
    s.tipHost:RemoveAllChildren();s.tips={};s.shown=key;s.block.visible=false;s.close.visible=false;s.closeHost:SetVisible(false)
    local v=s.snapshot.byKey[key]
    if v then
      local x=v.side=="deposit" and 912 or 24
      K.roundFill(s.tipHost,x,302,832,726,K.BLACK,1,8)
      local comparison=s.comparison and s.ctx.compare:GetPreferredComparisonItem(v.ui) or nil
      local function tip(ui,xx,w,other) local t=Tooltip.spawn(kit,s.tipHost,ui,ItemDisplayContext.VendorPlayer,xx,310,w,700,other);if t then s.tips[#s.tips+1]=t end end
      if comparison and comparison:GetID()~=v.id then tip(v.ui,x+8,400,comparison);tip(comparison,x+425,400,v.ui)
      else tip(v.ui,x+12,810) end
      s.block.x=kit.P1X+x;s.block.visible=true
      K.move(s.closeHost,x+770,250);s.closeHost:SetVisible(true);s.close.x=kit.P1X+x+770;s.close.y=kit.P1Y+250;s.close.visible=true
    end
  end
  if kit.S.time-(s.poll or 0)<.5 then return end
  s.poll=kit.S.time
  local stamp=D.signature(s.ctx,visit)
  if stamp~=s.stamp or (s.pending and kit.S.time-s.pending.at>3) then
    if s.pending then
      local q=s.pending;local owner=q.side=="deposit" and Game.GetPlayer() or visit.object
      local count=Game.GetTransactionSystem():GetItemQuantity(owner,q.id)
      if count==q.before-q.amount then s.selected=nil;kit.toast(tr(kit,"Предмет перемещён","Item transferred"))
      else kit.toast(tr(kit,"Перенос не выполнен","Transfer did not complete")) end
    end
    s.pending=nil;s.confirm=nil
    I.invalidate(s.ctx);s.ctx.compare:FlushCache();s.snapshot=D.snapshot(s.ctx,visit)
    local selected=s.snapshot.byKey[s.selected]
    if selected then s.quantity=math.min(s.quantity,selected.quantity) else s.selected=nil;s.quantity=1 end
    C.redraw(kit)
  end
end
function V.beforeBuild(kit)
  local s=C.state(kit).stash
  if s then
    s.offsets={deposit=kit.S.views.p1 and kit.S.views.p1.offset,withdraw=kit.S.views.p2 and kit.S.views.p2.offset}
    if s.resetSide then s.offsets[s.resetSide]=0;s.resetSide=nil end
    s.tipHost=nil;s.tips={}
  end
end
function V.leave(kit) local g=C.state(kit);if g.stash then D.close(g.stash.ctx);g.stash=nil end end
return V
end
