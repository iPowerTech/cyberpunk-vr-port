local K=require("modules/ink")
local L=require("modules/locale")
local R=require("modules/reference_data")
return function(C,U)
local draw
draw=function(kit,p)
  local ok,data=pcall(R.summary)
  if not ok then print("[VRMenu] summary: "..tostring(data));return end
  local s=C.state(kit);s.summary=data;s.summaryAt=kit.S.time
  kit.clearSummaryButtons();p:RemoveAllChildren()
  local x,y,w=270,250,640
  local expanded=s.summaryExpanded==true
  local extra=0
  if expanded then for _,row in ipairs(data.other) do extra=extra+math.max(30,#C.wrap(row.name,32)*27) end;extra=extra+12 end
  local h=590+extra
  kit.S.summaryRect=kit.panelRect(x,y,w,h)
  kit.glass(K.canvas(p,x,y,w,h),w,h)
  K.text(p,x+24,y+8,w-48,40,"V",34,K.WHITE)
  local rows={{"gun",L.tr("УРОН В СЕКУНДУ","DAMAGE PER SECOND"),data.dps},
    {"armor",L.tr("БРОНЯ","ARMOR"),data.armor},{"health",L.tr("ЗДОРОВЬЕ","HEALTH"),data.health}}
  for i,row in ipairs(rows) do
    local yy=y+58+(i-1)*50
    K.image(p,x+20,yy+5,32,32,K.ATLAS.mappin,row[1],K.CYAN,1)
    K.text(p,x+66,yy,w-190,42,row[2],26,K.TEXT)
    K.text(p,x+w-120,yy,96,42,tostring(math.floor(row[3]+.5)),29,K.CYAN,"right")
    K.rect(p,x+20,yy+45,w-40,1,K.CYAN,.14)
  end
  local more=K.roundFill(p,x+18,y+216,w-36,38,K.NAVY,.18,3)
  local moreText=K.text(p,x+30,y+216,w-60,38,(expanded and "- " or "+ ")..(L.tr("ДРУГОЕ","OTHER")),24,K.TEXT)
  kit.button("summary",kit.at(0,0),x+18,y+216,w-36,38,function() s.summaryExpanded=not expanded;draw(kit,p) end,function(hov)
    more:SetTintColor(K.color(hov and K.YELLOW or K.NAVY));more:SetOpacity(hov and .14 or .18)
    moreText:SetTintColor(K.color(hov and K.YELLOW or K.TEXT))
  end)
  if expanded then
    local yy=y+264
    for _,row in ipairs(data.other) do
      local names=C.wrap(row.name,32);local rh=math.max(30,#names*27)
      K.text(p,x+26,yy,110,30,row.value,24,K.CYAN)
      K.text(p,x+145,yy,w-169,rh,table.concat(names,"\n"),23,K.TEXT)
      yy=yy+rh
    end
  end
  local life=({Nomad=L.tr("КОЧЕВНИК","NOMAD"),StreetKid=L.tr("ДИТЯ УЛИЦ","STREETKID"),Corporate=L.tr("КОРПОРАТ","CORPO")})[data.life] or data.life
  local played=string.format("%02d:%02d",math.floor(data.seconds/3600),math.floor(data.seconds/60)%60)
  K.text(p,x+22,y+270+extra,w-44,36,played.."  /  "..life,24,K.TEXT)
  for i,progress in ipairs(data.progress) do
    local yy=y+324+extra+(i-1)*97
    local label=i==1 and (L.tr("УРОВЕНЬ","LEVEL")) or (L.tr("РЕПУТАЦИЯ","STREET CRED"))
    local ratio=progress.level>=progress.max and 1 or progress.xp/math.max(1,progress.total)
    K.text(p,x+22,yy,315,38,progress.level.."  "..label,28,K.CYAN)
    K.text(p,x+340,yy+3,w-362,32,progress.level>=progress.max and (L.tr("МАКС.","MAX")) or progress.xp.."/"..progress.total,21,K.GREY,"right")
    K.rect(p,x+22,yy+48,w-44,4,K.CYAN,.18);K.rect(p,x+22,yy+48,(w-44)*ratio,4,K.CYAN,1)
    K.text(p,x+22,yy+56,w-44,25,math.floor(ratio*100).."%",20,K.CYAN,"right")
  end
  local hint=K.text(p,x+22,y+h+12,w-44,82,"",24,K.CYAN)
  for i,reward in ipairs(data.rewards) do
    local xx=x+22+(i-1)*66;local col=data.progress[2].level>=reward.level and K.CYAN or K.GREY
    local bg=K.roundFill(p,xx,y+529+extra,58,42,col,.08,6)
    K.text(p,xx,y+529+extra,58,42,tostring(reward.level),22,col,"center")
    local function show() s.summaryHintOwner=reward.level;hint:SetText(table.concat(C.wrap(reward.text,42),"\n"));kit.S.summaryRect=kit.panelRect(x,y,w,h+105) end
    kit.button("summary",kit.at(0,0),xx,y+529+extra,58,42,show,function(hov)
      bg:SetOpacity(hov and .24 or .08)
      if hov then show() elseif s.summaryHintOwner==reward.level then s.summaryHintOwner=nil;hint:SetText("");kit.S.summaryRect=kit.panelRect(x,y,w,h) end
    end)
  end
end
return draw
end
