local K=require("modules/ink")
local L=require("modules/locale")
local D=require("modules/trade_data")
local I=require("modules/inventory_data")
local Tooltip=require("modules/item_tooltip")
local Sort=require("modules/item_sort")
return function(C,U)
local V={};local W,H=1780,1305
local colors={Common=K.GREY,Uncommon={.2,.85,.5},Rare={.25,.6,1},Epic={.75,.3,1},Legendary={1,.62,.16}}
local function tr(kit,ru,en) return L.tr(ru,en) end
local function blueprint(kit,parent,x,y,color)
  local b=K.canvas(parent,x,y,146,30)
  K.roundFill(b,0,0,146,30,K.BLACK,.85,3)
  -- Folded plan sheet: distinct from the item artwork and rarity stripe.
  local glyph=K.canvas(b,116,0,30,30)
  local points={{8,4},{20,4},{26,10},{26,25},{8,25},{8,4}}
  for i=1,#points-1 do U.line(glyph,points[i][1],points[i][2],points[i+1][1],points[i+1][2],color,1,1.5) end
  U.line(glyph,20,4,20,10,color,1,1.5);U.line(glyph,20,10,26,10,color,1,1.5)
  U.line(glyph,12,14,22,14,color,1,1.5);U.line(glyph,12,19,20,19,color,1,1.5)
  K.text(b,5,0,109,30,tr(kit,"ЧЕРТЁЖ","BLUEPRINT"),20,color,"right")
end
local function state(kit)
  local g=C.state(kit);g.trade=g.trade or {ctx=D.open(),filter="all",quantity=1,comparison=true,sorts={sell="QualityAsc",buy="QualityAsc"}}
  return g.trade
end
local function action(kit,p,x,y,w,ru,en,fn,enabled,key)
  local b=U.action(kit,p,kit.P1X,x,y,w,tr(kit,ru,en),fn,enabled);b.key=key;return b
end
local function tip(kit,p,ui,side,x,y,w,h,compare,price)
  local s=state(kit)
  local t=Tooltip.spawn(kit,p,ui,side=="sell" and ItemDisplayContext.VendorPlayer or ItemDisplayContext.Vendor,x,y,w,h,compare,price)
  if t then s.tips[#s.tips+1]=t end
end
local function sortKey(s) return s.filter..":"..s.sorts.sell..":"..s.sorts.buy end
local function filtered(snapshot,side,filter)
  local out={};for _,v in ipairs(snapshot[side]) do if filter=="all" or filter==v.category then out[#out+1]=v end end
  return out
end
function V.draw(kit,p,p2)
  local s=state(kit);local visit=kit.vendorSession();s.tips={};s.hover=nil;s.shown=nil
  kit.glass(p,W,H,kit.P1X,kit.P1Y);U.title(p,tr(kit,"ТОРГОВЛЯ","TRADE"),W)
  if not visit then K.text(p,30,210,W-60,160,tr(kit,"Откройте магазин у продавца, чтобы торговать.","Open a shop with a vendor to trade."),34,K.CYAN,"center");return end
  if not s.snapshot or not s.sorted or s.sortedKey~=sortKey(s) then
    -- the snapshot is read a slice a frame (V.update); the panel fills in when it is ready, with no
    -- loading line in the meantime
    s.loading=s.loading or {key=sortKey(s),filter=s.filter,modes={sell=s.sorts.sell,buy=s.sorts.buy}}
    return
  end
  s.stamp=D.signature(visit);s.poll=kit.S.time
  U.comparison(kit,p,H,s.comparison,function() s.comparison=not s.comparison;C.redraw(kit) end,"tradeComparison")
  local filters={{"all","ВСЁ","ALL"},{"weapon","ОРУЖИЕ","WEAPONS"},{"clothing","ОДЕЖДА","CLOTHING"},{"mods","МОДИФИКАЦИИ","MODS"},{"consumable","РАСХОДНИКИ","CONSUMABLES"},{"other","ДРУГОЕ","OTHER"}}
  for i,f in ipairs(filters) do
    local x=24+(i-1)*290;local on=s.filter==f[1]
    local bg=K.roundFill(p,x,105,278,60,on and K.YELLOW or K.NAVY,on and .12 or .18,4)
    local label=K.text(p,x,105,278,60,tr(kit,f[2],f[3]),25,on and K.YELLOW or K.TEXT,"center")
    local b=kit.button("p1",kit.at(kit.P1X,kit.P1Y),x,105,278,60,function()
      s.filter=f[1];s.selected=nil;s.confirm=nil;C.state(kit).resetList=true;C.redraw(kit)
    end,function(h) bg:SetTintColor(K.color((h or on) and K.YELLOW or K.NAVY));label:SetTintColor(K.color((h or on) and K.YELLOW or K.TEXT));bg:SetOpacity(h and .18 or on and .12 or .18) end);b.key="tradeFilter:"..f[1]
  end
  K.text(p,24,180,820,60,tr(kit,"ВАШИ ПРЕДМЕТЫ","YOUR ITEMS").."  /  "..#s.snapshot.sell,31,K.CYAN)
  K.text(p,920,180,820,60,K.clip(s.snapshot.name~="" and s.snapshot.name or tr(kit,"ПРОДАВЕЦ","VENDOR"),25).."  /  €$ "..s.snapshot.vendorMoney,31,K.YELLOW)
  K.rect(p,882,180,1,840,K.CYAN,.22)
  local function grid(side,x)
    local rows,shown={},s.sorted[side]
    Sort.control(kit,p,s,"tradeSort:"..side,x+528,256,244,s.sorts[side],function(value)
      s.sorts[side]=value;s.selected=nil;s.confirm=nil;s.resetSide=side
    end,function() C.redraw(kit) end)
    if #shown==0 then rows[1]={h=100,build=function(c) K.text(c,10,10,790,80,side=="buy" and tr(kit,"Нет товаров в этой категории","No items in this category") or tr(kit,"Нет подходящих предметов","No matching items"),29,K.GREY,"center") end} end
    for first=1,#shown,2 do
      local group={};for i=first,math.min(#shown,first+1) do group[#group+1]=shown[i] end
      rows[#rows+1]={h=200,build=function(c,it,view)
        for i,v in ipairs(group) do
          local xx=(i-1)*404;local width=392;local col=colors[v.quality] or K.CYAN
          local bg=K.roundFill(c,xx,4,width,185,col,.045,4);local edge=K.cutFrame(c,xx,4,width,185,s.selected==v.key and K.YELLOW or K.CYAN,.35,10,1)
          K.rect(c,xx+2,8,5,177,col,1)
          U.picture(kit,c,xx+14,v.recipe and 44 or 20,width-28,v.recipe and 80 or 104,v.image)
          if v.recipe then blueprint(kit,c,xx+width-150,10,col) end
          K.text(c,xx+12,126,width-24,30,K.clip(v.name,27),24,col)
          K.text(c,xx+14,158,200,28,"€$ "..v.price,24,K.YELLOW)
          K.text(c,xx+230,158,147,28,v.quantity>1 and ("x"..v.quantity) or v.buyback and tr(kit,"ВЫКУП","BUYBACK") or "",23,K.GREY,"right")
          if v.equipped then U.line(c,xx+width-34,15,xx+width-26,23,K.CYAN,1,3);U.line(c,xx+width-26,23,xx+width-12,8,K.CYAN,1,3) end
          if v.new then K.image(c,xx+12,10,9,9,K.ATLAS.phone,"circle",K.CYAN,1) end
          local b=kit.button("p1",kit.at(view.x,it.y,view),xx,4,width,185,function()
            s.selected=v.key;s.quantity=1;s.confirm=nil;I.inspect(s.ctx,v);C.redraw(kit)
          end,function(on)
            bg:SetTintColor(K.color(on and K.YELLOW or col));bg:SetOpacity(on and .13 or .045);K.tint(edge,(on or s.selected==v.key) and K.YELLOW or K.CYAN,on and 1 or .35)
            if on then s.hover=v.key;s.hoverAt=kit.S.time elseif s.hover==v.key then s.hover=nil;s.hoverAt=kit.S.time end
          end);b.key="tradeItem:"..v.key
        end
      end}
    end
    local host=K.canvas(p,x,0,850,1100)
    C.list(kit,host,"p1",side=="sell" and "p1" or "p2",kit.P1X+x,320,850,rows,645,s.offsets and s.offsets[side]):enableMask()
  end
  grid("sell",10);grid("buy",906)
  local selected=s.snapshot.byKey[s.selected]
  s.quote=nil
  if selected then
    local quote=D.quote(s.ctx,visit,selected,s.quantity)
    s.quote=quote
    local name=(selected.recipe and tr(kit,"ЧЕРТЁЖ · ","BLUEPRINT · ") or "")..selected.name
    K.text(p,26,1037,1710,47,K.clip(name,75),32,colors[selected.quality] or K.CYAN)
    if quote.max>1 then
      action(kit,p,24,1110,90,"","",function() s.quantity=math.max(1,s.quantity-1);s.confirm=nil;C.redraw(kit) end,s.quantity>1,"tradeQuantityMinus")
      K.rect(p,51,1142,36,4,K.CYAN,s.quantity>1 and 1 or .25)
      K.text(p,125,1110,150,68,tostring(s.quantity),32,K.CYAN,"center")
      action(kit,p,286,1110,90,"+","+",function() s.quantity=math.min(quote.max,s.quantity+1);s.confirm=nil;C.redraw(kit) end,s.quantity<quote.max,"tradeQuantityPlus")
      action(kit,p,392,1110,180,"МАКС.","MAX",function() s.quantity=quote.max;s.confirm=nil;C.redraw(kit) end,true,"tradeQuantityMax")
    end
    local ru=selected.side=="sell" and "ПРОДАТЬ" or selected.buyback and "ВЫКУПИТЬ" or "КУПИТЬ"
    local en=selected.side=="sell" and "SELL" or selected.buyback and "BUY BACK" or "BUY"
    if s.confirm then ru,en="ПОДТВЕРДИТЬ ПРОДАЖУ","CONFIRM SALE" end
    action(kit,p,900,1110,850,ru.."  €$ "..(quote.total or 0),L.t(en).."  €$ "..(quote.total or 0),function()
      if s.pending then return end
      local result=D.transact(s.ctx,kit.vendorSession(),selected,s.quantity,s.confirm==selected.key)
      if result=="confirm" then s.confirm=selected.key;C.redraw(kit)
      elseif result then s.confirm=nil;s.pending={at=kit.S.time,stamp=s.stamp};kit.toast(tr(kit,"Применение…","Applying…"))
      else kit.toast(kit.T.blocked) end
    end,quote.allowed and not s.pending,"tradeApply")
    if s.confirm then K.text(p,585,1100,300,100,tr(kit,"Будет продан культовый или экипированный предмет","Selling an iconic or equipped item"),24,K.YELLOW,nil,true)
    elseif not quote.allowed then K.text(p,24,1100,820,100,quote.reason=="protected" and tr(kit,"Этот предмет нельзя продать","This item cannot be sold") or tr(kit,"Недостаточно денег, запаса или не выполнены требования","Insufficient funds, stock or requirements"),26,K.RED,nil,true) end
  else K.text(p,30,1080,W-60,100,tr(kit,"Выберите предмет для покупки или продажи","Select an item to buy or sell"),31,K.GREY,"center") end
  s.tipHost=K.canvas(p,0,0,W,1030)
  s.block=kit.button("p2",kit.at(kit.P1X,kit.P1Y),900,240,850,790,nil,nil);s.block.blocker=true;s.block.visible=false
  s.closeHost=K.canvas(p,0,0,60,50);s.closeHost:SetVisible(false)
  K.roundFill(s.closeHost,0,0,60,50,K.BLACK,1,7);U.line(s.closeHost,18,12,42,36,K.CYAN,1,3);U.line(s.closeHost,42,12,18,36,K.CYAN,1,3)
  s.close=kit.button("p2",kit.at(kit.P1X,kit.P1Y),0,0,60,50,function() s.selected=nil;s.hover=nil;s.confirm=nil;C.redraw(kit) end,nil);s.close.key="tradeTooltipClose";s.close.visible=false
  Sort.popup(kit,p,s)
end
function V.update(kit)
  local g=C.state(kit);local s=g.trade;if not s then return end
  local visit=kit.vendorSession();if not visit then kit.openSection("inventory");return end
  if s.loading then
    local load=s.loading;local deadline=os.clock()+.004
    if not load.snapshot then
      if s.snapshot then load.snapshot=s.snapshot
      elseif not load.data then load.data=D.beginSnapshot(s.ctx,visit);return
      else load.snapshot=D.stepSnapshot(s.ctx,visit,load.data,deadline);if not load.snapshot then return end end
    end
    load.sorted=load.sorted or {};local side=not load.sorted.sell and "sell" or "buy"
    load.sort=load.sort or Sort.begin(filtered(load.snapshot,side,load.filter),load.modes[side])
    local sorted=Sort.step(load.sort,deadline)
    if sorted then load.sorted[side]=sorted;load.sort=nil end
    if load.sorted.sell and load.sorted.buy then
      s.snapshot=load.snapshot;s.sorted=load.sorted;s.sortedKey=load.key;s.loading=nil
      C.redraw(kit)
    end
    return
  end
  Tooltip.fit(s.tips)
  local key=s.hover or s.selected
  if s.tipHost and key~=s.shown and kit.S.time-(s.hoverAt or 0)>.12 then
    s.tipHost:RemoveAllChildren();s.tips={};s.shown=key;s.block.visible=false;s.close.visible=false;s.closeHost:SetVisible(false)
    local v=s.snapshot.byKey[key]
    if v then
      local x=v.side=="sell" and 912 or 24
      K.roundFill(s.tipHost,x,236,832,792,K.BLACK,1,8)
      local comparison=s.comparison and s.ctx.compare:GetPreferredComparisonItem(v.ui) or nil
      if comparison and comparison:GetID()~=v.id then
        tip(kit,s.tipHost,v.ui,v.side,x+8,250,400,755,comparison,v.price)
        tip(kit,s.tipHost,comparison,"sell",x+425,250,400,755,v.ui)
      else tip(kit,s.tipHost,v.ui,v.side,x+12,250,810,755,nil,v.price) end
      s.block.x=kit.P1X+x;s.block.visible=true
      K.move(s.closeHost,x+770,185);s.closeHost:SetVisible(true);s.close.x=kit.P1X+x+770;s.close.y=kit.P1Y+185;s.close.visible=true
    end
  end
  if kit.S.time-(s.poll or 0)<.5 then return end
  s.poll=kit.S.time
  local stamp=D.signature(visit)
  if stamp~=s.stamp or (s.pending and kit.S.time-s.pending.at>3) then
    s.pending=nil;s.offsets={sell=kit.S.views.p1 and kit.S.views.p1.offset,buy=kit.S.views.p2 and kit.S.views.p2.offset}
    I.invalidate(s.ctx);s.ctx.compare:FlushCache();s.snapshot=nil;s.sorted=nil;C.redraw(kit)
  end
end
function V.beforeBuild(kit)
  local g=C.state(kit);local s=g.trade
  if s then
    s.offsets=not g.resetList and {sell=kit.S.views.p1 and kit.S.views.p1.offset,buy=kit.S.views.p2 and kit.S.views.p2.offset} or nil
    if s.resetSide then s.offsets=s.offsets or {};s.offsets[s.resetSide]=0;s.resetSide=nil end
    s.tipHost=nil;s.tips={}
  end
end
function V.leave(kit) local g=C.state(kit);if g.trade then D.close(g.trade.ctx);g.trade=nil end end
return V
end
