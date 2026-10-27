-- THE MENU'S OWN WORDS IN THE GAME'S TEXT LANGUAGE (Settings > Language > On-screen text, read when the menu
-- opens). Russian is written beside the English at every call site, as it always was; each other language is a
-- table keyed by the English text, modules/lang/<code>.lua (tools/menu_locale/build_lang.py writes them from
-- tools/menu_locale/strings.json). A string a table lacks stays English and is remembered, so a tour of the menu
-- in that language ends with L.missing() listing exactly what is left to translate.
--
-- Codes are the game's own: pl-pl en-us es-es fr-fr it-it de-de es-mx kr-kr zh-cn ru-ru pt-br jp-jp zh-tw ar-ar
-- cz-cz hu-hu tr-tr th-th ua-ua.
local L = { code = "en-us" }
local tables, misses, labelSets = {}, {}, {}

local function load(code)
  if tables[code] == nil then
    local ok, t = pcall(require, "modules/lang/" .. code)
    tables[code] = (ok and type(t) == "table") and t or false
  end
  return tables[code] or nil
end

function L.set(code)
  L.code = code or "en-us"
  L.map = (L.code ~= "en-us" and L.code ~= "ru-ru") and load(L.code) or nil
end

function L.ru() return L.code == "ru-ru" end

function L.t(en)
  if en == nil or L.map == nil then return en end
  local s = L.map[en]
  if s ~= nil then return s end
  -- a string with a number in it was put together already (a price, a count): not a key, not a miss
  if not string.find(en, "%d") then misses[L.code .. "\t" .. en] = true end
  return en
end

function L.tr(ru, en)
  if L.code == "ru-ru" then return ru end
  return L.t(en)
end

-- A whole label table (menu.lua's TEXT, gameplay.lua's LABELS): Russian as written, any other language the
-- English one translated value by value, made once per language. `lang` stays "ru" or "en".
function L.labels(en, ru)
  if L.code == "ru-ru" then return ru end
  if L.map == nil then return en end
  local byCode = labelSets[en] or {}
  labelSets[en] = byCode
  local out = byCode[L.code]
  if out == nil then
    out = {}
    for k, v in pairs(en) do out[k] = (k == "lang" or type(v) ~= "string") and v or L.t(v) end
    byCode[L.code] = out
  end
  return out
end

-- PLURALS. Russian keeps its three written forms; the others are translated as the English singular with the
-- `#` prefix, a table of forms in the language's own order, chosen by its rule (CLDR's cardinal categories,
-- reduced to the forms a count of saves or playthroughs can take). A form follows the number, unless it says
-- where the number goes with {n} (a counter word: "セーブデータ{n}件", "저장 파일 {n}개").
local RULES = {
  ["pl-pl"] = function(n) local a, b = n % 10, n % 100
    if n == 1 then return 1 end
    if a >= 2 and a <= 4 and not (b >= 12 and b <= 14) then return 2 end
    return 3 end,
  ["cz-cz"] = function(n) if n == 1 then return 1 end if n >= 2 and n <= 4 then return 2 end return 3 end,
  ["ua-ua"] = function(n) local a, b = n % 10, n % 100
    if a == 1 and b ~= 11 then return 1 end
    if a >= 2 and a <= 4 and not (b >= 12 and b <= 14) then return 2 end
    return 3 end,
  ["ar-ar"] = function(n) local b = n % 100
    if n == 0 then return 6 end
    if n == 1 then return 1 end
    if n == 2 then return 2 end
    if b >= 3 and b <= 10 then return 3 end
    if b >= 11 then return 4 end
    return 5 end,
  ["fr-fr"] = function(n) return (n == 0 or n == 1) and 1 or 2 end,
  ["pt-br"] = function(n) return (n == 0 or n == 1) and 1 or 2 end,
}
local function oneOther(n) return n == 1 and 1 or 2 end

-- n and its noun: ruOne/ruFew/ruMany are Russian's forms, enOne/enOther English's.
function L.count(n, ruOne, ruFew, ruMany, enOne, enOther)
  if L.code == "ru-ru" then
    local last, teen = n % 10, n % 100
    return n .. " " .. ((teen >= 11 and teen <= 14) and ruMany or last == 1 and ruOne or (last >= 2 and last <= 4) and ruFew or ruMany)
  end
  local forms = L.map and L.map["#" .. enOne]
  if type(forms) == "table" then
    local i = (RULES[L.code] or oneOther)(n)
    local form = forms[math.min(i, #forms)] or forms[#forms]
    if string.find(form, "{n}", 1, true) then return (string.gsub(form, "{n}", tostring(n))) end
    return n .. " " .. form
  end
  return n .. " " .. L.t(n == 1 and enOne or enOther)
end

-- THE SETTINGS PAGES (settings.lua). The plugin's ImGui panels are written in English, so every language,
-- Russian too, has its own table: modules/lang/settings/<code>.lua, keyed by the panel's English
-- (tools/menu_locale/build_lang.py writes them). A key with printf conversions is a FORMAT the panel filled in
-- ("%.1f deg", "GPU load %.1f%%"): a finished text is matched against it, and what it holds goes into the
-- translated format in the same order -- a %s translated itself when the table has it ("LEFT").
local settingSets = {}
local CONV = "^%%[%-+ #0]*%d*%.?%d*[hlLzjt]*[diouxXeEfgGcsp]"
local CAPTURE = { d = "( *[%+%-]?%d+)", i = "( *[%+%-]?%d+)", u = "( *%d+)", o = "( *%d+)", x = "( *%x+)",
  X = "( *%x+)", c = "(.)", s = "(.-)", p = "(%x+)" }

-- A format as a Lua pattern, and the kinds of its conversions in order.
local function compile(en)
  local pat, kinds, i = { "^" }, {}, 1
  while i <= #en do
    local ch = string.sub(en, i, i)
    if ch == "%" and string.sub(en, i + 1, i + 1) == "%" then
      pat[#pat + 1] = "%%"
      i = i + 2
    else
      local conv = ch == "%" and string.match(en, CONV, i)
      if conv then
        local kind = string.sub(conv, -1)
        kinds[#kinds + 1] = kind
        pat[#pat + 1] = CAPTURE[kind] or "( *[%+%-]?[%d%.]+)"
        i = i + #conv
      else
        pat[#pat + 1] = string.find(ch, "^[%^%$%(%)%%%.%[%]%*%+%-%?]") and "%" .. ch or ch
        i = i + 1
      end
    end
  end
  pat[#pat + 1] = "$"
  return table.concat(pat), kinds
end

-- A format with its conversions replaced by `values`, in order, and %% made one %.
local function fill(format, values)
  local n, i, out = 0, 1, {}
  while i <= #format do
    local ch = string.sub(format, i, i)
    if ch == "%" and string.sub(format, i + 1, i + 1) == "%" then
      out[#out + 1] = "%"
      i = i + 2
    else
      local conv = ch == "%" and string.match(format, CONV, i)
      if conv then
        n = n + 1
        out[#out + 1] = values[n] or ""
        i = i + #conv
      else
        out[#out + 1] = ch
        i = i + 1
      end
    end
  end
  return table.concat(out)
end

local function settingSet(code)
  local set = settingSets[code]
  if set == nil then
    local ok, t = pcall(require, "modules/lang/settings/" .. code)
    set = false
    if ok and type(t) == "table" then
      set = { exact = {}, formats = {}, memo = {}, size = 0 }
      for en, tr in pairs(t) do
        if string.find(en, "%", 1, true) then
          local pat, kinds = compile(en)
          if #kinds == 0 then set.exact[fill(en, {})] = fill(tr, {})
          else set.formats[#set.formats + 1] = { pat = pat, kinds = kinds, tr = tr } end
        else
          set.exact[en] = tr
        end
      end
    end
    settingSets[code] = set
  end
  return set or nil
end

function L.setting(en)
  if en == nil or en == "" or L.code == "en-us" then return en end
  local set = settingSet(L.code)
  if set == nil then return en end
  local s = set.exact[en] or set.memo[en]
  if s ~= nil then return s end
  s = nil
  for _, f in ipairs(set.formats) do
    local caps = { string.match(en, f.pat) }
    if #caps == #f.kinds then
      for k, kind in ipairs(f.kinds) do
        if kind == "s" then caps[k] = set.exact[caps[k]] or caps[k] end
      end
      s = fill(f.tr, caps)
      break
    end
  end
  if s == nil then
    s = en
    if string.find(en, "%a") then misses[L.code .. "\t[settings] " .. en] = true end
  end
  -- texts with live numbers in them change all the time: the memo is dropped before it grows large
  if set.size > 4000 then set.memo, set.size = {}, 0 end
  set.memo[en], set.size = s, set.size + 1
  return s
end

function L.missing()
  local out = {}
  for k in pairs(misses) do out[#out + 1] = k end
  table.sort(out)
  return out
end

return L
