local D=require("modules/gameplay_data")
local Jobs=require("modules/jobs")
local L={}

-- A codex entry: from the job's last list when it has it (a list read is ~7 ms), else read now.
function L.codex(hash)
  local list=Jobs.get("codex")
  if list then for _,entry in ipairs(list) do if entry.hash==hash then return entry end end end
  -- Not in the list yet: a quest link activates its entry when the quest is shown.
  local fresh,stamp=Jobs.now(D.codexList)
  Jobs.put("codex",fresh,stamp)
  for _,entry in ipairs(fresh) do if entry.hash==hash then return entry end end
  return nil
end

-- The shard groups as a job producer (returns a step function): the journal's shard list, then the
-- backpack's encrypted chips (700+ items late in a save), sixty items a step.
function L.shardList()
  local jm,groups,items,encrypted,i
  return function()
    if not groups then
      jm=Game.GetJournalManager();groups={};local byLevel={}
      local entries=CodexUtils.GetShardsDataArray(jm,nil)
      for _,item in ipairs(entries) do
        if item.isHeader then
          local group={key=item.level,name=D.loc(item.data.title),entries={}}
          groups[#groups+1]=group;byLevel[item.level]=group
        end
      end
      for _,item in ipairs(entries) do
        if not item.isHeader then
          local d=item.data;local group=byLevel[item.level]
          if group then group.entries[#group.entries+1]={hash=d.hash,name=D.loc(d.title),text=D.loc(d.description),fresh=d.isNew,crypted=false} end
        end
      end
      return false
    end
    if not items then
      local available,list=Game.GetTransactionSystem():GetItemList(Game.GetPlayer())
      items=available and list or {};i=0
      encrypted={key="encrypted",name=D.loc("LocKey#42347"),entries={}}
      return false
    end
    -- Encrypted chips remain encrypted and use their real inventory action to start a breach.
    local last=math.min(#items,i+60)
    while i<last do
      i=i+1
      local item=items[i];local id=item:GetID()
      local action=item:GetItemType()==gamedataItemType.Gen_Misc and not item:HasTag(CName.new("HideInBackpackUI")) and ItemActionsHelper.GetCrackAction(id) or nil
      if action then
        local effect=action:Effector()
        if effect and effect:IsA(CName.new("gamedataTriggerHackingMinigameEffector_Record")) then
          local path=effect:JournalEntry();local entry=jm:GetEntryByString(path,"gameJournalOnscreen")
          if entry and jm:GetEntryState(entry)==gameJournalEntryState.Inactive then
            encrypted.entries[#encrypted.entries+1]={hash=jm:GetEntryHash(entry),name=D.loc(entry:GetTitle()),
              text=CodexUtils.GetShardTextString(true,entry:GetDescription()),fresh=not jm:IsEntryVisited(entry),crypted=true,item=id}
          end
        end
      end
    end
    if i<#items then return false end
    if #encrypted.entries>0 then groups[#groups+1]=encrypted end
    local stamp={}
    for _,group in ipairs(groups) do
      stamp[#stamp+1]=tostring(group.key)
      for _,e in ipairs(group.entries) do stamp[#stamp+1]=tostring(e.hash)..(e.fresh and "*" or "")..(e.crypted and "!" or "") end
    end
    return true,groups,table.concat(stamp,"|")
  end
end

function L.shards()
  local groups=Jobs.get("shards")
  if groups then return groups end
  local stamp;groups,stamp=Jobs.now(L.shardList)
  Jobs.put("shards",groups,stamp)
  return groups
end

function L.findShard(hash)
  for _,group in ipairs(L.shards()) do for _,entry in ipairs(group.entries) do
    if entry.hash==hash then return entry,group end
  end end
end

function L.readShard(hash)
  local entry=L.findShard(hash)
  if not entry then return nil end
  -- Same operation as ShardsMenuGameController.OnShardSelectedEvent. Native journal listeners
  -- handle quest completion; never set quest facts or activate an undiscovered shard here.
  local jm=Game.GetJournalManager();local native=jm:GetEntry(hash)
  if native and not jm:IsEntryVisited(native) then jm:SetEntryVisited(native,true) end
  -- the list shows it as read at once; the next read of the journal agrees
  entry.fresh=false
  return entry
end

function L.crack(hash)
  local entry=L.findShard(hash)
  if not entry or not entry.crypted or not entry.item or not ItemActionsHelper.GetCrackAction(entry.item) then return false end
  Game.GetBlackboardSystem():Get(GetAllBlackboardDefs().HackingMinigame):SetBool(GetAllBlackboardDefs().HackingMinigame.IsJournalTarget,false)
  ItemActionsHelper.PerformItemAction(Game.GetPlayer(),entry.item)
  Jobs.drop("shards")
  return true
end
return L
