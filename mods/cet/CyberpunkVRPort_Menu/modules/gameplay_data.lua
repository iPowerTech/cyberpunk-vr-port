-- Native game data and requests for the world-space character, journal, codex and clock.
-- Nothing here grants points, reveals inactive journal entries, or bypasses purchase restrictions.
local D = {}

function D.loc(s)
  if s == nil then return "" end
  s = tostring(s)
  local text = GetLocalizedText(s)
  return text ~= nil and text ~= "" and text or s
end

local function development()
  local player = Game.GetPlayer()
  assert(player ~= nil, "player unavailable")
  local manager = PlayerDevelopmentDataManager.new()
  manager:Initialize(player, nil)
  return player, manager, PlayerDevelopmentSystem.GetData(player)
end

function D.character()
  local player, manager, data = development()
  local out = { attributes = {}, skills = {}, points = manager:GetAttributePoints(),
    perks = manager:GetPerkPoints(), relic = manager:GetSpyPerkPoints(),
    level = data:GetProficiencyLevel(gamedataProficiencyType.Level) }
  for _, a in ipairs(manager:GetAttributes()) do
    if a.type == gamedataStatType.Strength or a.type == gamedataStatType.Reflexes
        or a.type == gamedataStatType.TechnicalAbility or a.type == gamedataStatType.Intelligence
        or a.type == gamedataStatType.Cool or (a.type == gamedataStatType.Espionage and a.value > 0) then
      out.attributes[#out.attributes + 1] = { type = a.type, id = a.id, name = D.loc(a.label),
        value = a.value, max = a.maxValue, description = D.loc(a.description),
        canBuy = a.type ~= gamedataStatType.Espionage and data:CanAttributeBeBought(a.type) }
    end
  end
  local hasRelic = false
  for _, a in ipairs(out.attributes) do if a.type == gamedataStatType.Espionage then hasRelic = true end end
  if not hasRelic and Game.GetQuestsSystem():GetFactStr("ep1_tree_unlocked") > 0 then
    out.attributes[#out.attributes + 1] = { type = gamedataStatType.Espionage, name = D.loc("LocKey#20997"),
      value = data:GetAttributePoints(gamedataAttributeDataType.EspionageAttributeData), max = 15,
      description = "", canBuy = false, relic = true }
  end
  for _, name in ipairs({ "CoolSkill", "IntelligenceSkill", "ReflexesSkill", "StrengthSkill", "TechnicalAbilitySkill" }) do
    local kind = gamedataProficiencyType[name]
    if kind ~= nil then
      local info = manager:GetProficiencyDisplayData(kind)
      out.skills[#out.skills + 1] = { name = info.localizedName, level = data:GetProficiencyLevel(kind),
        xp = data:GetCurrentLevelProficiencyExp(kind), remaining = data:GetRemainingExpForLevelUp(kind) }
    end
  end
  return out
end

function D.perks(attribute)
  local player, manager, data = development()
  local record = TweakDBInterface.GetAttributeDataRecord(PerkAttributeHelper.BaseStatToAttribute(attribute.type))
  -- The native perk screen evaluates free roots when entering an attribute, too.
  data:UnlockFreeNewPerks(PlayerDevelopmentData.StatTypeToAttributeDataType(attribute.type))
  local out = {}
  for _, perk in ipairs(record:Perks()) do
    local kind = perk:Type()
    local info = NewPerkDisplayData.new()
    info.type = kind
    info.level = math.max(0, data:IsNewPerkBought(kind))
    info.maxLevel = math.max(data:GetNewPerkMaxLevel(kind), perk:GetLevelsCount())
    info.locked = not data:IsNewPerkUnlocked(kind)
    local requires = {}
    for _, parent in ipairs(perk:RequiresPerks()) do requires[#requires + 1] = D.loc(parent:Loc_name_key()) end
    out[#out + 1] = { type = kind, name = D.loc(perk:Loc_name_key()),
      description = D.loc(perk:Loc_desc_key()), record = perk, level = info.level, max = info.maxLevel,
      locked = info.locked, required = perk:Tier():RequiredAttributePoints(), requires = requires,
      cost = data:IsEspionageMilestonePerk(kind) and 3 or 1,
      canBuy = manager:IsPerkUpgradeable(info, false),
      canSell = manager:IsNewPerkRefundable(info) and PlayerDevelopmentSystem.CanSellNewPerk(player, kind).success
        and player:GetPlayerStateMachineBlackboard():GetInt(GetAllBlackboardDefs().PlayerStateMachine.Combat) ~= 1 }
  end
  table.sort(out, function(a, b)
    if a.required ~= b.required then return a.required < b.required end
    return a.name < b.name
  end)
  return out
end

function D.perkDescription(perk)
  local result = {}
  for index, level in ipairs(perk.record:Levels()) do
    local text = D.loc(level:Loc_desc_key())
    if text == "" then text = perk.description end
    local ui = level:UiData() or perk.record:UiData()
    if ui ~= nil then
      local params = UILocalizationDataPackage.FromNewPerkUIDataPackage(ui)
      local values = {}
      for _, field in ipairs({ {"float", "floatValues"}, {"int", "intValues"}, {"stat", "statValues"} }) do
        for i, value in ipairs(params[field[2]]) do values[field[1] .. "_" .. (i-1)] = string.format("%g", value) end
      end
      for i, value in ipairs(params.nameValues) do values["name_" .. (i-1)] = D.loc(NameToString(value)) end
      for i, value in ipairs(params.statNames) do
        if values["stat_" .. (i-1)] == nil then values["stat_" .. (i-1)] = D.loc(NameToString(value)) end
      end
      text = text:gsub("{([%w_]+)[^}]*}", function(key) return values[key] or ("{" .. key .. "}") end)
    end
    local package = level:DataPackage()
    if package ~= nil then
      for _, modifier in ipairs(package:Stats()) do
        if modifier:IsA(CName.new("gamedataConstantStatModifier_Record")) then
          local key = "<BaseStats." .. NameToString(modifier:StatType():EnumName()) .. ">"
          text = text:gsub(key:gsub("(%W)", "%%%1"), tostring(math.floor(modifier:Value()+0.5)))
        end
      end
    end
    result[#result + 1] = { level = index, text = text }
  end
  return result
end

function D.buyAttribute(kind)
  local player, _, data = development()
  if kind == gamedataStatType.Espionage or not data:CanAttributeBeBought(kind) then return false end
  local request = BuyAttribute.new()
  request:Set(player, kind, false)
  PlayerDevelopmentSystem.GetInstance(player):QueueRequest(request)
  return true
end

function D.changePerk(attribute, kind, refund)
  local player = Game.GetPlayer()
  local current
  for _, p in ipairs(D.perks(attribute)) do if p.type == kind then current = p; break end end
  if current == nil or not (refund and current.canSell or not refund and current.canBuy) then return false end
  local request = refund and SellNewPerk.new() or BuyNewPerk.new()
  request:Set(player, kind)
  PlayerDevelopmentSystem.GetInstance(player):QueueRequest(request)
  UIInventoryScriptableSystem.GetInstance():QueueRequest(request)
  return true
end

local function wrapper()
  local w = JournalWrapper.new()
  w:Init()
  return w
end

function D.quests()
  local w = wrapper()
  local out = {}
  for _, entry in ipairs(w:GetQuests()) do
    local q = w:BuildQuestData(entry)
    out[#out + 1] = { hash = q:GetUniqueId(), name = D.loc(q:GetTitle()),
      state = q:GetStatus(), tracked = q:IsTrackedInHierarchy(), kind = tostring(q:GetType()), entry = entry }
  end
  table.sort(out, function(a, b)
    if a.tracked ~= b.tracked then return a.tracked end
    return a.name < b.name
  end)
  return out
end

function D.quest(hash)
  local w = wrapper()
  local jm = w:GetJournalManager()
  local entry = jm:GetEntry(hash)
  if entry == nil then return nil end
  local q = w:BuildQuestData(entry)
  local out = { name = D.loc(q:GetTitle()), description = D.loc(q:GetDescription()), objectives = {} }
  local function add(o, sub)
    out.objectives[#out.objectives + 1] = { hash = jm:GetEntryHash(o:GetQuestObjective()),
      name = D.loc(o:GetDescription()) .. o:GetCounterText(), active = o:IsActive(),
      state = o:GetStatus(), tracked = o:IsTrackedInHierarchy(), sub = sub }
  end
  for _, o in ipairs(q:GetObjectives()) do
    add(o, false)
    for _, sub in ipairs(o:GetSubObjectives()) do add(sub, true) end
  end
  return out
end

function D.track(hash)
  local jm = Game.GetJournalManager()
  local e = jm:GetEntry(hash)
  if e == nil or jm:GetEntryState(e) ~= gameJournalEntryState.Active then return false end
  jm:TrackEntry(e)
  return true
end

function D.codex()
  local out = {}
  for _, item in ipairs(CodexUtils.GetCodexDataArray(Game.GetJournalManager(), nil, true)) do
    if not item.isHeader then
      local d = item.data
      out[#out + 1] = { hash = d.hash, name = D.loc(d.title), description = D.loc(d.description),
        category = d.category, image = d.imageId, fresh = d.isNew }
    end
  end
  table.sort(out, function(a, b) return a.name < b.name end)
  return out
end

-- D.codex as a job producer (modules/jobs.lua: returns a step function): the native array is one call
-- (~5 ms with 350 entries), so it is a step of its own. Stamp: entries, names and new flags.
function D.codexList()
  local started=false
  return function()
    if not started then started=true;return false end
    local out=D.codex()
    local stamp={}
    for _,e in ipairs(out) do stamp[#stamp+1]=tostring(e.hash)..(e.fresh and "*" or "")..e.name end
    return true,out,table.concat(stamp,"|")
  end
end

-- The codex's active entries, walked a group per step: a cheap watch for D.codexList to rerun.
function D.codexWatch()
  local jm,filter,groups,values,i
  return function()
    if not groups then
      jm=Game.GetJournalManager()
      filter=JournalRequestStateFilter.new();filter.active=true
      local context=JournalRequestContext.new();context.stateFilter=filter
      groups,values,i={},{},0
      for _,category in ipairs(jm:GetCodexCategories(context)) do
        for _,group in ipairs(jm:GetChildren(category,filter)) do groups[#groups+1]=group end
      end
      return false
    end
    i=i+1
    local group=groups[i]
    if group then
      for _,entry in ipairs(jm:GetChildren(group,filter)) do values[#values+1]=tostring(jm:GetEntryHash(entry)) end
      return false
    end
    table.sort(values)
    return true,true,table.concat(values,"|")
  end
end

function D.image(id)
  if id == nil or not TDBID.IsValid(id) then return nil end
  local icon = TweakDBInterface.GetUIIconRecord(id)
  if icon == nil then return nil end
  return { atlas = icon:AtlasResourcePath(), part = NameToString(icon:AtlasPartName()) }
end

-- Cheap signatures avoid rebuilding the menu or interrupting a drag unless game data changed.
function D.signature(section, selected)
  local values = {}
  if section == "character" then
    local _, manager, data = development()
    values = { manager:GetAttributePoints(), manager:GetPerkPoints(), manager:GetSpyPerkPoints() }
    for _, a in ipairs(manager:GetAttributes()) do values[#values+1] = a.value end
    for _, kind in ipairs({ "CoolSkill", "IntelligenceSkill", "ReflexesSkill", "StrengthSkill", "TechnicalAbilitySkill" }) do
      values[#values+1] = data:GetTotalProfExperience(gamedataProficiencyType[kind])
    end
  elseif section == "journal" then
    local jm = Game.GetJournalManager()
    local w = wrapper()
    local tracked = jm:GetTrackedEntry()
    values[1] = tracked ~= nil and jm:GetEntryHash(tracked) or 0
    local position = Game.GetPlayer():GetWorldPosition()
    values[#values+1] = math.floor(position.x / 5)
    values[#values+1] = math.floor(position.y / 5)
    for _, e in ipairs(w:GetQuests()) do
      values[#values+1] = tostring(jm:GetEntryHash(e)) .. tostring(jm:GetEntryState(e))
    end
    if selected ~= nil then
      local q = D.quest(selected)
      if q then
        values[#values+1] = q.description
        for _, o in ipairs(q.objectives) do values[#values+1] = o.name .. tostring(o.state) end
      end
    end
  elseif section == "codex" then
    local jm = Game.GetJournalManager()
    local filter = JournalRequestStateFilter.new()
    filter.active = true
    local context = JournalRequestContext.new()
    context.stateFilter = filter
    for _, category in ipairs(jm:GetCodexCategories(context)) do
      for _, group in ipairs(jm:GetChildren(category, filter)) do
        for _, entry in ipairs(jm:GetChildren(group, filter)) do
          values[#values+1] = tostring(jm:GetEntryHash(entry)) .. tostring(jm:IsEntryVisited(entry))
        end
      end
    end
  end
  return table.concat(values, "|")
end

function D.visit(hash)
  local jm = Game.GetJournalManager()
  local entry = jm:GetEntry(hash)
  if entry ~= nil then jm:SetEntryVisited(entry, true) end
end

function D.clock()
  local player = Game.GetPlayer()
  local time = Game.GetTimeSystem():GetGameTime()
  return { hour = GameTime.Hours(time), minute = GameTime.Minutes(time),
    allowed = GameTimeUtils.CanPlayerTimeSkip(player), hidden = GameTimeUtils.IsTimeDisplayGlitched(player) }
end

function D.skip(hours)
  hours = math.floor(tonumber(hours) or 0)
  local player = Game.GetPlayer()
  if hours < 1 or hours > 24 or not GameTimeUtils.CanPlayerTimeSkip(player) then return false end
  local ts = Game.GetTimeSystem()
  local now = ts:GetGameTime()
  -- Same time-system request and player fast-forward as TimeskipGameController.Apply.
  ts:SetGameTimeByHMS(GameTime.Hours(now) + hours, GameTime.Minutes(now), GameTime.Seconds(now), CName.new("ui_menu_timeskip"))
  GameTimeUtils.FastForwardPlayerState(player)
  return true
end

return D
