-- User preference belongs to the CET mod directory, independently of saves and equipment.
local P={characterModel=false}
local file=io.open("menu_preferences.json","r")
if file then
  local text=file:read("*a");file:close()
  P.characterModel=text:match('"characterModel"%s*:%s*(%a+)')=="true"
end
function P.setCharacterModel(enabled)
  local out=io.open("menu_preferences.json","w")
  if not out then return false end
  local ok=out:write('{"characterModel":'..(enabled and "true" or "false")..'}\n')
  local closed=out:close()
  if not ok or not closed then return false end
  P.characterModel=enabled==true
  return true
end
return P
