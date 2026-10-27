-- EquipmentEx's wardrobe inside the inventory section: the saved outfits on the left, the character in the
-- middle, on the right the clothing EquipmentEx offers, grouped by its outfit slots (a slot folds the way it
-- does in EquipmentEx's own screen). Texts are the game's and EquipmentEx's own localized strings.
local K=require("modules/ink")
local L=require("modules/locale")
local Wd=require("modules/wardrobe_data")
local Preview=require("modules/inventory_preview")
return function(C,U,tile)
local V={}
local W=1780
local COLS,TILE_H=3,206
local LEFT_X,LEFT_W,RIGHT_X,RIGHT_W=10,478,1066,704

local function pending(kit,ok)
  if ok then
    local g=C.state(kit);g.inventoryRefreshAt=kit.S.time+.3;g.inventoryPreviewDirty=true
  else kit.toast(kit.T.blocked) end
end

-- A compact glass row: a label, an optional note under it, yellow when chosen. `extra` draws on the row
-- after its own button, so a button it adds is the one a pointer finds.
local function row(kit,label,note,click,opts)
  opts=opts or {}
  return {h=96,build=function(c,it,v)
    local w,h=v.cw,86
    local col=opts.disabled and K.GREY or K.TEXT
    local bg=K.roundFill(c,0,0,w,h,K.NAVY,opts.disabled and .04 or .18,4)
    local edge=K.cutFrame(c,1,1,w-2,h-2,K.CYAN,opts.disabled and .1 or .22,10,1)
    local tw=w-48-(opts.rightW or 0)
    local txt=K.text(c,24,note and 6 or 0,tw,note and 44 or h,K.clip(label,opts.clip or 26),opts.size or 30,
      opts.selected and K.YELLOW or col)
    if note then K.text(c,24,48,tw,32,K.clip(note,34),22,opts.noteCol or K.GREY) end
    if not opts.disabled then
      local b=kit.button("p1",kit.at(v.x,it.y,v),0,0,w,h,click,function(hover)
        local lit=hover and K.YELLOW or (opts.selected and K.YELLOW or nil)
        bg:SetTintColor(K.color(lit or K.NAVY));bg:SetOpacity(hover and .16 or (opts.selected and .12 or .18))
        K.tint(edge,lit or K.CYAN,lit and 1 or .22)
        txt:SetTintColor(K.color(hover and K.WHITE or (opts.selected and K.YELLOW or col)))
      end)
      b.key=opts.key
    end
    if opts.extra then opts.extra(c,w,h,it,v) end
  end}
end

local function outfitRows(kit,s)
  local o=Wd.outfits();local rows={}
  rows[#rows+1]=row(kit,"+  "..K.upper(Wd.text("UI-Wardrobe-SaveSet")),nil,function()
    s.wardrobeDelete=nil;pending(kit,Wd.save())
  end,{disabled=not o.active or Wd.blocked(),size=24,clip=34,key="wardrobeSave"})
  rows[#rows+1]=row(kit,Wd.text("UI-Wardrobe-NoOutfit"),nil,function()
    s.wardrobeDelete=nil;pending(kit,Wd.deactivate())
  end,{selected=not o.active,key="wardrobeNone"})
  for i,e in ipairs(o.list) do
    local armed=s.wardrobeDelete==e.label
    rows[#rows+1]=row(kit,e.label,armed and Wd.text("UI-Wardrobe-Deleteset").."?" or nil,function()
      s.wardrobeDelete=nil;pending(kit,Wd.load(e.name))
    end,{selected=e.equipped,rightW=64,noteCol=K.RED,key="wardrobeOutfit"..i,extra=function(c,w,h,it,v)
      -- delete: the first press arms it, the second one deletes
      local x=w-70
      local col=armed and K.RED or K.CYAN
      local box=K.roundFill(c,x,14,56,h-28,col,armed and .18 or .06,4)
      local a=U.line(c,x+18,h/2-10,x+38,h/2+10,col,1,3);local b=U.line(c,x+38,h/2-10,x+18,h/2+10,col,1,3)
      kit.button("p1",kit.at(v.x,it.y,v),x,14,56,h-28,function()
        if armed then s.wardrobeDelete=nil;pending(kit,Wd.delete(e.name))
        else s.wardrobeDelete=e.label;C.redraw(kit) end
      end,function(hover)
        local lit=hover and (armed and K.RED or K.YELLOW) or col
        box:SetTintColor(K.color(lit));box:SetOpacity(hover and .3 or (armed and .18 or .06))
        a:SetTintColor(K.color(lit));b:SetTintColor(K.color(lit))
      end).key="wardrobeDelete"..i
    end})
  end
  return rows
end

local function clothingRows(kit,ctx)
  local rows={}
  local itemsLabel=K.upper(Wd.text("Story-base-journal-codex-tutorials-Itemization_groupName"))
  for gi,g in ipairs(Wd.groups(ctx)) do
    rows[#rows+1]={h=104,build=function(c,it,v)
      local w,h=v.cw,92
      local bg=K.roundFill(c,0,0,w,h,K.CYAN,g.collapsed and .05 or .10,4)
      local edge=K.cutFrame(c,1,1,w-2,h-2,K.CYAN,.3,10,1)
      local chev=kit.chevron(c,32,h/2,11,5,g.collapsed and "right" or "down",K.CYAN,1)
      K.text(c,62,6,w-280,44,K.clip(K.upper(g.name),28),28,K.CYAN)
      K.text(c,62,48,w-280,34,K.clip(g.equipped and g.equipped.name or Wd.text("UI-Labels-EmptySlot"),34),24,
        g.equipped and K.TEXT or K.GREY)
      K.text(c,w-232,0,208,h,itemsLabel..": "..#g.items,22,K.GREY,"right")
      kit.button("p1",kit.at(v.x,it.y,v),0,0,w,h,function() Wd.toggleSlot(g);C.redraw(kit) end,function(hover)
        bg:SetTintColor(K.color(hover and K.YELLOW or K.CYAN));bg:SetOpacity(hover and .16 or (g.collapsed and .05 or .10))
        K.tint(edge,hover and K.YELLOW or K.CYAN,hover and 1 or .3);K.tint(chev,hover and K.WHITE or K.CYAN,1)
      end).key="wardrobeSlot"..gi
    end}
    if not g.collapsed then
      for first=1,#g.items,COLS do
        local group={};for i=first,math.min(#g.items,first+COLS-1) do group[#group+1]=g.items[i] end
        rows[#rows+1]={h=TILE_H+12,build=function(c,it,v)
          local cw=math.floor((v.cw-(COLS-1)*10)/COLS)
          for i,item in ipairs(group) do
            tile(kit,c,kit.at(v.x,it.y,v),(i-1)*(cw+10),0,cw,TILE_H,item,nil,function() pending(kit,Wd.toggle(item)) end,
              item.equipped,"wardrobeItem:"..item.key)
          end
        end}
      end
    end
  end
  return rows
end

function V.draw(kit,p,ctx,s,back)
  U.title(p,K.upper(Wd.text("Gameplay-Devices-DisplayNames-Wardrobe")),W)
  U.headAction(kit,p,kit.P1X,W-72,L.tr("К СНАРЯЖЕНИЮ","EQUIPMENT"),back).key="wardrobeBack"

  K.text(p,24,96,450,48,K.upper(Wd.text("Story-base-gameplay-gui-fullscreen-inventory-inventory_screen-_localizationString15")),26,K.WHITE)
  local left=K.canvas(p,LEFT_X,0,LEFT_W,1305)
  C.list(kit,left,"p1","wardrobeOutfits",kit.P1X+LEFT_X,150,LEFT_W,outfitRows(kit,s),1060,nil)

  Preview.show(kit,p,490,100,560,1150)

  K.text(p,RIGHT_X+14,96,330,48,K.upper(Wd.text("Story-base-gameplay-gui-fullscreen-wardrobe-wardrobe_fullscreen-_localizationString6")),26,K.WHITE)
  local source=K.upper(Wd.source())
  local _,cont=string.gsub(source,"[\128-\191]","")
  local sw=math.min(340,math.floor((#source-cont)*22*.56)+44)
  U.action(kit,p,kit.P1X,RIGHT_X+RIGHT_W-14-sw,98,sw,source,function()
    pending(kit,Wd.nextSource());C.state(kit).resetList=true
  end,true,46,22).key="wardrobeSource"
  local right=K.canvas(p,RIGHT_X,0,RIGHT_W,1305)
  C.list(kit,right,"p1","p1",kit.P1X+RIGHT_X,160,RIGHT_W,clothingRows(kit,ctx),1060,kit.S.keepScroll):enableMask()
end
return V
end
