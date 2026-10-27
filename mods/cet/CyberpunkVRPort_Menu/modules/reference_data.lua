-- Presentation data from the same records and journal methods used by the game's hub.
local D = require("modules/gameplay_data")
local R = {}
local ATTRIBUTE_ATLAS = "base\\gameplay\\gui\\common\\icons\\attributes_icons.inkatlas"

function R.params(text, package)
  if not package then return D.loc(text) end
  local values = {}
  for _, field in ipairs({{"float","floatValues"},{"int","intValues"},{"stat","statValues"}}) do
    for i,v in ipairs(package[field[2]]) do values[field[1].."_"..(i-1)]=string.format("%g",v) end
  end
  for i,v in ipairs(package.nameValues) do values["name_"..(i-1)]=D.loc(NameToString(v)) end
  for i,v in ipairs(package.statNames) do values["stat_"..(i-1)]=values["stat_"..(i-1)] or D.loc(NameToString(v)) end
  return (D.loc(text):gsub("{([%w_]+)[^}]*}",function(key) return values[key] or "" end))
end

function R.character()
  local d=D.character()
  for _,a in ipairs(d.attributes) do
    a.icon={atlas=ATTRIBUTE_ATLAS,part=NameToString(PerkAttributeHelper.GetIconAtlasPart(PerkAttributeHelper.BaseStatToPerkMenuAttribute(a.type)))}
    if a.relic then a.icon={atlas="base\\gameplay\\gui\\fullscreen\\perks\\atlas_perks.inkatlas",part="ico_atrib_relic"} end
  end
  local manager=PlayerDevelopmentDataManager.new();manager:Initialize(Game.GetPlayer(),nil)
  for i,kind in ipairs({"CoolSkill","IntelligenceSkill","ReflexesSkill","StrengthSkill","TechnicalAbilitySkill"}) do
    local skill=d.skills[i]
    local info=manager:GetProficiencyDisplayData(gamedataProficiencyType[kind])
    skill.max=info.maxLevel;skill.rewards={}
    for _,reward in ipairs(info.passiveBonusesData) do
      if reward.level%5==0 then
        skill.rewards[#skill.rewards+1]={level=reward.level,text=R.params(reward.description,reward.locPackage),unlocked=skill.level>=reward.level}
      end
    end
  end
  return d
end

function R.perks(attribute)
  local perks=D.perks(attribute)
  for _,p in ipairs(perks) do
    p.key=tostring(p.type);p.parents={}
    for _,parent in ipairs(p.record:RequiresPerks()) do p.parents[#p.parents+1]=tostring(parent:Type()) end
    p.slot=NameToString(p.record:Slot():EnumName())
    p.icon=D.image(p.record:PerkIcon():GetID())
    p.video=p.record:BinkPath()
  end
  return perks
end

function R.summary()
  local player=Game.GetPlayer();local stats=Game.GetStatsSystem()
  local function stat(kind) return stats:GetStatValue(player:GetEntityID(),gamedataStatType[kind]) end
  local dev=PlayerDevelopmentSystem.GetData(player)
  local out={health=stat("Health"),armor=stat("Armor"),dps=stat("EffectiveDPS"),other={}}
  local record=TweakDBInterface.GetUIStatsMapRecord(TweakDBID.new("UIMaps.Player_Stat_Panel_Other"))
  for _,r in ipairs(record:PrimaryStats()) do
    local v=stats:GetStatValue(player:GetEntityID(),r:StatType())
    local kind=r:StatType()
    local percent=kind==gamedataStatType.Visibility or kind==gamedataStatType.MemoryRegenRate
      or kind==gamedataStatType.FallDamageReduction or kind==gamedataStatType.IntrinsicQuickHackUploadBonus
    if v~=0 then
      out.other[#out.other+1]={name=D.loc(r:LocalizedName()),value=percent and string.format("%.0f%%",v*100) or string.format("%.2f",v):gsub("%.?0+$","")}
    end
  end
  out.progress={}
  for _,kind in ipairs({"Level","StreetCred"}) do
    local p=gamedataProficiencyType[kind]
    local xp=dev:GetCurrentLevelProficiencyExp(p)
    local remaining=dev:GetRemainingExpForLevelUp(p)
    out.progress[#out.progress+1]={level=dev:GetProficiencyLevel(p),max=dev:GetProficiencyAbsoluteMaxLevel(p),xp=xp,total=xp+remaining}
  end
  out.life=tostring(PlayerDevelopmentSystem.GetInstance(player):GetLifePath(player)):match(": (%w+)") or ""
  out.seconds=EngineTime.ToFloat(Game.GetPlaythroughTime())
  out.rewards={}
  for _,reward in ipairs(SteetCredDataHelper.GetData()) do
    out.rewards[#out.rewards+1]={level=reward.level,text=D.loc(reward.description)}
  end
  return out
end

function R.quests()
  local w=JournalWrapper.new();w:Init()
  local jm=Game.GetJournalManager();local result=D.quests()
  for _,q in ipairs(result) do
    q.story=jm:GetQuestType(q.entry)==gameJournalQuestType.MainQuest
    q.at=GameTime.GetSeconds(jm:GetEntryTimestamp(q.entry))
    local data=w:BuildQuestData(q.entry)
    q.objective="";q.distance=nil
    for _,o in ipairs(data:GetObjectives()) do
      if o:IsActive() then
        if q.objective=="" or o:IsTrackedInHierarchy() then q.objective=D.loc(o:GetDescription()) end
        local distance=jm:GetDistanceToNearestMappin(o:GetQuestObjective(),QuestLogUtils.GetDefaultFilter())
        if distance>0 and (q.distance==nil or distance<q.distance) then q.distance=distance end
      end
    end
  end
  table.sort(result,function(a,b)
    if a.story~=b.story then return a.story end
    if a.at~=b.at then return a.at>b.at end
    return a.name<b.name
  end)
  return result
end

-- How a distance reads in a quest row; the list's stamp keeps this, not the metres, so walking
-- redraws a row only when its text would change.
function R.distanceKey(d)
  if not d then return "" end
  return d>=1000 and string.format("%.1fk",d/1000) or tostring(math.floor(d+.5))
end

-- The quest list as a job producer (modules/jobs.lua: returns a step function). BuildQuestData builds a
-- quest's whole objective tree (~0.15 ms each, 150+ quests late in a save), so it runs for active quests
-- only -- the row's objective line and distance come from active objectives; a finished quest needs its
-- journal fields. Done with the list and its stamp; list.structure is the stamp without the distances (a
-- change there needs a redraw, a distance change only a row's text).
function R.questList()
  local jm,w,quests,out,i
  return function()
    if not quests then
      jm=Game.GetJournalManager();w=JournalWrapper.new();w:Init()
      quests=w:GetQuests();out={};i=0
      return false
    end
    -- a step: one active quest (its objective tree), or up to eight finished ones
    local cost=0
    while i<#quests and cost<8 do
      i=i+1
      local e=quests[i]
      local state=jm:GetEntryState(e)
      local q={hash=jm:GetEntryHash(e),name=D.loc(e:GetTitle(jm)),state=state,kind=tostring(e:GetType()),
        story=jm:GetQuestType(e)==gameJournalQuestType.MainQuest,at=GameTime.GetSeconds(jm:GetEntryTimestamp(e)),
        objective="",tracked=false}
      if state==gameJournalEntryState.Active then
        local data=w:BuildQuestData(e)
        q.tracked=data:IsTrackedInHierarchy()
        for _,o in ipairs(data:GetObjectives()) do
          if o:IsActive() then
            if q.objective=="" or o:IsTrackedInHierarchy() then q.objective=D.loc(o:GetDescription()) end
            local distance=jm:GetDistanceToNearestMappin(o:GetQuestObjective(),QuestLogUtils.GetDefaultFilter())
            if distance>0 and (q.distance==nil or distance<q.distance) then q.distance=distance end
          end
        end
        cost=cost+8
      else cost=cost+1 end
      out[#out+1]=q
    end
    if i<#quests then return false end
    table.sort(out,function(a,b)
      if a.story~=b.story then return a.story end
      if a.at~=b.at then return a.at>b.at end
      return a.name<b.name
    end)
    local structure,distances={},{}
    for _,q in ipairs(out) do
      structure[#structure+1]=tostring(q.hash)..":"..tostring(q.state)..":"..tostring(q.tracked)..":"..q.name..":"..q.objective
      distances[#distances+1]=R.distanceKey(q.distance)
    end
    out.structure=table.concat(structure,"|")
    return true,out,out.structure.."#"..table.concat(distances,"|")
  end
end

-- One quest's detail panel (description, objectives, message/shard actions, codex links), as a job
-- producer in three steps. R.links activates the linked codex entries, as the native details panel does
-- when it shows the quest -- so this runs only for the quest the player selected.
function R.questDetail(hash)
  local phase,q,actions=0
  return function()
    phase=phase+1
    if phase==1 then
      q=D.quest(hash)
      if not q then return true,false,"none" end
      return false
    elseif phase==2 then actions=R.actions(hash);return false end
    local links=R.links(hash)
    local stamp={q.name,q.description}
    for _,o in ipairs(q.objectives) do stamp[#stamp+1]=tostring(o.hash)..o.name..tostring(o.state)..tostring(o.active)..tostring(o.tracked) end
    for _,a in ipairs(actions) do stamp[#stamp+1]=a.kind..tostring(a.hash)..a.name end
    for _,l in ipairs(links) do stamp[#stamp+1]=tostring(l.hash) end
    return true,{quest=q,actions=actions,links=links},table.concat(stamp,"|")
  end
end

-- Action links can live below an inactive link container under an active objective.
-- Match QuestDetailsPanelController.PopulateObjectiveActionLinks, without revealing other quests.
function R.actions(hash)
  local jm=Game.GetJournalManager();local w=JournalWrapper.new();w:Init()
  local q=w:BuildQuestData(jm:GetEntry(hash));local out,seen={},{}
  local filter=JournalRequestStateFilter.new();filter.active=true;filter.inactive=true
  local function walk(entry,depth)
    if depth>12 then return end
    for _,child in ipairs(jm:GetChildren(entry,filter)) do
      if child:IsA(CName.new("gameJournalQuestCodexLink")) then
        local linked=jm:GetEntry(child:GetLinkPathHash())
        if linked then
          local id=jm:GetEntryHash(linked)
          if not seen[id] then
            seen[id]=true
            if linked:IsA(CName.new("gameJournalOnscreen")) then
              out[#out+1]={kind="shard",hash=id,name=D.loc(linked:GetTitle())}
            elseif linked:IsA(CName.new("gameJournalPhoneMessage")) or linked:IsA(CName.new("gameJournalPhoneConversation")) or linked:IsA(CName.new("gameJournalPhoneChoiceGroup")) then
              local contact=MessengerUtils.FindContactHash(jm,linked)
              local conversation=MessengerUtils.FindConversationHash(jm,linked)
              local threads=require("modules/data").threads(true,contact)
              for _,thread in ipairs(threads) do
                if not thread.multi or thread.conversationHash==conversation then
                  out[#out+1]={kind="message",hash=id,name=thread.name,thread=thread,contact=contact};break
                end
              end
            end
          end
        end
      elseif child:IsA(CName.new("gameJournalContainerEntry")) then walk(child,depth+1) end
    end
  end
  for _,objective in ipairs(q:GetObjectives()) do if objective:IsActive() then walk(objective:GetQuestObjective(),0) end end
  return out
end

function R.links(hash)
  local w=JournalWrapper.new();w:Init()
  local jm=Game.GetJournalManager();local entry=jm:GetEntry(hash)
  if not entry then return {} end
  local objectives=w:BuildQuestData(entry):GetObjectives();local chosen
  for _,objective in ipairs(objectives) do
    if objective:IsActive() and (not chosen or objective:IsTrackedInHierarchy()) then chosen=objective end
  end
  chosen=chosen or objectives[#objectives]
  if not chosen then return {} end
  local out,seen={},{}
  local filter=JournalRequestStateFilter.new();filter.active=true;filter.inactive=true
  local function walk(parent,depth)
    if depth>12 then return end
    for _,child in ipairs(jm:GetChildren(parent,filter)) do
      if child:IsA(CName.new("gameJournalQuestCodexLink")) then
        local link=jm:GetEntry(child:GetLinkPathHash())
        if link and link:IsA(CName.new("gameJournalCodexEntry")) then
          link=CodexUtils.GetCodexReplacementRecord(jm,link) or link
          local id=jm:GetEntryHash(link)
          if not seen[id] then
            seen[id]=true
            if jm:GetEntryState(link)==gameJournalEntryState.Inactive then jm:ChangeEntryStateByHash(id,gameJournalEntryState.Active,JournalNotifyOption.DoNotNotify) end
            out[#out+1]={hash=id,name=D.loc(link:GetTitle()),image=D.image(link:GetLinkImageID()) or D.image(link:GetImageID())}
          end
        end
      elseif child:IsA(CName.new("gameJournalContainerEntry")) then walk(child,depth+1) end
    end
  end
  walk(chosen:GetQuestObjective(),0)
  table.sort(out,function(a,b) return a.name<b.name end)
  return out
end

function R.tarot()
  local jm=Game.GetJournalManager()
  local context=JournalRequestContext.new();local filter=JournalRequestStateFilter.new();filter.active=true;context.stateFilter=filter
  local found={};local count=0
  for _,group in ipairs(jm:GetTarots(context)) do
    for _,card in ipairs(jm:GetChildren(group,filter)) do
      local index=card:GetIndex();count=count+1
      found[index]={index=index,name=D.loc(card:GetName()),description=D.loc(card:GetDescription()),
        image=D.image(TweakDBID.new("UIIcon."..NameToString(card:GetImagePart()).."_BIG")),found=true}
    end
  end
  local cards={};local total=IsEP1() and 26 or 22
  local back=D.image(TweakDBID.new("UIIcon.TarotCard_Reverse"))
  for i=0,total-1 do cards[#cards+1]=found[i] or {index=i,found=false,image=back} end
  return cards,count,total
end
return R
