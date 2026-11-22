-- Fork: THE DIALOGUE ON THE WRIST. A conversation's answers (UIInteractions.DialogChoiceHubs, the list the game draws
-- in front of the camera) drawn again on a small panel over the LEFT wrist, shown and hidden as the game shows and
-- hides them: the player brings the wrist up to read them.
--
-- The game's list in front is hidden meanwhile (hideOriginal); the right hand picks an answer on the wrist (pick), the
-- stick and A as before. F10 > HUD "Dialogue choices on the left wrist" (xr_dialog_wrist) turns it all off. Not in a
-- vehicle (the dashboard panel is a later step) and not while the VR menu is open.
--
-- WHAT IT IS: the menu's screen (vr_menu.ent, 3200 x 1600 px on 1.00 x 0.50 m, alpha blended: what is not drawn is
-- not there), with the panel in its middle. It faces the eyes and stands upright, its bottom edge LIFT above the
-- wrist. Placed every frame. It stays at the wrist for the session, its content transparent between dialogues: brought
-- to the wrist from out of sight when a dialogue appeared, it showed black first, every time (user, 2026-11-23: only
-- as the dialogue appears; a new line, an answer, the wrist moving: never).
--
-- THE GAME'S OWN PANEL. The user wanted the game's dialogue list itself, not a panel that looks different
-- (2026-11-22). dialog.inkwidget's library item "dialog" is the list the HUD shows, with its own game controller
-- (dialogWidgetGameController), which reads the UIInteractions board by itself. The HUD's interactions hub controller
-- declares that library, so its stock SpawnFromExternal puts a second one on this screen: the game draws it, with its
-- colours, captions, timers and highlight (measured 2026-11-22: its controller runs there). Should it not come alive
-- (no hub spawned in it within GAME_WAIT while the game shows answers), its hubs are given the answers from here; and
-- should those not spawn, the panel drawn here stands in.
--
-- THE WRIST is where the arm's solver is sent (VRHandRawWorld: the hand bone's target, on the forearm's axis). The
-- palm bone moved back along one of its axes swung round the wrist as the forearm turned, and the panel rose and
-- sank into the wrist (user report 2026-11-22).
--
-- Verbose log (F10 > Diagnostics): "[VRDialogWrist]" when it shows, hides, and when the answers change.

local K = require("modules/ink")
local D = require("modules/data")
local L = require("modules/locale")
local Space = require("modules/space")

local add, sub, mul, cross, norm, qrot, transformFor =
  Space.add, Space.sub, Space.mul, Space.cross, Space.norm, Space.qrot, Space.transformFor

local DW = {}

local ENT = "vrport\\ui\\vr_menu.ent"
local UI = "vrp_menu_ui"
local CW, CH, PX = 3200, 1600, 3200       -- canvas pixels, pixels per metre
local CX, CY = CW / 2, CH / 2

-- The panel, canvas px (3200 per metre): 0.23 m wide, a 2 cm row.
local PW = 740
local PAD = 18
local TITLE_H = 56
local ROW_H = 66
local TEXT_SIZE = 34
local TAG_SIZE = 28
local TITLE_SIZE = 30
local MAX_ROWS = 10

-- The game's panel (see the top): its library, the width it is fitted to (canvas px) and how long it may take to fill.
local DIALOG_LIB = "base\\gameplay\\gui\\widgets\\interactions\\dialog.inkwidget"
local GAME_W = 760
local GAME_WAIT = 1.5
local ROWS_SETTLE, ROWS_MOST = 0.05, 1.0   -- s: after the game's rows are all there; shown anyway past this

-- Metres.
local LIFT = 0.05                          -- the panel's bottom edge over the wrist, world up
local POLL = 1 / 15                        -- seconds between reads of the blackboard

local DEBUG_SLOT = 156                     -- F10 > Diagnostics "Verbose log"

-- gameinteractionsChoiceType bits in ChoiceTypeWrapper.properties
local QUEST_IMPORTANT, ALREADY_READ, INACTIVE, CHECK_FAILED, BLUELINE, GLOWLINE = 0, 1, 2, 4, 8, 12

local BLUE = { 0.36, 0.62, 1.0 }

local S = {
  time = 0.0,
  screen = nil,          -- { id, at, entity, comp, root, built, gpu, shown, displayOn, warm, panel, game, mode, waited }
  tries = 0,             -- screens made in a row that did not come ready (made again up to TRIES)
  pollAt = -1.0,
  data = nil,            -- what the blackboard said last: { hubs, active, selected }
  sig = nil,             -- the answers drawn
  hl = nil,              -- the highlight drawn
  rows = {},             -- drawn rows: { hub, index, frame, fill }
  panelH = 0,
  showing = false,
  shownAt = 0.0,         -- when the answers last appeared or changed (no fingertip take before DEAD after it)
  pick = {},             -- the right hand on the panel: target, pending step, armed, appliedAt
  original = nil,        -- the HUD's own list (hidden while the wrist shows it)
  originalHidden = false,
  lastError = nil,
}

---------------------------------------------------------------- small helpers

local function verbose()
  if type(GetVRSharedSlot) ~= "function" then return false end
  local ok, v = pcall(GetVRSharedSlot, DEBUG_SLOT)
  return ok and v ~= nil and v > 0.5
end

local function note(s)
  if verbose() then print("[VRDialogWrist] " .. s) end
end

-- Which panel is on the wrist and why: said once, as soon as the Verbose log is on (also when ticked later).
local function status(s)
  S.status = s
end

-- A hub's title comes as a LocKey ("LocKey#46420"); resolved here so it can be cut and upper-cased like any text.
local function loc(s)
  if type(s) ~= "string" then return "" end
  if string.sub(s, 1, 7) == "LocKey#" then
    local ok, t = pcall(GetLocalizedText, s)
    if ok and type(t) == "string" and t ~= "" then return t end
  end
  return s
end

-- Characters in a UTF-8 string.
local function chars(s)
  local _, n = string.gsub(s or "", "[^\128-\191]", "")
  return n
end

local function has(c, bit)
  local p = 0
  pcall(function() p = c.type.properties end)
  return math.floor((p or 0) / 2 ^ bit) % 2 == 1
end

---------------------------------------------------------------- what the game shows

-- The answers on screen, as dialogUI.script draws them: a caption's tags, or a "[tag]" opening the text.
local function readHubs()
  local out = nil
  pcall(function()
    local defs = Game.GetAllBlackboardDefs().UIInteractions
    local bb = Game.GetBlackboardSystem():Get(defs)
    local hubs = FromVariant(bb:GetVariant(defs.DialogChoiceHubs))
    local list = {}
    for _, hub in ipairs((hubs and hubs.choiceHubs) or {}) do
      local rows = {}
      for i, c in ipairs(hub.choices or {}) do
        local tags = ""
        pcall(function() tags = GetCaptionTagsFromArray(c.captionParts.parts) or "" end)
        local text = loc(c.localizedName)
        if tags == "" and string.sub(text, 1, 1) == "[" then
          local a, b = string.match(text, "^%[([^%]]*)%]%s*(.*)$")
          if a ~= nil then tags, text = a, b end
        end
        rows[i] = {
          text = text, tags = tags,
          inactive = has(c, INACTIVE),
          important = has(c, QUEST_IMPORTANT),
          blue = has(c, BLUELINE),
          dim = has(c, INACTIVE) or has(c, CHECK_FAILED) or
                (not has(c, QUEST_IMPORTANT) and has(c, ALREADY_READ) and not has(c, GLOWLINE)),
        }
      end
      if #rows > 0 then list[#list + 1] = { id = hub.id, title = loc(hub.title), rows = rows, raw = hub } end
    end
    out = { hubs = list, active = bb:GetInt(defs.ActiveChoiceHubID), selected = bb:GetInt(defs.SelectedIndex) }
  end)
  return out
end

local function signature(data)
  local parts = {}
  for _, hub in ipairs(data.hubs) do
    parts[#parts + 1] = tostring(hub.id) .. "|" .. hub.title
    for _, r in ipairs(hub.rows) do
      parts[#parts + 1] = r.tags .. "|" .. r.text .. "|" .. tostring(r.important) .. tostring(r.blue) .. tostring(r.dim)
    end
  end
  return table.concat(parts, "\n")
end

local function inVehicle(pl)
  local yes = false
  pcall(function() yes = Game.GetMountedVehicle(pl) ~= nil end)
  return yes
end

---------------------------------------------------------------- the screen entity (as modules/wheel.lua)

local function spawn(path, tr)
  local id = nil
  pcall(function() id = exEntitySpawner.Spawn(path, tr, "") end)
  return { id = id }
end

local function despawn(h)
  if h == nil or h.id == nil then return end
  pcall(function()
    local e = Game.FindEntityByID(h.id)
    if e ~= nil then exEntitySpawner.Despawn(e) end
  end)
  h.id, h.entity, h.comp, h.root = nil, nil, nil, nil
end

local function resolve(h)
  if h == nil or h.id == nil then return false end
  if h.comp ~= nil and h.root ~= nil then return true end
  pcall(function()
    h.entity = Game.FindEntityByID(h.id)
    if h.entity ~= nil then h.comp = h.entity:FindComponentByName(CName.new(UI)) end
    if h.comp ~= nil then h.root = h.comp:GetWidget() end
  end)
  return h.comp ~= nil and h.root ~= nil
end

local function place(h, tr)
  if h ~= nil and h.entity ~= nil then pcall(function() Game.VREntitySetWorldTransform(h.entity, tr) end) end
end

-- THE SCREEN'S DISPLAY (the 1.00 x 0.50 m plane the canvas is drawn on) IS SWITCHED ON ONCE, out of sight (tick), and
-- never off: switched on, the engine makes the plane's render proxy anew, which starts as the material's default, an
-- OPAQUE BLACK plane, until the component binds its texture to it some frames later (Natives/MenuSurface.cpp, Rebind).
-- Switched on at the wrist, all of it showed black before the panel (user's screenshot, 2026-11-22). Shown and hidden
-- after that by the opacity of what is drawn on it (the panel, the game's list, the cursor), the plane left at the wrist.
local function display(h, on)
  if h == nil or h.entity == nil or h.shown == on then return end
  for _, w in ipairs({ h.panel, h.holder }) do pcall(function() w:SetOpacity(on and 1.0 or 0.0) end) end
  if not on and h.cursor ~= nil then pcall(function() h.cursor:SetOpacity(0) end) end
  h.shown = on
end

-- Out of sight while its display warms up (once): HEAD_BACK behind the eyes along the view's heading, facing away
-- from them; no field of view reaches behind the head, and nothing covers it there. 5 m under the floor, the engine
-- never gave a new screen its canvas there, nor bound a warmed one's (the plugin's log, 2026-11-22).
local HEAD_BACK, WARM_UP = 0.5, 1.5      -- m; s the screen stays out of sight after its display goes on
local READY_WAIT, TRIES = 3.0, 3         -- s a new screen has to come ready; screens made in a row before giving up
local MOVE_WAIT = 0.15                   -- s out of sight before the display goes on there
local function behindHead()
  local view = Space.view()
  if view == nil then return nil end
  local N = mul(view.f, -1)
  local R = norm(cross({ 0, 0, 1 }, N))
  if R[1] == 0 and R[2] == 0 and R[3] == 0 then return nil end
  return transformFor(add(view.p, mul(view.f, -HEAD_BACK)), R, cross(N, R))
end

local function release()
  despawn(S.screen)
  S.screen, S.sig, S.hl, S.rows, S.showing = nil, nil, nil, {}, false
  S.pick = {}
end

---------------------------------------------------------------- drawing

-- The HUD's interactions hub: its library declares dialog.inkwidget.
local function hubController()
  local layer = Game.VRInkLayer(CName.new("inkHUDLayer"))
  if layer == nil then return nil end
  for _, c in ipairs(layer:GetGameControllers()) do
    if NameToString(c:GetClassName()) == "gameuiInteractionsHubGameController" then return c end
  end
  return nil
end

-- A library item of dialog.inkwidget under parent. Nil and why when it cannot be made.
local function spawnItem(parent, item)
  local ctrl = hubController()
  if ctrl == nil then return nil, "no interactions hub controller in the HUD" end
  local w = ctrl:SpawnFromExternal(parent, ResRef.FromString(DIALOG_LIB), CName.new(item))
  if w == nil then return nil, "SpawnFromExternal(" .. item .. ") gave nothing" end
  return w
end

-- The game's list on this screen, centred on the canvas (scaled in fitGame): its "dialog" item, whose own controller
-- may run it ("game"); if that controller does not come alive, the same item holds the game's hubs, given the board's
-- answers from here with the call the game makes ("hubs"); else the drawn panel ("drawn").
local function buildFrame(h)
  h.root:RemoveAllChildren()
  h.warm = K.rect(h.root, 0, 0, 1, 1, K.BLACK, 1)
  h.panel = K.canvas(h.root, 0, 0, CW, CH)
  local holder = K.canvas(h.root, 0, 0, CW, CH)
  -- hidden from the first write on (display)
  h.panel:SetOpacity(0)
  holder:SetOpacity(0)
  h.holder, h.shown = holder, false
  local ok, w, why = pcall(spawnItem, holder, "dialog")
  if ok and w ~= nil then
    w:SetAnchor(inkEAnchor.Centered)
    w:SetAnchorPoint(Vector2.new({ X = 0.5, Y = 0.5 }))
    h.game, h.mode, h.waited, h.hubs = w, "game", 0, {}
    status(string.format("the game's dialog item is on the wrist screen (%s, %d child(ren)); waiting for its controller",
      NameToString(w:GetClassName()), w:GetNumChildren()))
  else
    h.mode = "drawn"
    status("the game's dialog item could not be made (" .. tostring(ok and why or w) .. "): the drawn panel stands in")
  end
  h.built = true
end

-- The game's hubs in the dialog item, given the answers as dialogWidgetGameController.OnInteractionsChanged gives them.
local function driveHubs(h, data)
  local hubs = h.hubs
  while #hubs > #data.hubs do
    local x = table.remove(hubs)
    pcall(function() h.game:RemoveChild(x.widget) end)
  end
  while #hubs < #data.hubs do
    local w, why = spawnItem(h.game, "hub")
    if w == nil then return false, why end
    local c = w:GetController()
    if c == nil then return false, "the hub has no controller" end
    hubs[#hubs + 1] = { widget = w, ctrl = c }
  end
  local total = 0
  for _, hub in ipairs(data.hubs) do total = total + #hub.rows end
  local at = 0
  for i, hub in ipairs(data.hubs) do
    hubs[i].ctrl:SetData(hub.raw, hub.id == data.active, data.selected, i ~= 1, i ~= #data.hubs, at, total)
    at = at + #hub.rows
  end
  if #hubs > 0 then pcall(function() hubs[1].ctrl:OverrideInputButton(data.active == -1) end) end
  return true
end

-- The game's list's laid-out size, and which call gave it: its desired size, the size its parent gives it, its own.
local function measure(w)
  local function got(f)
    local ok, s = pcall(f)
    if ok and s ~= nil and s.X >= 1 and s.Y >= 1 then return s end
  end
  local s = got(function() return w:GetDesiredSize() end)
  if s then return s, "desired size" end
  s = got(function() return w:GetParentWidget():GetChildSize(w) end)
  if s then return s, "parent's child size" end
  s = got(function() return w:GetSize() end)
  if s then return s, "own size" end
end

-- The game's list at GAME_W wide; its height for the placement. False while it holds no hub yet. A list with no size
-- yet (a new line's hub for a moment; and the screen is off until the panel shows, where it may not be laid out) keeps
-- the last fit, or before any the fit of the list as measured (1072 x 244, a one-hub answer list, 2026-11-22): drawn at
-- the game's own scale it shrank a moment after, at every line.
local KNOWN_W, KNOWN_H = 1072, 244
local function fitGame(h, dt)
  local w = h.game
  if w == nil then return false end
  if w:GetNumChildren() <= 1 then return false end -- only its authored selection canvas: no hub spawned yet
  local size, from = measure(w)
  if size == nil then
    local s = h.lastScale or GAME_W / KNOWN_W
    w:SetScale(Vector2.new({ X = s, Y = s }))
    S.panelH = h.lastH or KNOWN_H * s
    return true
  end
  local s = math.max(0.2, math.min(1.5, GAME_W / size.X))
  w:SetScale(Vector2.new({ X = s, Y = s }))
  S.panelH = size.Y * s
  h.lastScale, h.lastH = s, S.panelH
  if h.sizeFrom ~= from then
    h.sizeFrom = from
    status(string.format("the game's list is %.0f x %.0f (its %s), fitted at %.2f", size.X, size.Y, from, s))
  end
  return true
end

local function rowColour(r)
  if r.dim then return K.GREY end
  if r.important then return K.YELLOW end
  if r.blue then return BLUE end
  return K.TEXT
end

-- The panel, centred on the canvas: each hub's title (when it has one), then its answers.
local function drawPanel(h, data)
  local p = h.panel
  p:RemoveAllChildren()
  S.rows = {}
  local lines = 0
  for _, hub in ipairs(data.hubs) do
    if hub.title ~= "" and hub.title ~= " " then lines = lines + TITLE_H end
    lines = lines + math.min(#hub.rows, MAX_ROWS) * ROW_H
  end
  local ph = lines + 2 * PAD
  local x0, y0 = CX - PW / 2, CY - ph / 2
  K.roundFill(p, x0, y0, PW, ph, K.NAVY, .82, 14)
  K.roundEdge(p, x0, y0, PW, ph, K.CYAN, .55, 14)
  local y = y0 + PAD
  for _, hub in ipairs(data.hubs) do
    if hub.title ~= "" and hub.title ~= " " then
      K.text(p, x0 + PAD + 8, y, PW - 2 * PAD - 16, TITLE_H, K.upper(hub.title), TITLE_SIZE, K.ICE, "left", true)
      y = y + TITLE_H
    end
    for i, r in ipairs(hub.rows) do
      if i > MAX_ROWS then break end
      local fill = K.roundFill(p, x0 + PAD, y + 3, PW - 2 * PAD, ROW_H - 6, K.YELLOW, 0, 8)
      local frame = K.roundEdge(p, x0 + PAD, y + 3, PW - 2 * PAD, ROW_H - 6, K.YELLOW, 0, 8)
      local tx = x0 + PAD + 14
      local col = rowColour(r)
      if r.tags ~= "" then
        local tag = K.clip(K.upper(loc(r.tags)), 14)
        K.text(p, tx, y, 260, ROW_H, tag, TAG_SIZE, r.blue and BLUE or K.ICE, "left", true)
        -- a text's own size is known a frame on; the column is the tag's length at this size
        tx = tx + math.max(chars(tag) * TAG_SIZE * 0.56, 40) + 14
      end
      K.text(p, tx, y, x0 + PW - PAD - 14 - tx, ROW_H, K.clip(r.text, 44), TEXT_SIZE, col, "left", false)
      S.rows[#S.rows + 1] = { hub = hub.id, index = i - 1, fill = fill, frame = frame }
      y = y + ROW_H
    end
  end
  S.panelH = ph
end

-- The game's highlight: the active hub's selected answer.
local function paintHighlight(data)
  for _, row in ipairs(S.rows) do
    local on = row.hub == data.active and row.index == data.selected
    pcall(function() row.fill:SetOpacity(on and .16 or 0) end)
    K.tint(row.frame, K.YELLOW, on and .9 or 0)
  end
end

---------------------------------------------------------------- where it hangs

-- Over the left wrist, facing the eyes, upright: C, R, U for transformFor (R canvas right, U canvas up).
-- The wrist and the eyes in the world, two ways. The ENGINE's route: the arm solver's target and the camera in model
-- space, carried by the player as the engine has it this frame (VRPlayerEnginePos/Rot, as Natives/HeldBody.cpp's
-- HandPose places a carried thing). The VIEW's route: the same target through the render view (VRHandRawWorld,
-- Space.view), which rides another point of the frame than the entity this places: walking, the panel hopped after
-- the player (user report 2026-11-22). The engine's is placed, the view's only when it is missing.
local function where()
  local e = {}
  pcall(function()
    local P, q = VRPlayerEnginePos(), VRPlayerEngineRot()
    if P.w < 0.5 or (q.i == 0 and q.j == 0 and q.k == 0 and q.r == 0) then return end
    local m, c = VRHandRawModel(0), VRCamModelPos()
    local o = { P.x, P.y, P.z }
    if m.w > 0.5 then e.wrist = add(o, qrot(q, { m.x, m.y, m.z })) end
    if c.w > 0.5 then e.eyes = add(o, qrot(q, { c.x, c.y, c.z })) end
    e.player = o
  end)
  local v = {}
  pcall(function()
    local p = VRHandRawWorld(0)
    if p.w > 0.5 then v.wrist = { p.x, p.y, p.z } end
    local view = Space.view()
    if view ~= nil then v.eyes = view.p end
  end)
  return e, v
end

-- Verbose log: walking, how far each route's wrist jumps a frame against the player (a still hand: none), every 2 s.
local J = { n = 0, e = 0, v = 0, p = 0, apart = 0, t = 0 }
local function jitter(e, v, dt)
  local last = J.last
  J.last = { e = e.wrist, v = v.wrist, p = e.player }
  if not (last and last.e and last.v and last.p and e.wrist and v.wrist and e.player) then return end
  local dp = sub(e.player, last.p)
  local step = math.sqrt(dp[1] ^ 2 + dp[2] ^ 2 + dp[3] ^ 2)
  if step < 0.005 or step > 0.5 then return end -- standing, or a load / a teleport
  local function off(a, b)
    local d = sub(sub(a, b), dp)
    return math.sqrt(d[1] ^ 2 + d[2] ^ 2 + d[3] ^ 2)
  end
  local gap = sub(e.wrist, v.wrist)
  J.n, J.p, J.t = J.n + 1, J.p + step, J.t + dt
  J.e, J.v = J.e + off(e.wrist, last.e), J.v + off(v.wrist, last.v)
  J.apart = J.apart + math.sqrt(gap[1] ^ 2 + gap[2] ^ 2 + gap[3] ^ 2)
  if J.t >= 2 then
    note(string.format("walking, %d frames: the wrist jumps %.1f mm a frame against the player by the engine's route, "
      .. "%.1f mm by the view's (the player moves %.1f mm a frame; the routes %.0f mm apart)",
      J.n, J.e / J.n * 1000, J.v / J.n * 1000, J.p / J.n * 1000, J.apart / J.n * 1000))
    J.n, J.e, J.v, J.p, J.apart, J.t = 0, 0, 0, 0, 0, 0
  end
end

local function frame(dt)
  local e, v = where()
  if verbose() then jitter(e, v, dt) end
  local wrist, eyes = e.wrist, e.eyes
  if wrist == nil or eyes == nil then wrist, eyes = v.wrist, v.eyes end
  if wrist == nil or eyes == nil then return nil end
  local base = add(wrist, { 0, 0, LIFT })
  local N = norm(sub(eyes, base))
  local R = norm(cross({ 0, 0, 1 }, N))
  if R[1] == 0 and R[2] == 0 and R[3] == 0 then return nil end
  local U = cross(N, R)
  return add(base, mul(U, S.panelH / 2 / PX)), R, U
end

---------------------------------------------------------------- the game's list in the HUD

-- THE ORIGINAL, in front of the camera: the HUD's own dialog item (its root "hubVert" under the interactions hub),
-- hidden by opacity while the wrist shows the answers, back as soon as it does not (2026-11-22, the user: "nascosto il
-- pannello originale"). Re-applied every frame, in case the game sets it.
local function originalList()
  local w = S.original
  if w ~= nil then
    local ok, alive = pcall(function() return w:GetParentWidget() ~= nil end)
    if ok and alive then return w end
    S.original = nil
  end
  local ctrl = hubController()
  if ctrl == nil then return nil end
  local found = nil
  local function walk(x, depth)
    if found ~= nil or x == nil or depth > 14 then return end
    if NameToString(x:GetName()) == "hubVert" then found = x return end
    if x:IsA("inkCompoundWidget") then
      for i = 0, x:GetNumChildren() - 1 do walk(x:GetWidgetByIndex(i), depth + 1) end
    end
  end
  walk(ctrl:GetRootWidget(), 0)
  S.original = found
  return found
end

local function hideOriginal(hide)
  if not hide and not S.originalHidden then return end
  pcall(function()
    local w = originalList()
    if w == nil then return end
    local want = hide and 0 or 1
    if math.abs(w:GetOpacity() - want) > 0.01 then w:SetOpacity(want) end
    if hide ~= S.originalHidden then
      S.originalHidden = hide
      note(hide and "the game's own list in front: hidden" or "the game's own list in front: shown again")
    end
  end)
end

---------------------------------------------------------------- picking an answer by hand

-- THE RIGHT HAND picks (the panel is on the left wrist), as in the VR menu: its pointer's beam, or its fingertip once
-- within HOVER of the panel, over an answer of the active hub moves the game's highlight there, a step at a time
-- (ChoiceScrollUp / ChoiceScrollDown, as the stick does); the fingertip pushed into the HIGHLIGHTED answer, coming from
-- ARM in front and crossing PRESS, takes it (ChoiceApply, as A does). Never within DEAD of the answers appearing or
-- changing, never twice within COOLDOWN, never on an unavailable answer, never with a weapon in the right hand. The
-- stick and A keep working as before.
local PLANE_W, PLANE_H = 1.00, 0.50             -- the screen, metres (vr_menu.ent)
local HOVER, ARM, PRESS, THROUGH = 0.10, 0.025, 0.010, -0.05
local STEP_WAIT, STEP_LOST = 0.10, 0.40         -- s between steps; a step the game has not answered by then is lost
local DEAD, COOLDOWN = 0.5, 0.6
local HAPTIC_CLICK = 20
local CLICK_WAV = "CyberpunkVRPort_Menu/sounds/click.wav"

-- A widget's box on the screen, canvas px (as Stereo's world_touch.lua): its place and the size its parent gives it,
-- times every scale above it.
local function box(w)
  local parent = w:GetParentWidget()
  if parent == nil or not parent:IsA("inkCompoundWidget") then return nil end
  local size = parent:GetChildSize(w)
  local point = WidgetUtils.LocalToGlobal(w)
  local sx, sy, a = 1, 1, w
  for _ = 1, 32 do
    if a == nil then break end
    local s = a:GetScale()
    sx, sy = sx * s.X, sy * s.Y
    a = a:GetParentWidget()
  end
  if size.X <= 0 or size.Y <= 0 then return nil end
  return point.X, point.Y, size.X * sx, size.Y * sy
end

-- The game's answers on the wrist: { hub (1-based, as data.hubs), index (0-based), x, y, w, h } in canvas px.
local MAIN_VERT = nil
local function answerRows(h)
  local out = {}
  local list = h.game
  if list == nil then return out end
  MAIN_VERT = MAIN_VERT or CName.new("mainVerticalContainer")
  local hub = 0
  for i = 0, list:GetNumChildren() - 1 do
    local hw = list:GetWidgetByIndex(i)
    local vert = hw and hw:GetWidgetByPathName(MAIN_VERT)
    if vert ~= nil and NameToString(vert:GetName()) == "mainVerticalContainer" then -- a failed lookup returns the root
      hub = hub + 1
      for k = 0, vert:GetNumChildren() - 1 do
        local x, y, bw, bh = box(vert:GetWidgetByIndex(k))
        if x ~= nil then out[#out + 1] = { hub = hub, index = k, x = x, y = y, w = bw, h = bh } end
      end
    end
  end
  return out
end

-- How many answer rows the game's list holds (laid out or not: the screen is off until the panel shows).
local function countRows(h)
  local n = 0
  local list = h.game
  if list == nil then return 0 end
  MAIN_VERT = MAIN_VERT or CName.new("mainVerticalContainer")
  for i = 0, list:GetNumChildren() - 1 do
    local hw = list:GetWidgetByIndex(i)
    local vert = hw and hw:GetWidgetByPathName(MAIN_VERT)
    if vert ~= nil and NameToString(vert:GetName()) == "mainVerticalContainer" then n = n + vert:GetNumChildren() end
  end
  return n
end

local function selectedNow()
  local v = nil
  pcall(function()
    local defs = Game.GetAllBlackboardDefs().UIInteractions
    v = Game.GetBlackboardSystem():Get(defs):GetInt(defs.SelectedIndex)
  end)
  return v
end

local function weaponInRight(pl)
  local yes = false
  pcall(function()
    yes = Game.GetTransactionSystem():GetItemInSlot(pl, TweakDBID.new("AttachmentSlots.WeaponRight")) ~= nil
  end)
  return yes
end

local function click()
  if type(VRHapticEvent) == "function" then pcall(VRHapticEvent, HAPTIC_CLICK, 2) end
  local ok, played = false, false
  if type(VRMenuPlayWav) == "function" then ok, played = pcall(VRMenuPlayWav, CLICK_WAV, 1.0) end
  if not (ok and played) then pcall(function() Game.GetAudioSystem():Play(CName.new("ui_terminal_positive")) end) end
end

local function cursor(h, x, y, on)
  if h.cursor == nil then
    h.cursor = K.rect(h.root, 0, 0, 16, 16, K.CYAN, 0)
  end
  if on then K.move(h.cursor, x - 8, y - 8) end
  pcall(function() h.cursor:SetOpacity(on and 0.9 or 0) end)
end

-- One frame of the right hand on the panel at C (canvas centre), R (canvas right), U (canvas up).
local function pick(h, data, pl, C, R, U)
  local T = S.pick
  if weaponInRight(pl) or type(VRMenuPointer) ~= "function" or type(VRInteractionAction) ~= "function" then
    cursor(h, 0, 0, false)
    T.target, T.armed = nil, false
    return
  end
  if type(SetVRDiagCapture) == "function" then pcall(SetVRDiagCapture, 1) end
  local out = nil
  pcall(function()
    out = VRMenuPointer(Vector4.new(C[1], C[2], C[3], 1), Vector4.new(R[1], R[2], R[3], 0),
      Vector4.new(U[1], U[2], U[3], 0), Vector4.new(PLANE_W, PLANE_H, 0, 0))
  end)
  if out == nil or #out < 8 then cursor(h, 0, 0, false) return end
  local tip, ray = out[5], out[6]
  local rows = answerRows(h)
  local function at(px, py)
    for _, r in ipairs(rows) do
      if px >= r.x and px < r.x + r.w and py >= r.y and py < r.y + r.h then return r end
    end
  end
  local row, how, px, py = nil, nil, nil, nil
  if tip.w > 0.5 and tip.z < HOVER and tip.z > THROUGH then
    px, py, how = tip.x * CW, tip.y * CH, "tip"
  elseif ray.w > 0.5 then
    px, py, how = ray.x * CW, ray.y * CH, "ray"
  end
  if px ~= nil then row = at(px, py) end
  cursor(h, px or 0, py or 0, px ~= nil and row ~= nil)
  -- only the active hub's answers, and not one the game shows unavailable
  local hub = row and data.hubs[row.hub]
  local answer = hub and hub.rows[row.index + 1]
  if hub == nil or hub.id ~= data.active or answer == nil or answer.inactive then row = nil end
  local selected = selectedNow()
  if selected == nil then return end

  -- the highlight onto the pointed answer, a step at a time
  T.target = row and row.index or nil
  if T.target ~= nil and selected ~= T.target then
    local p = T.pending
    if p ~= nil and selected == p.from and S.time - p.at < STEP_LOST then
      -- the last step is not answered yet
    elseif S.time - (T.stepAt or -9) >= STEP_WAIT then
      VRInteractionAction(CName.new(selected > T.target and "ChoiceScrollUp" or "ChoiceScrollDown"))
      T.pending, T.stepAt = { from = selected, at = S.time }, S.time
    end
  else
    T.pending = nil
  end

  -- the fingertip pushed into the highlighted answer takes it; pushed into another one it only moves the highlight
  -- there, and has to come back out past ARM before a push takes anything
  if tip.w <= 0.5 or tip.z >= ARM or tip.z <= THROUGH then
    T.armed = tip.w > 0.5 and tip.z >= ARM
  elseif tip.z < PRESS then
    if T.armed and row ~= nil and row.index == selected and S.time - S.shownAt >= DEAD
        and S.time - (T.appliedAt or -9) >= COOLDOWN then
      T.appliedAt = S.time
      VRInteractionAction(CName.new("ChoiceApply"))
      click()
      note(string.format("answer %d taken by the fingertip", row.index))
    end
    T.armed = false
  end
end

---------------------------------------------------------------- the frame

local function tick(dt, menuOpen)
  if S.status ~= nil and S.status ~= S.statusTold and verbose() then
    print("[VRDialogWrist] " .. S.status)
    S.statusTold = S.status
  end
  local pl = Game.GetPlayer()
  if S.time - S.pollAt >= POLL then
    S.pollAt = S.time
    S.data = readHubs()
  end
  local data = S.data
  -- F10 > HUD "Dialogue choices on the left wrist" (xr_dialog_wrist): off, the game's list in front as before
  local on = true
  if type(VRDialogWristEnabled) == "function" then
    local ok, v = pcall(VRDialogWristEnabled)
    on = not ok or v ~= false
  end
  local want = on and data ~= nil and #data.hubs > 0 and not menuOpen and not inVehicle(pl)

  -- THE SCREEN LIVES FOR THE SESSION while the switch is on, its display switched on once OUT OF SIGHT (behindHead):
  -- its black start (see display) passes there; then it stays at the wrist, and a dialogue only shows its content.
  if not on then
    if S.screen ~= nil then
      hideOriginal(false)
      release()
      note("switched off in F10: the game's list in front")
    end
    S.tries = 0
    return
  end
  -- Made only once the HUD has its interactions hub (the game's list is spawned through it): made at once after a load,
  -- before the HUD was there, the game's list could not be made on it, and the screen was not seen again after the
  -- second load that followed (log 2026-11-22 22:29). MADE AT THE WRIST, its display off (nothing of it shows): made
  -- out of sight, the engine never gave it its canvas (no native target in the plugin's log, three screens, 22:44).
  if (S.screen == nil or S.screen.id == nil) and S.tries < TRIES then
    local hudOk, hud = pcall(hubController)
    local tr = nil
    if hudOk and hud ~= nil then
      local C, R, U = frame(dt)
      local view = Space.view()
      if C ~= nil then
        tr = transformFor(C, R, U)
      elseif view ~= nil then
        tr = transformFor(add(view.p, { 0, 0, -1 }), view.r, { 0, 0, 1 })
      end
    end
    if tr ~= nil then
      S.screen = spawn(ENT, tr)
      S.screen.at = S.time
      if type(D.language) == "function" then pcall(function() L.set(D.language()) end) end
    end
  end
  local h = S.screen
  -- A screen that does not come ready in READY_WAIT, or that the game took away (a load), is made again, TRIES at most.
  if h ~= nil and not h.displayOn and S.time - (h.at or S.time) >= READY_WAIT then
    S.tries = S.tries + 1
    note(string.format("the wrist's screen did not come ready in %.0f s (spawned %s, found %s, drawn on %s): %s",
      READY_WAIT, tostring(h.id ~= nil), tostring(h.entity ~= nil), tostring(h.built == true),
      S.tries < TRIES and "made again" or "given up, the game's list in front"))
    hideOriginal(false)
    release()
    h = nil
  elseif h ~= nil and h.displayOn and S.time - (h.checkedAt or 0) >= 1.0 then
    h.checkedAt = S.time
    local alive = false
    pcall(function() alive = Game.FindEntityByID(h.id) ~= nil end)
    if not alive then
      note("the game took the wrist's screen away (a load?): it is made again")
      hideOriginal(false)
      release()
      h = nil
    end
  end
  if h ~= nil and resolve(h) then
    if not h.built then buildFrame(h) end
    if not h.gpu then
      local ok, done = pcall(VRMenuSurfaceReady, h.comp, h.root)
      h.gpu = ok and done == true
    end
    -- Its canvas given, it goes out of sight (behindHead, below), and its display goes on there once the move has
    -- reached the screen (a spawned entity's move shows a frame late); back at the wrist WARM_UP after.
    if h.gpu and not h.displayOn then
      h.hideAt = h.hideAt or S.time
      if S.time - h.hideAt >= MOVE_WAIT then
        pcall(function() h.entity:FindComponentByName(CName.new("display")):Toggle(true) end)
        if h.warm ~= nil then pcall(function() h.root:RemoveChild(h.warm) end); h.warm = nil end
        h.displayOn, h.warmAt, S.tries = true, S.time, 0
        note(string.format("the wrist's screen is on, out of sight, %.1f s after its spawn, for %.1f s before its first use",
          S.time - h.at, WARM_UP))
      end
    end
  end
  local warmed = h ~= nil and h.built and h.displayOn and S.time - h.warmAt >= WARM_UP

  if not want or not warmed then
    if S.showing then
      note("hidden")
      S.showing = false
    end
    hideOriginal(false)
    S.pick.target, S.pick.pending, S.pick.armed = nil, nil, false
    if h ~= nil then
      h.readyAt, h.unready = nil, nil
      display(h, false)
      -- at the wrist until its canvas is given (see the spawn), behind the head while its display warms up, then at
      -- the wrist for good, its content transparent until a dialogue (see the top)
      local tr = nil
      if h.gpu and not warmed then
        tr = behindHead()
      else
        local C, R, U = frame(dt)
        if C ~= nil then tr = transformFor(C, R, U) end
      end
      if tr ~= nil and h.entity ~= nil then place(h, tr) end
      if warmed and not h.atWrist then
        h.atWrist = true
        note("the wrist's screen is warmed up: it stays at the wrist from now on")
      end
    end
    return
  end
  local C, R, U = frame(dt)
  if C ~= nil then place(h, transformFor(C, R, U)) end

  local sig = signature(data)
  local changed = sig ~= S.sig
  if changed then
    S.sig, S.hl, S.shownAt = sig, nil, S.time
    local n = 0
    for _, hub in ipairs(data.hubs) do n = n + #hub.rows end
    note(string.format("answers: %d hub(s), %d row(s), active hub %d, selected %d", #data.hubs, n, data.active, data.selected))
  end
  if h.mode == "game" and h.waited ~= nil then
    if h.game:GetNumChildren() > 1 then -- its controller spawned the game's hubs: it is alive
      h.waited = nil
      status("the game's own dialogue list runs on the wrist (its controller is alive)")
    else
      h.waited = h.waited + dt
      if h.waited < GAME_WAIT then return end
      h.mode, h.waited, changed = "hubs", nil, true
      status("the dialog item's controller did not come alive in " .. GAME_WAIT .. " s: its hubs are driven from here")
    end
  end
  local fitted = true
  if h.mode == "hubs" then
    local hl = tostring(data.active) .. ":" .. tostring(data.selected)
    if changed or hl ~= S.hl then
      local ok, done, why = pcall(driveHubs, h, data)
      S.hl = hl
      if not ok or not done then
        status("the game's hubs could not be driven (" .. tostring(ok and why or done) .. "): the drawn panel stands in")
        pcall(function() h.game:SetVisible(false) end)
        h.mode, h.game, S.sig = "drawn", nil, nil
        return
      end
    end
    fitted = fitGame(h, dt)
  elseif h.mode == "game" then
    fitted = fitGame(h, dt)
  else
    if changed then drawPanel(h, data) end
    local hl = tostring(data.active) .. ":" .. tostring(data.selected)
    if hl ~= S.hl then
      paintHighlight(data)
      S.hl = hl
    end
  end

  -- THE GAME'S ROWS ALL THERE before it shows: at each line the game takes its hubs away and makes them anew, the hub at
  -- once and its answers a frame or two after (spawned asynchronously). Held ROWS_SETTLE once they are; past ROWS_MOST
  -- shown however they are.
  local ready = true
  if h.mode ~= "drawn" and not S.showing then
    local total = 0
    for _, hub in ipairs(data.hubs) do total = total + #hub.rows end
    local okr, n = pcall(countRows, h)
    local all = okr and n >= total
    if all then h.readyAt = h.readyAt or S.time else h.readyAt = nil end
    h.unready = (h.unready or 0) + dt
    ready = (all and S.time - h.readyAt >= ROWS_SETTLE) or h.unready >= ROWS_MOST
    if ready and not all then status("the game's rows were not all there after " .. ROWS_MOST .. " s: shown anyway") end
  end

  if C == nil then
    display(h, false)
    hideOriginal(false)
    return
  end
  if not h.gpu then return end
  if h.warm ~= nil then h.root:RemoveChild(h.warm); h.warm = nil end
  display(h, fitted and ready)
  if not (fitted and ready) then return end
  if not S.showing then
    S.showing, S.shownAt = true, S.time
    note(string.format("shown at (%.2f, %.2f, %.2f), %.2f s after the answers came", C[1], C[2], C[3],
      h.unready or 0))
    h.readyAt, h.unready = nil, nil
  end
  hideOriginal(true)
  -- The hands' physics: while the answers are on the wrist the two hands do not meet (the right finger pushed the left
  -- hand and the panel away, user report 2026-11-22); asked again every frame, so it ends 0.3 s after the panel goes.
  if type(VRHandsKeepApart) == "function" then pcall(VRHandsKeepApart, 300) end
  if h.mode ~= "drawn" then
    local ok, err = pcall(pick, h, data, pl, C, R, U)
    if not ok and tostring(err) ~= S.pickError then
      S.pickError = tostring(err)
      print("[VRDialogWrist] picking by hand failed: " .. S.pickError)
    end
  end
end

---------------------------------------------------------------- driven by init.lua

function DW.update(dt, menuOpen)
  S.time = S.time + (dt or 0)
  if Game.GetPlayer() == nil then
    if S.screen ~= nil then S.screen, S.sig, S.hl, S.rows, S.showing = nil, nil, nil, {}, false end
    S.tries = 0
    return
  end
  local ok, err = pcall(tick, dt or 0, menuOpen)
  if not ok then
    if tostring(err) ~= S.lastError then print("[VRDialogWrist] " .. tostring(err)) end
    S.lastError = tostring(err)
  end
end

function DW.release()
  hideOriginal(false)
  release()
end

-- For the console: GetMod("CyberpunkVRPort_Menu").dialogWrist()
function DW.state()
  return { showing = S.showing, data = S.data, panelH = S.panelH, rows = #S.rows, error = S.lastError }
end

return DW
