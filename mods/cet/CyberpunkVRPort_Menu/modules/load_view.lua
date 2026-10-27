local K=require("modules/ink")
local L=require("modules/locale")
local D=require("modules/load_data")
local I=require("modules/inventory_data")
return function(C,U)
local V={};local W,H=1780,1305
local ATLAS=[[base\gameplay\gui\fullscreen\load_game\save_game.inkatlas]]
local function tr(kit,ru,en) return L.tr(ru,en) end
local function counted(kit,n,one,few,many,en,enOther) return L.count(n,one,few,many,en,enOther) end
local function saveCount(kit,n) return counted(kit,n,"сохранение","сохранения","сохранений","save","saves") end
local function state(kit)
  local g=C.state(kit)
  g.load=g.load or {ctx=D.open(),expanded={},seenRevision=-1,started=kit.S.time}
  return g.load
end
local function life(kit,value)
  if value==inkLifePath.Nomad then return tr(kit,"КОЧЕВНИК","NOMAD"),"LifepathNomad1" end
  if value==inkLifePath.Corporate then return tr(kit,"КОРПОРАТ","CORPO"),"LifepathCorpo1" end
  if value==inkLifePath.StreetKid then return tr(kit,"ДИТЯ УЛИЦ","STREETKID"),"LifepathStreetKid1" end
  return tr(kit,"ПРОХОЖДЕНИЕ","PLAYTHROUGH"),nil
end
local function duration(kit,seconds)
  local minutes=math.floor(math.max(0,seconds)/60);local hours=math.floor(minutes/60)
  return hours>0 and (hours..tr(kit," ч. "," h ")..(minutes%60)..tr(kit," мин."," min")) or (minutes..tr(kit," мин."," min"))
end
local function date(p,x,y,w,h,timestamp,size)
  local label=K.text(p,x,y,w,h,"",size or 24,K.CYAN,"right")
  label:SetDateTimeByTimestamp(timestamp)
  return label
end
local function preview(s,p,index,x,y,w,h)
  local img=s.ctx.images[index]
  K.roundFill(p,x,y,w,h,K.BLACK,.8,5)
  if not img then return end
  img:SetAnchor(inkEAnchor.TopLeft);img:SetAnchorPoint(Vector2.new({X=0,Y=0}))
  img:SetRenderTransformPivot(Vector2.new({X=0,Y=0}));img:SetScale(Vector2.new({X=1,Y=1}))
  K.move(img,x,y);K.size(img,w,h);img:SetOpacity(1);img:SetVisible(true);img:Reparent(p)
end
local function action(kit,p,x,y,w,ru,en,fn,enabled,key)
  local b=U.action(kit,p,kit.P1X,x,y,w,tr(kit,ru,en),fn,enabled);b.key=key;return b
end
local function headAction(kit,p,x1,ru,en,fn,enabled,key)
  local b=U.headAction(kit,p,kit.P1X,x1,tr(kit,ru,en),fn,enabled);b.key=key;return b
end
local function confirm(kit,p,s,info)
  local name=I.loc(info.trackedQuest)
  preview(s,p,info.saveIndex,80,135,400,225)
  K.text(p,520,155,1150,60,K.clip(name,62),38,K.WHITE)
  K.text(p,520,230,1100,50,info.internalName.."  /  "..duration(kit,math.max(info.playTime,info.playthroughTime)),29,K.CYAN)
  date(p,520,295,1100,44,info.timestamp,27)
  local isMods=s.confirm=="mods"
  local heading=isMods and tr(kit,"ПРОВЕРКА МОДИФИКАЦИЙ","MOD VERIFICATION") or tr(kit,"ЗАГРУЗИТЬ ЭТО СОХРАНЕНИЕ?","LOAD THIS SAVE?")
  K.roundFill(p,80,425,W-160,520,K.BLACK,1,16)
  K.cutFrame(p,80,425,W-160,520,K.YELLOW,.8,20,2)
  K.text(p,125,460,W-250,65,heading,40,K.YELLOW)
  K.rect(p,125,545,W-250,2,K.CYAN,.5)
  local message=isMods and tr(kit,"Этот файл сохранения создан в модифицированной версии игры.\nЕго загрузка может привести к ошибкам в этой системе.\nПродолжить?","This save was created in a modified version of the game.\nLoading it may cause errors on this system.\nContinue?")
    or tr(kit,"Несохранённый прогресс будет потерян.","Unsaved progress will be lost.")
  K.text(p,125,590,W-250,215,message,35,K.WHITE,nil,true)
  if s.ctx.pending then
    K.text(p,125,840,W-250,65,tr(kit,"Проверка сохранения…","Checking save…"),32,K.CYAN,"center")
  else
    action(kit,p,740,840,440,"ПОДТВЕРДИТЬ","CONFIRM",function()
      if not isMods and info.isModded then s.confirm="mods";C.redraw(kit)
      elseif D.beginLoad(s.ctx,info) then s.loadAt=kit.S.time;C.redraw(kit)
      else kit.toast(kit.T.blocked) end
    end,D.available(info) and not s.ctx.loading,"loadConfirm")
    action(kit,p,1210,840,440,"ОТМЕНА","CANCEL",function() s.confirm=nil;s.selection=nil;C.redraw(kit) end,true,"loadCancel")
  end
end
function V.draw(kit,p,p2)
  local s=state(kit);local c=s.ctx
  kit.glass(p,W,H,kit.P1X,kit.P1Y);U.title(p,tr(kit,"ЗАГРУЗИТЬ ИГРУ","LOAD GAME"),W)
  local refresh=headAction(kit,p,W-72,"ОБНОВИТЬ","REFRESH",function() s.confirm=nil;s.selection=nil;s.started=kit.S.time;D.refresh(c);C.redraw(kit) end,not c.loading and not c.pending,"loadRefresh")
  headAction(kit,p,refresh.x-kit.P1X-12,"МЕНЮ","MENU",function() kit.openSection("game") end,not c.pending,"loadBack")
  local info=c.records[s.selection]
  if s.confirm and info then confirm(kit,p,s,info);return end
  local groups=D.groups(c);s.groups=groups
  if not s.initialGroup and not c.loading and #groups>0 then s.expanded[groups[1].id]=true;s.initialGroup=true end
  local rows={}
  for _,group in ipairs(groups) do
    local opened=s.expanded[group.id]==true
    rows[#rows+1]={h=130,build=function(row,it,view)
      local w=view.cw-8;local name,part=life(kit,group.life)
      local bg=K.roundFill(row,4,6,w,112,K.ICE,.08,9)
      local edge=K.roundEdge(row,4,6,w,112,K.CYAN,.45,9)
      if part then K.image(row,20,18,84,84,ATLAS,part,K.CYAN,.8) end
      K.text(row,120,18,385,48,name,33,K.WHITE)
      K.text(row,120,69,350,35,saveCount(kit,#group.saves),23,K.GREY)
      K.text(row,525,20,w-620,42,tr(kit,"Время в игре ","Play time ")..duration(kit,group.seconds).."    "..tr(kit,"Макс. уровень ","Max level ")..math.floor(group.level),28,K.CYAN)
      K.text(row,525,69,410,35,tr(kit,"Последнее сохранение","Last saved"),24,K.GREY)
      date(row,w-605,69,485,35,group.stamp)
      kit.chevron(row,w-45,60,14,3,opened and "down" or "up",K.CYAN,1)
      local b=kit.button("p1",kit.at(view.x,it.y,view),4,6,w,112,function() s.expanded[group.id]=not opened;C.redraw(kit) end,function(h) bg:SetOpacity(h and .17 or .08);K.tint(edge,K.CYAN,h and 1 or .45) end)
      b.key="loadGroup:"..group.id
    end}
    if opened then for _,save in ipairs(group.saves) do
      rows[#rows+1]={h=190,build=function(row,it,view)
        local w=view.cw-28;local available=D.available(save)
        local bg=K.roundFill(row,20,5,w,177,K.ICE,.035,8)
        local edge=K.roundEdge(row,20,5,w,177,available and K.CYAN or K.GREY,.45,8)
        preview(s,row,save.saveIndex,28,14,260,146)
        local title=save.isValid and I.loc(save.trackedQuest) or tr(kit,"Повреждённое сохранение","Corrupted save")
        if save.isValid and not available then title=tr(kit,"Требуется Phantom Liberty","Phantom Liberty required") end
        K.text(row,310,16,w-675,48,K.clip(title,58),34,available and K.WHITE or K.GREY)
        if save.isModded then kit.glyph(row,w-320,40,54,kit.GLYPH.cog,K.CYAN,1) end
        K.text(row,w-285,20,275,38,tr(kit,"Версия ","Version ")..save.gameVersion,23,K.GREY,"right")
        local path=life(kit,save.lifePath)
        K.text(row,310,79,w-330,40,duration(kit,math.max(save.playTime,save.playthroughTime)).."   /   "..path.."   /   "..tr(kit,"Уровень ","Level ")..math.floor(save.level).."   /   "..K.clip(I.loc(save.locationName),40),25,K.CYAN)
        K.text(row,310,130,760,32,save.internalName,24,K.GREY)
        date(row,w-490,130,480,32,save.timestamp)
        local b=kit.button("p1",kit.at(view.x,it.y,view),20,5,w,177,function()
          if not available or c.loading then kit.toast(kit.T.blocked);return end
          s.selection=save.saveIndex;s.confirm="load";C.redraw(kit)
        end,function(h) bg:SetOpacity(h and .14 or .035);K.tint(edge,h and K.CYAN or available and K.CYAN or K.GREY,h and 1 or .45) end)
        b.key="loadSave:"..save.saveID
      end}
    end end
  end
  -- While the engine still sends saves the list grows as they arrive: no reading line meanwhile.
  if #rows>0 then C.list(kit,p,"p1","p1",kit.P1X,115,W,rows,1050,s.offset):enableMask()
  elseif not c.loading then K.text(p,30,450,W-60,120,c.timedOut and tr(kit,"Не удалось прочитать сохранения. Нажмите «Обновить».","Could not read saves. Select Refresh.") or tr(kit,"Нет сохранений","No saves found"),36,K.CYAN,"center") end
  if not (c.loading and #groups==0) then
    K.text(p,24,1232,W-48,45,c.timedOut and not c.loading and tr(kit,"Не все данные получены. Нажмите «Обновить».","Some data is missing. Select Refresh.") or (counted(kit,#groups,"прохождение","прохождения","прохождений","playthrough","playthroughs").."  /  "..saveCount(kit,c.received)),26,K.GREY)
  end
end
function V.update(kit)
  local s=C.state(kit).load;if not s then return end
  local c=s.ctx
  if c.readyLoad~=nil then
    local index=c.readyLoad;c.readyLoad=nil
    kit.loadSave(c.handler,index)
    return
  end
  if c.error then
    c.error=nil;c.pending=nil;s.confirm=nil;s.selection=nil
    kit.toast(tr(kit,"Список сохранений изменился. Выберите сохранение снова.","Saves changed. Select the save again."))
    s.started=kit.S.time;D.refresh(c)
  end
  if c.pending and kit.S.time-(s.loadAt or kit.S.time)>12 then
    c.pending=nil;s.confirm=nil;s.selection=nil;kit.toast(tr(kit,"Не удалось проверить сохранение","Could not verify the save"));s.started=kit.S.time;D.refresh(c)
  end
  if c.loading and kit.S.time-s.started>15 then c.loading=false;c.timedOut=true;c.revision=c.revision+1 end
  if c.revision~=s.seenRevision and kit.S.time-(s.drawAt or 0)>.2 then
    s.seenRevision=c.revision;s.drawAt=kit.S.time;C.redraw(kit)
  end
end
function V.beforeBuild(kit)
  local s=C.state(kit).load
  if s and kit.S.views.p1 then s.offset=kit.S.views.p1.offset end
end
function V.leave(kit) local g=C.state(kit);if g.load then D.close(g.load.ctx);g.load=nil end end
return V
end
