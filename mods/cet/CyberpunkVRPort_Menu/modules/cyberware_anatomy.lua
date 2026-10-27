local K=require("modules/ink")
local Layout=require("modules/cyberware_anatomy_layout")
local A={}
local names={FrontalCortexCW={"ma_pfrontal_cortex","wo_frontal_cortex"},SystemReplacementCW={"ma_operating_system","wo_operating_system"},
  ArmsCW={"ma_arms","wo_arms"},EyesCW={"ma_ocular","wo_ocular"},MusculoskeletalSystemCW={"ma_full_skeleton","wo_skeleton"},
  HandsCW={"ma_hands","wo_hands"},NervousSystemCW={"ma_nervous","wo_nervous"},CardiovascularSystemCW={"ma_circular","wo_circular"},
  IntegumentarySystemCW={"ma_intergumentary","wo_intergumentary"},LegsCW={"ma_legs","wo_legs"}}
local focus={FrontalCortexCW={.5,.13,3.2},SystemReplacementCW={.5,.21,2},ArmsCW={.5,.36,1.4},EyesCW={.5,.14,3.2},
  MusculoskeletalSystemCW={.5,.43,1.25},HandsCW={.5,.48,1.4},NervousSystemCW={.5,.35,1.65},
  CardiovascularSystemCW={.5,.30,1.65},IntegumentarySystemCW={.5,.40,1.3},LegsCW={.5,.76,1.5}}
function A.new(parent,x,y,w,h)
  local a={w=w,h=h,parts={},female=Game.GetPlayer():GetResolvedGenderName()==CName.new("Female")}
  a.layout=Layout[a.female and "woman" or "man"];a.base=a.female and "wo_intergumentary" or "ma_intergumentary"
  a.bw,a.bh=a.female and 735 or 1089,a.female and 1781 or 1861
  local mask=inkScrollAreaWidget.new();mask:SetAnchor(inkEAnchor.TopLeft);mask:SetAnchorPoint(Vector2.new({X=0,Y=0}))
  mask:SetMargin(inkMargin.new({left=x,top=y,right=0,bottom=0}));mask:SetSize(Vector2.new({X=w,Y=h}))
  mask:SetFitToContent(false);mask:SetUseInternalMask(true);mask.constrainContentPosition=false;mask.fitToContentDirection=inkFitToContentDirection.None;mask:Reparent(parent)
  a.content=K.canvas(mask,0,0,w,h)
  local atlas=[[base\gameplay\gui\fullscreen\ripperdoc\]]..(a.female and "woman_body" or "man_body")..".inkatlas"
  a.body=K.image(a.content,0,0,a.bw,a.bh,atlas,a.base,K.WHITE,1)
  for area,parts in pairs(names) do
    local part=parts[a.female and 2 or 1]
    if part~=a.base then
      local img=K.image(a.content,0,0,1,1,atlas,part,K.CYAN,0);img:SetFitToContent(true)
      a.parts[area]={widget=img,part=part}
    end
  end
  a.scale=math.min(w/a.bw,h/a.bh);a.zoom={.5,.5,1};a.target=a.zoom
  return a
end
function A.focus(a,area,zoom)
  a.area=area;a.target=zoom and focus[area] or {.5,.5,1};a.target=a.target or {.5,.5,1}
end
function A.update(a,dt)
  local base=a.layout[a.base]
  for area,p in pairs(a.parts) do
    if not p.fitted then
      local size=p.widget:GetDesiredSize()
      if size.X>1 and size.Y>1 then
        local source=a.layout[p.part];local scale=source[3]/base[3]
        p.widget:SetFitToContent(false)
        p.w,p.h=size.X*scale,size.Y*scale
        p.x,p.y=a.bw/2+(source[1]-base[1])/base[3]-p.w/2,a.bh/2+(source[2]-base[2])/base[3]-p.h/2
        a.placed=false
        p.fitted=true
      end
    end
    p.widget:SetOpacity(a.area==area and 1 or 0)
  end
  a.body:SetOpacity(a.area and a.area~="IntegumentarySystemCW" and .35 or .85)
  local moved=false
  for i=1,3 do local delta=a.target[i]-a.zoom[i];if math.abs(delta)>.0005 then a.zoom[i]=a.zoom[i]+delta*math.min(1,dt*12);moved=true else a.zoom[i]=a.target[i] end end
  if moved or not a.placed then
    local scale=a.scale*a.zoom[3]
    local x,y=a.w/2-a.zoom[1]*a.bw*scale,a.h/2-a.zoom[2]*a.bh*scale
    K.move(a.body,x,y);K.size(a.body,a.bw*scale,a.bh*scale)
    for _,p in pairs(a.parts) do if p.fitted then K.move(p.widget,x+p.x*scale,y+p.y*scale);K.size(p.widget,p.w*scale,p.h*scale) end end
    a.placed=true
  end
end
return A
