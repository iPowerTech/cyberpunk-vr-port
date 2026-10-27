-- Original game tooltip widgets, shared by exchange panels on the world-space canvas.
local K=require("modules/ink")
local T={}
-- `backed`: a black plate behind the tooltip, sized to it once it has laid out (T.fit) -- for a tooltip over other
-- content; a plate drawn at the full box before that stood empty around a smaller tooltip.
function T.spawn(kit,p,ui,context,x,y,w,h,compare,price,backed)
  local data=UIInventoryItemTooltipWrapper.Make(ui,ItemDisplayContextData.Make(Game.GetPlayer(),context,false))
  if compare then data.comparisonData=UIInventoryItemComparisonManager.Make(ui,compare) end
  if price then data.overridePrice=price end
  local path,library=[[base\gameplay\gui\common\tooltip\itemtooltip.inkwidget]],"itemTooltip"
  if ui:IsWeapon() then path,library=[[base\gameplay\gui\common\tooltip\new_itemtooltip.inkwidget]],"itemTooltip"
  elseif ui:GetItemData():HasTag(CName.new("Cyberdeck")) then path,library=[[base\gameplay\gui\common\tooltip\cyberdecktooltip.inkwidget]],"cyberdeckTooltip"
  elseif ui:GetItemType()==gamedataItemType.Prt_Program then path,library=[[base\gameplay\gui\common\tooltip\programtooltip.inkwidget]],"programTooltip" end
  local back=backed and K.canvas(p,0,0,10,10) or nil
  local widget=kit.menuController():SpawnFromExternal(p,ResRef.FromString(path),CName.new(library))
  if not widget then return nil end
  widget:SetAnchor(inkEAnchor.TopLeft);widget:SetAnchorPoint(Vector2.new({X=0,Y=0}));widget:SetRenderTransformPivot(Vector2.new({X=0,Y=0}))
  widget:SetScale(Vector2.new({X=.001,Y=.001}));widget:SetOpacity(.001)
  K.move(widget,x,y);widget:GetController():SetData(data)
  return {widget=widget,x=x,y=y,w=w,h=h,back=back}
end
-- The weapon tooltip widget given ready tooltip data (a MinimalItemTooltipData snapshot), as the upgrade screen
-- feeds it: what it shows is read when the data is made, not when the widget draws.
function T.spawnData(kit,p,data,x,y,w,h)
  local widget=kit.menuController():SpawnFromExternal(p,ResRef.FromString([[base\gameplay\gui\common\tooltip\new_itemtooltip.inkwidget]]),CName.new("itemTooltip"))
  if not widget then return nil end
  widget:SetAnchor(inkEAnchor.TopLeft);widget:SetAnchorPoint(Vector2.new({X=0,Y=0}));widget:SetRenderTransformPivot(Vector2.new({X=0,Y=0}))
  widget:SetScale(Vector2.new({X=.001,Y=.001}));widget:SetOpacity(.001)
  K.move(widget,x,y);widget:GetController():SetData(data)
  return {widget=widget,x=x,y=y,w=w,h=h}
end
function T.fit(tips)
  for _,t in ipairs(tips or {}) do local z=t.widget:GetDesiredSize();if z.X>1 and z.Y>1 then
    local scale=math.min(t.w/z.X,t.h/(z.Y+65));t.widget:SetScale(Vector2.new({X=scale,Y=scale}));K.move(t.widget,t.x,t.y+35*scale);t.widget:SetOpacity(1)
    local bw,bh=math.floor(z.X*scale+.5),math.floor(z.Y*scale+.5)
    if t.back and (bw~=t.backW or bh~=t.backH) then
      t.backW,t.backH=bw,bh;t.back:RemoveAllChildren()
      K.roundFill(t.back,t.x-10,t.y+35*scale-10,bw+20,bh+20,K.BLACK,1,8)
    end
  end end
end
return T
