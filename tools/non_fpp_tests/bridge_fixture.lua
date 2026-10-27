-- Runs the real module in an isolated LuaJIT environment; no game APIs leak in.
return function(source)
  local callbacks, observed = {}, {}
  local state = { player = true, restriction = true, removals = 0, checks = 0 }
  local player = { GetEntityID = function() return { hash = 123 } end }
  local sys = {
    HasStatusEffect = function() state.checks = state.checks + 1; return state.restriction end,
    RemoveStatusEffect = function()
      if state.fail then error("temporary unavailable system") end
      state.removals = state.removals + 1; state.restriction = false
    end,
    ApplyStatusEffect = function() error("must never force FPP") end,
  }
  local env = {
    type = type, tostring = tostring, tonumber = tonumber, pcall = pcall,
    pairs = pairs, ipairs = ipairs, math = math, string = string, table = table,
    print = function() end, io = { open = function() return nil end }, ImGui = {},
    registerForEvent = function(name, fn) callbacks[name] = fn end,
    ObserveAfter = function(class, name, fn) observed[class .. "." .. name] = fn end,
    Game = {
      GetPlayer = function() if state.player then return player end end,
      GetStatusEffectSystem = function() return sys end,
    },
  }
  -- CET intentionally omits setfenv. Bind all game-facing names lexically so
  -- this also runs in the bridge without registering real callbacks or writes.
  local prefix = "return function(env)\n"
  for _, name in ipairs({"Game", "registerForEvent", "ObserveAfter", "print", "io", "ImGui"}) do
    prefix = prefix .. "local " .. name .. "=env." .. name .. "\n"
  end
  local names = {}
  for name in source:gmatch("%f[%w]VR[%w_]+") do names[name] = true end
  for name in pairs(names) do prefix = prefix .. "local " .. name .. "\n" end
  local module = assert(loadstring(prefix .. source .. "\nend", "camera-bridge-under-test"))()
  module(env); callbacks.onInit()
  callbacks.onUpdate(0.5)
  assert(state.removals == 1 and not state.restriction, "legacy restriction not cleared")
  state.restriction = true -- a fresh restriction applied by the game, not this mod
  for i = 1, 120 do callbacks.onUpdate(0.1) end
  assert(state.removals == 1 and state.restriction, "module keeps fighting game restrictions")
  state.player = false; callbacks.onUpdate(0.6)
  state.player = true; callbacks.onUpdate(0.6)
  assert(state.removals == 2, "new player did not receive migration cleanup")
  state.restriction = true
  observed["PlayerPuppet.OnGameAttached"]()
  state.fail = true; callbacks.onUpdate(0.6)
  assert(state.removals == 2 and state.restriction, "failed cleanup lost its retry")
  state.fail = false; callbacks.onUpdate(0.6)
  assert(state.removals == 3 and not state.restriction, "cleanup retry failed")
  local labels = {}
  local imgui = {
    Begin = function() end, End = function() end, Text = function() end,
    Separator = function() end, Checkbox = function(label, value)
      labels[#labels + 1] = label; return value, false
    end,
  }
  for key, value in pairs(imgui) do env.ImGui[key] = value end
  callbacks.onOverlayOpen(); callbacks.onDraw()
  assert(#labels >= 1, "overlay fixture did not reach checkboxes")
  for _, label in ipairs(labels) do
    assert(not label:find("hold the player"), "hold FPP checkbox remains")
  end
  return "PASS camera bridge: syntax, migration, once per player, reattach, retry, no hold checkbox"
end
