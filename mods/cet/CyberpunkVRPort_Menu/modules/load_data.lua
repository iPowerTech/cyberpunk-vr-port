-- The engine owns save enumeration, compatibility metadata, thumbnails and loading.
-- Keep the request handler and listener alive until their asynchronous callbacks finish.
local D={}
local MARKER=-7392000
local function tagged(self) return self.sourceIndex and self.sourceIndex<=MARKER end
function D.bind(shared)
  D.shared=shared;shared.loadRouting=D
  if shared.loadRoutingBound then return end
  shared.loadRoutingBound=true
  Override("LoadGameMenuGameController","OnSavesForLoadReady",function(self,names,wrapped)
    if not tagged(self) then return wrapped(names) end
    local c=shared.loadContext
    if c and c.marker==self.sourceIndex then shared.loadRouting.names(c,names) end
    return true
  end)
  Override("LoadGameMenuGameController","OnSaveMetadataReady",function(self,info,wrapped)
    if not tagged(self) then return wrapped(info) end
    local c=shared.loadContext
    if c and c.marker==self.sourceIndex then shared.loadRouting.metadata(c,info) end
    return true
  end)
end
function D.open()
  local shared=D.shared;shared.loadEpoch=(shared.loadEpoch or 0)+1
  local c={handler=Game.GetSystemRequestsHandler(),listener=LoadGameMenuGameController.new(),
    marker=MARKER-shared.loadEpoch,records={},names={},images={},revision=0,received=0,loading=true}
  c.listener.sourceIndex=c.marker;shared.loadContext=c
  c.handler:RegisterToCallback(CName.new("OnSavesForLoadReady"),c.listener,CName.new("OnSavesForLoadReady"))
  c.handler:RegisterToCallback(CName.new("OnSaveMetadataReady"),c.listener,CName.new("OnSaveMetadataReady"))
  c.handler:RequestSavesForLoad()
  return c
end
function D.refresh(c)
  if c.loading or c.pending then return end
  c.loading=true;c.error=nil;c.timedOut=false;c.handler:RequestSavesForLoad()
end
function D.names(c,names)
  c.names=names;c.revision=c.revision+1
  if c.pending then
    local index
    for n,name in ipairs(names) do if name==c.pending.file then index=n-1;break end end
    if not index then c.error="changed";c.pending=nil;return end
    c.pending.index=index;c.validationImage=inkImageWidget.new()
    c.handler:RequestSavedGameScreenshot(index,c.validationImage)
    return
  end
  c.records={};c.images={};c.received=0;c.total=#names;c.loading=#names>0
  for n=1,#names do
    local img=inkImageWidget.new();c.images[n-1]=img
    c.handler:RequestSavedGameScreenshot(n-1,img)
  end
end
function D.metadata(c,info)
  if c.pending then
    local pending=c.pending
    if info.saveIndex~=pending.index then return end
    if info.saveID~=pending.id or tostring(info.timestamp)~=pending.stamp or not D.available(info) then
      c.error="changed";c.pending=nil
    else c.readyLoad=pending.index end
    c.revision=c.revision+1
    return
  end
  local index=info.saveIndex
  if not c.names[index+1] then return end
  if not c.records[index] then c.received=c.received+1 end
  c.records[index]=info;c.loading=c.received<c.total;c.revision=c.revision+1
end
function D.available(info)
  return info and info.isValid and (IsEP1() or not info:IsEp1Save())
end
function D.groups(c)
  local groups,map={},{}
  for index=0,(c.total or 0)-1 do
    local info=c.records[index]
    if info then
      local id=info.playthroughID
      if not id or id=="" then id="unknown:"..tostring(info.saveID) end
      local g=map[id]
      if not g then g={id=id,life=info.lifePath,level=0,seconds=0,stamp=info.timestamp,saves={}};map[id]=g;groups[#groups+1]=g end
      g.level=math.max(g.level,info.level)
      g.seconds=math.max(g.seconds,info.playTime,info.playthroughTime)
      if info.timestamp>g.stamp then g.stamp=info.timestamp end
      g.saves[#g.saves+1]=info
    end
  end
  local function newer(a,b)
    if a.timestamp~=b.timestamp then return a.timestamp>b.timestamp end
    return a.saveIndex<b.saveIndex
  end
  for _,g in ipairs(groups) do
    table.sort(g.saves,newer)
    local latest=g.saves[1]
    -- Match the game's SaveGroupInfo: the most recent save supplies the header statistics.
    g.seconds=math.max(latest.playTime,latest.playthroughTime);g.level=latest.level;g.life=latest.lifePath
  end
  table.sort(groups,function(a,b) if a.stamp~=b.stamp then return a.stamp>b.stamp end;return a.id<b.id end)
  return groups
end
function D.beginLoad(c,info)
  if c.pending or c.loading or not D.available(info) then return false end
  c.pending={file=c.names[info.saveIndex+1],id=info.saveID,stamp=tostring(info.timestamp)}
  c.error=nil;c.handler:RequestSavesForLoad()
  return true
end
function D.close(c)
  if not c or c.closed then return end
  c.closed=true
  c.handler:UnregisterFromCallback(CName.new("OnSavesForLoadReady"),c.listener,CName.new("OnSavesForLoadReady"))
  c.handler:UnregisterFromCallback(CName.new("OnSaveMetadataReady"),c.listener,CName.new("OnSaveMetadataReady"))
  c.handler:CancelSavesRequest();c.handler:CancelSavedGameScreenshotRequests()
  if D.shared.loadContext==c then D.shared.loadContext=nil end
end
return D
