local K=require("modules/ink")
local L=require("modules/locale")
local D=require("modules/gameplay_data")
return function(C,U)
return function(kit,p)
  local s,t=C.state(kit);local clock=D.clock();s.clock=clock
  local w,cx,cy,r=900,450,430,220
  kit.glass(p,w,1130,kit.P1X,kit.P1Y);U.title(p,L.tr("ОЖИДАНИЕ","WAIT"),w)
  local function angle(h) return h/24*math.pi*2+math.pi/2 end
  local function time(h) return clock.hidden and "--:--" or string.format("%02d:%02d",h%24,clock.minute) end
  local atlas="base\\gameplay\\gui\\widgets\\time_skip\\atlas_time_skip.inkatlas"
  local function sun(x,y,size,col)
    return {K.image(p,x-size,y-size,size*2,size*2,atlas,"ico-day",col,1)}
  end
  local function moon(x,y,size)
    return {K.image(p,x-size,y-size,size*2,size*2,atlas,"ico-night",K.CYAN,1)}
  end
  K.image(p,cx-r-16,cy-r-16,(r+16)*2,(r+16)*2,atlas,"circle-fg",K.CYAN,.45)
  K.image(p,cx-r+22,cy-r+22,(r-22)*2,(r-22)*2,atlas,"inner-circle",K.CYAN,.4)
  local segments={}
  for i=0,95 do
    local a,b=angle(i/4),angle((i+.94)/4)
    segments[i+1]=U.line(p,cx+math.cos(a)*r,cy+math.sin(a)*r,cx+math.cos(b)*r,cy+math.sin(b)*r,K.CYAN,.65,10)
  end
  for h=0,23 do
    local a=angle(h);local outer=r+18;local inner=outer+(h%3==0 and 15 or 7)
    U.line(p,cx+math.cos(a)*outer,cy+math.sin(a)*outer,cx+math.cos(a)*inner,cy+math.sin(a)*inner,K.CYAN,.75,2)
  end
  sun(cx,cy-r-73,24,K.YELLOW);moon(cx,cy+r+73,23)
  for _,h in ipairs({6,18}) do
    local x=cx+math.cos(angle(h))*(r+73)
    K.image(p,x-26,cy-26,52,52,atlas,h==6 and "ico-morning" or "ico-evening",h==6 and K.CYAN or K.YELLOW,1)
  end
  local day=sun(cx,cy-112,27,K.YELLOW);local night=moon(cx,cy-112,26)
  local hours=K.text(p,cx-160,cy-54,320,94,"",72,K.CYAN,"center")
  local phase=K.text(p,cx-170,cy+52,340,42,"",28,K.GREY,"center")
  local nextDay=K.text(p,cx-170,cy+97,340,34,"",23,K.GREY,"center")
  K.text(p,70,787,310,34,L.tr("СЕЙЧАС","NOW"),25,K.GREY,"center")
  K.text(p,520,787,310,34,L.tr("ПОСЛЕ ОЖИДАНИЯ","AFTER WAITING"),25,K.GREY,"center")
  K.text(p,70,833,310,66,time(clock.hour),52,K.TEXT,"center")
  local target=K.text(p,520,833,310,66,"",52,K.CYAN,"center")
  U.line(p,416,866,477,866,K.CYAN,.8,3);U.line(p,466,854,478,866,K.CYAN,.8,3);U.line(p,478,866,466,878,K.CYAN,.8,3)
  local start=angle(clock.hour+clock.minute/60)
  K.image(p,cx+math.cos(start)*r-8,cy+math.sin(start)*r-8,16,16,K.ATLAS.phone,"circle",K.CYAN,1)
  local knob=K.image(p,0,0,34,34,K.ATLAS.phone,"circle",K.CYAN,1)
  local function update()
    local hour=(clock.hour+s.hours)%24;local daylight=hour>=6 and hour<20
    for _,i in ipairs(day) do i:SetVisible(daylight) end
    for _,i in ipairs(night) do i:SetVisible(not daylight) end
    hours:SetText(s.hours..(L.tr(" ч."," h")))
    local label=hour>=6 and hour<12 and (L.tr("Утро","Morning")) or hour>=12 and hour<18 and (L.tr("День","Day")) or hour>=18 and hour<22 and (L.tr("Вечер","Evening")) or (L.tr("Ночь","Night"))
    phase:SetText(label);nextDay:SetText(clock.hour+s.hours>=24 and t.tomorrow or "");target:SetText(time(clock.hour+s.hours))
    local a=angle(clock.hour+clock.minute/60+s.hours);K.move(knob,cx+math.cos(a)*r-17,cy+math.sin(a)*r-17)
    for i,segment in ipairs(segments) do
      local lit=((i-1)/4-clock.hour-clock.minute/60)%24<=s.hours
      segment:SetTintColor(K.color(lit and K.CYAN or K.CYAN));segment:SetOpacity(lit and 1 or .36)
    end
    s.clockPreview={hours=s.hours,hour=hour,phase=label,nextDay=clock.hour+s.hours>=24}
  end
  update()
  local b=kit.button("p1",kit.at(kit.P1X,kit.P1Y),cx-r-29,cy-r-29,(r+29)*2,(r+29)*2,nil,nil)
  b.contains=function(x,y) local radius=math.sqrt((x-r-29)^2+(y-r-29)^2);return radius>=r-26 and radius<=r+26 end
  b.drag=function(x,event,hand,y)
    if event=="end" or not x or not y then return end
    local theta=math.atan2(y-kit.P1Y-cy,x-kit.P1X-cx)-math.pi/2
    local delta=math.floor((theta/(2*math.pi)*24-clock.hour-clock.minute/60)%24+.5)
    s.hours=delta==0 and 24 or delta;update()
  end
  s.clockControl=b
  U.action(kit,p,kit.P1X,60,960,90,"-",function() s.hours=math.max(1,s.hours-1);update() end)
  U.action(kit,p,kit.P1X,750,960,90,"+",function() s.hours=math.min(24,s.hours+1);update() end)
  U.action(kit,p,kit.P1X,175,960,550,clock.allowed and t.confirm or t.blocked,function()
    if not D.clock().allowed or type(VRMenuBlink)~="function" then kit.toast(t.blocked);return end
    kit.close();kit.S.waitTransition={started=kit.S.time,hours=s.hours,phase="closing"};VRMenuCapture(true);VRMenuBlink(0)
  end,clock.allowed)
end
end
