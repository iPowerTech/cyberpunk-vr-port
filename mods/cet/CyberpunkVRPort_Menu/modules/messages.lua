-- The phone section of the VR menu: the game's messenger on the menu's glass, the way the phone shows
-- it (newHudPhoneGameController): two tabs, MESSAGES and CONTACTS.
--
--   MESSAGES  the threads (data.lua D.threads): quest ones first with the "!", unread and waiting-for-a-
--             reply ones lit, the rest dim; a reply envelope or the time on the right; the last row shows
--             or hides the read ones, as the phone's own last row does. A thread opens on the right: its
--             messages as the phone's bubbles (the NPC's on the left, the player's on the right, quest ones
--             yellow, pictures shown), the reply options under them, CALL when the contact can be called.
--             Sending a reply is the messenger's own (D.reply); the answer that comes back arrives the
--             way it does in the game -- the journal schedules it, "<name> is typing" shows from half its
--             delay, then it lands -- heard through the redscript watcher (D.journalWatch).
--   CONTACTS  the callable and non-empty contacts with their pictures; one opens on the right with CALL
--             and MESSAGES, and MESSAGES lists that contact's threads.
--
-- menu.lua hands in its drawing kit (kit) and calls Msg.build when the section is drawn, Msg.update every
-- frame it is open.

local K = require("modules/ink")
local D = require("modules/data")

local Msg = {}

local GREEN = { 0.36, 1.0, 0.72 }       -- the player's own bubbles, as the phone draws them
local SOFT = { 1.0, 0.76, 0.72 }
local ROW2 = 124                         -- a thread's row: name over preview
local BUBBLE_W = 640                     -- a bubble's widest
local FS = 34                            -- bubble text
local LINE = 42
local POLL = 0.25                        -- s: how often the journal's counters are read
local THREAD_W = 900                     -- the thread panel: wider than a detail panel, a chat needs it

-- the phone's state, kept across redraws (and in the menu's S, across a hot reload of this file)
local function ph(kit)
  local S = kit.S
  S.phone = S.phone or { tab = "messages", showRead = false }
  return S.phone
end

local function same(a, b)
  return a ~= nil and b ~= nil and a.hash == b.hash and (a.conversationHash or 0) == (b.conversationHash or 0)
end

---------------------------------------------------------------- text

local function ulen(s)
  local _, n = string.gsub(s or "", "[^\128-\191]", "")
  return n
end

local function wrap(s, n)
  local lines = {}
  for para in string.gmatch((s or "") .. "\n", "(.-)\n") do
    local line, len = "", 0
    for word in string.gmatch(para, "%S+") do
      local wl = ulen(word)
      if len > 0 and len + 1 + wl > n then
        lines[#lines + 1] = line
        line, len = word, wl
      elseif len > 0 then
        line, len = line .. " " .. word, len + 1 + wl
      else
        line, len = word, wl
      end
    end
    lines[#lines + 1] = line
  end
  while #lines > 1 and lines[#lines] == "" do lines[#lines] = nil end
  return lines
end

local function charsFor(w, fs) return math.max(8, math.floor(w / (fs * 0.50))) end

---------------------------------------------------------------- the tabs

local function tabs(kit, p, w)
  local T, S = kit.T, ph(kit)
  local items = { { key = "messages", label = T.messagesTab }, { key = "contacts", label = T.contactsTab } }
  -- inside the glass with their outline: the glass is inset half the panel's corner from its edge
  local gap, y, h, x0 = 24, 26, kit.HEAD - 40, kit.PAD + 12
  local tw = (w - x0 - 72 - gap) / 2      -- the right one ends left of the frame's corner mark
  for i, tab in ipairs(items) do
    local x = x0 + (i - 1) * (tw + gap)
    local on = S.tab == tab.key
    local bg = K.roundFill(p, x, y, tw, h, on and K.CYAN or K.ICE, on and 0.14 or 0.04, h / 2)
    local edge = K.roundEdge(p, x, y, tw, h, K.CYAN, on and 0.9 or 0.0, h / 2)
    local txt = K.text(p, x, y, tw, h, tab.label, 36, on and K.WHITE or K.TEXT, "center")
    kit.button("p1", kit.at(kit.P1X, kit.P1Y), x, y, tw, h, function()
      if S.tab == tab.key then return end
      S.tab, S.thread, S.contact, S.filter = tab.key, nil, nil, nil
      kit.rebuild(true, false)
    end, function(hover)
      if on then return end
      bg:SetTintColor(K.color(hover and K.YELLOW or K.ICE))
      bg:SetOpacity(hover and 0.22 or 0.04)
      K.tint(edge, K.CYAN, hover and 1.0 or 0.0)
      txt:SetTintColor(K.color(hover and K.WHITE or K.TEXT))
    end)
  end
end

---------------------------------------------------------------- the threads

local function threadRow(kit, t, selected, click)
  return { h = ROW2, build = function(itc, it, v)
    local w, h = v.cw, ROW2 - 14
    local c = K.canvas(itc, 0, 0, w, h)
    local bg = K.roundFill(c, 0, 0, w, h, K.ICE, 0.06, kit.R_CELL)
    local edge = K.roundEdge(c, 0, 0, w, h, K.ICE, 0.0, kit.R_CELL)
    local x = 26
    if t.avatar ~= nil then
      K.image(c, x, (h - 76) / 2, 76, 76, t.avatar.atlas, t.avatar.part, K.WHITE, 1.0)
      x = x + 76 + 22
    end
    local live = t.unread > 0 or t.canReply
    local nameCol = t.quest and K.YELLOW or (live and K.CYAN or K.GREY)
    local nx = x
    if t.quest then
      K.image(c, x, 14, 36, 36, K.ATLAS.common, "ico_exclamation_circle", K.YELLOW, 1.0)
      nx = x + 46
    end
    local RIGHT = 130
    local name = K.text(c, nx, 4, w - nx - RIGHT, 54, K.clip(K.upper(t.name), 24), 40, nameCol)
    K.text(c, x, 54, w - x - RIGHT, 48, K.clip(t.preview, 36), 32, live and SOFT or K.GREY, nil, true)
    if t.canReply then
      K.image(c, w - 40 - 56, (h - 40) / 2, 56, 40, K.ATLAS.common, "ico_envelelope_reply1", t.quest and K.YELLOW or K.CYAN, 1.0)
    else
      K.text(c, w - RIGHT - 30, 0, RIGHT, h, t.time, 32, K.GREY, "right", true)
    end
    if t.unread > 0 then
      K.image(c, w - RIGHT - 48, (h - 18) / 2, 18, 18, K.ATLAS.phone, "circle", t.quest and K.YELLOW or K.CYAN, 1.0)
    end
    local b
    b = kit.button("p1", kit.at(v.x, it.y, v), 0, 0, w, h, click, function(hover)
      local sel = b ~= nil and b.selected
      local lit = hover and K.YELLOW or (sel and K.YELLOW or nil)
      bg:SetTintColor(K.color(lit or K.ICE))
      bg:SetOpacity(hover and 0.24 or (sel and 0.16 or 0.06))
      K.tint(edge, lit or K.ICE, lit and 1.0 or 0.0)
      name:SetTintColor(K.color(hover and K.WHITE or (sel and K.YELLOW or nameCol)))
    end)
    b.selected = selected
    b.paint(false)
    if kit.S.anim1 and v.first then kit.slideIn("p1", c, 0, 0, 0.03 + it.index * 0.035, 0, 36) end
  end }
end

local function messagesList(kit, p1)
  local T, S = kit.T, ph(kit)
  local threads = D.threads(S.showRead or S.filter ~= nil, S.filter and S.filter.hash)
  -- a thread that dropped out of the list (read and filtered away) stays open where it is
  local rows = {}
  if S.filter ~= nil then
    rows[#rows + 1] = kit.listRow("p1", "‹  " .. T.allMessages, function()
      S.filter, S.thread = nil, nil
      kit.rebuild(false, false)
    end, { clip = 30 })
  end
  for _, t in ipairs(threads) do
    rows[#rows + 1] = threadRow(kit, t, same(S.thread, t), function()
      if same(S.thread, t) then
        S.thread = nil
      else
        S.thread, S.threadSeen = t, nil
      end
      kit.rebuild(false, S.thread ~= nil)
    end)
  end
  if S.filter == nil then
    rows[#rows + 1] = kit.listRow("p1", S.showRead and T.hideRead or T.showRead, function()
      S.showRead = not S.showRead
      kit.rebuild(false, false)
    end, { clip = 32 })
  end
  local h = kit.listSize(rows, kit.LIST_H)
  kit.glass(p1, kit.P1W, kit.HEAD + math.max(h, kit.ROW) + 14, kit.P1X, kit.P1Y)
  tabs(kit, p1, kit.P1W)
  kit.list(p1, "p1", "p1", kit.P1X, kit.P1Y, kit.HEAD, kit.P1W - 2 * kit.PAD, rows, kit.LIST_H, kit.S.keepScroll)
end

---------------------------------------------------------------- a thread

local function bubbleRow(m)
  local lines = wrap(m.text, charsFor(BUBBLE_W - 76, FS))
  local widest = 0
  for _, l in ipairs(lines) do widest = math.max(widest, ulen(l)) end
  local textH = (m.text ~= "" and #lines or 0) * LINE
  local imgH = m.image ~= nil and 300 or 0
  local bh = math.max(84, textH + imgH + 44)
  local bw = m.image ~= nil and BUBBLE_W or math.min(BUBBLE_W, math.max(200, math.floor(widest * FS * 0.50) + 80))
  return { h = bh + 18, build = function(itc, it, v)
    local x = m.player and (v.cw - bw - 6) or 6
    local col = m.quest and K.YELLOW or (m.player and GREEN or K.CYAN)
    local part = m.player and "msgBuble_reply" or "msgBuble"
    K.slice(itc, x, 0, bw, bh, K.ATLAS.phone, part .. "_bg", col, 0.12)
    K.slice(itc, x, 0, bw, bh, K.ATLAS.phone, part .. "_fg", col, 0.85)
    if textH > 0 then
      K.text(itc, x + 36, 20, bw - 72, textH, table.concat(lines, "\n"), FS, m.player and K.WHITE or SOFT, nil, true)
    end
    if m.image ~= nil then
      K.image(itc, x + 30, 22 + textH, bw - 60, imgH - 12, m.image.atlas, m.image.part, K.WHITE, 1.0)
    end
  end }
end

local function typingRow(kit, name, player)
  return { h = 130, build = function(itc, it, v)
    local bw, bh = 170, 84
    local x = player and (v.cw - bw - 6) or 6
    local col = player and GREEN or K.CYAN
    local part = player and "msgBuble_reply" or "msgBuble"
    if not player and name ~= nil then
      local label = string.gsub(D.locKey("UI-Phone-isTyping", "{contact_name}…"), "{contact_name}", name)
      K.text(itc, x + 10, 0, v.cw - 20, 40, label, 28, K.CYAN, nil, true)
    end
    local y = 42
    K.slice(itc, x, y, bw, bh, K.ATLAS.phone, part .. "_bg", col, 0.10)
    K.slice(itc, x, y, bw, bh, K.ATLAS.phone, part .. "_fg", col, 0.7)
    -- the dots breathe one after another, as the phone's (Msg.update steps them)
    local dots = {}
    for i = 0, 2 do
      dots[#dots + 1] = K.image(itc, x + 44 + i * 32, y + 32, 20, 20, K.ATLAS.phone, "circle", col, 0.35)
    end
    ph(kit).dots = dots
  end }
end

-- Whether the watcher's last scheduled message belongs to the open thread, and its typing window.
local function typingFor(kit, t)
  local S = ph(kit)
  local ty = S.typing
  if ty == nil or t == nil then return nil end
  if ty.contact ~= t.hash then return nil end
  if t.multi and ty.conversation ~= 0 and ty.conversation ~= t.conversationHash then return nil end
  local now = kit.S.time
  if now < ty.from or now > ty.till then return nil end
  return ty
end

local function replyButton(kit, p, H, x, y, w, r, send)
  local lines = wrap(r.text, charsFor(w - 80, FS))
  local h = math.max(84, #lines * LINE + 40)
  local col = r.quest and K.YELLOW or GREEN
  local bg = K.slice(p, x, y, w, h, K.ATLAS.phone, "msgBuble_reply_bg", col, 0.22)
  local fg = K.slice(p, x, y, w, h, K.ATLAS.phone, "msgBuble_reply_fg", col, 1.0)
  local txt = K.text(p, x + 36, y + 20, w - 76, h - 40, table.concat(lines, "\n"), FS, K.WHITE, nil, true)
  kit.button("p2", H, x, y, w, h, send, function(hover)
    bg:SetTintColor(K.color(hover and K.YELLOW or col))
    bg:SetOpacity(hover and 0.34 or 0.22)
    fg:SetTintColor(K.color(hover and K.YELLOW or col))
    txt:SetTintColor(K.color(hover and K.WHITE or K.WHITE))
  end)
  return h
end

local function threadPanel(kit, p2, t)
  local T, S = kit.T, ph(kit)
  local w = THREAD_W
  local data = D.thread(t)
  D.markRead(data.messages)
  S.threadCount = #data.messages
  local last = data.messages[#data.messages]
  S.threadLastPlayer = last ~= nil and last.player
  S.dots = nil
  local H2 = kit.at(kit.P2X, kit.P1Y)
  local rows = {}
  for _, m in ipairs(data.messages) do rows[#rows + 1] = bubbleRow(m) end
  local ty = typingFor(kit, t)
  S.typingShown = ty ~= nil
  if ty ~= nil then rows[#rows + 1] = typingRow(kit, t.name, ty.player) end
  -- what goes under the messages: the replies, and CALL
  local inner = w - 2 * kit.PAD
  local replyW = math.min(inner - 40, BUBBLE_W + 120)
  local replyH = 0
  for _, r in ipairs(data.replies) do
    replyH = replyH + math.max(84, #wrap(r.text, charsFor(replyW - 80, FS)) * LINE + 40) + 14
  end
  local callH = t.callable and (kit.ROW + 14) or 0
  local avail = math.max(300, kit.LIST_H - replyH - callH)
  local listH = math.max(kit.listSize(rows, avail), 120)
  local total = kit.HEAD + listH + (replyH > 0 and replyH + 16 or 0) + callH + 20
  kit.glass(p2, w, total, kit.P2X, kit.P1Y)
  kit.header(p2, w, K.clip(K.upper(t.name), 26), t.quest and K.YELLOW or K.CYAN)
  local v = kit.list(p2, "p2", "p2", kit.P2X, kit.P1Y, kit.HEAD, inner, rows, avail, 1e9)
  if #rows == 0 then K.text(p2, kit.PAD + 30, kit.HEAD, inner - 60, kit.ROW, T.noMessages, 40, K.GREY) end
  local y = kit.HEAD + listH + 16
  for _, r in ipairs(data.replies) do
    local hh = replyButton(kit, p2, H2, kit.PAD + inner - replyW, y, replyW, r, function()
      if D.reply(data.replies, r) then
        kit.play("notify")
        S.sentAt = kit.S.time
      end
      kit.rebuild(false, false)
    end)
    y = y + hh + 14
  end
  if t.callable then
    kit.action(p2, "p2", H2, kit.PAD, y, inner, kit.ROW, T.call, D.callingEnabled(), function()
      local ok, why = D.call({ callable = true, contactId = t.contactId })
      kit.toast(ok and T.calling or (T[why] or T.blocked))
      if ok then kit.S.closeAt = kit.S.time + 0.9 end
    end, K.CYAN, kit.GLYPH.call)
  end
  return v
end

---------------------------------------------------------------- contacts

local function initial(c, x, w, h, name)
  local ch = string.match(name or "", "^[%z\1-\127\194-\244][\128-\191]*") or "?"
  local s = math.min(w, h - 12)
  K.image(c, x + (w - s) / 2, (h - s) / 2, s, s, K.ATLAS.phone, "circle", K.CYAN, 0.25)
  return K.text(c, x, 0, w, h, K.upper(ch), 46, K.CYAN, "center")
end

local function contactsList(kit, p1)
  local S = ph(kit)
  S.contacts = S.contacts or D.contacts()
  local rows = {}
  for _, c in ipairs(S.contacts) do
    local o = {
      iconW = 84,
      icon = function(rc, x, y, w, h)
        if c.avatar ~= nil then
          local s = math.min(w, h - 12)
          K.roundFill(rc, x + (w - s) / 2 - 3, (h - s) / 2 - 3, s + 6, s + 6, K.CYAN, 0.35, 8)
          return K.image(rc, x + (w - s) / 2, (h - s) / 2, s, s, c.avatar.atlas, c.avatar.part, K.WHITE, 1.0)
        end
        return initial(rc, x, w, h, c.name)
      end,
      tintIcon = c.avatar == nil,
      selected = S.contact ~= nil and S.contact.hash == c.hash,
    }
    if c.unread > 0 then o.right, o.rightW = tostring(c.unread), 90 end
    rows[#rows + 1] = kit.listRow("p1", c.name, function()
      S.contact = (S.contact ~= nil and S.contact.hash == c.hash) and nil or c
      kit.rebuild(false, S.contact ~= nil)
    end, o)
  end
  local h = kit.listSize(rows, kit.LIST_H)
  kit.glass(p1, kit.P1W, kit.HEAD + math.max(h, kit.ROW) + 14, kit.P1X, kit.P1Y)
  tabs(kit, p1, kit.P1W)
  if #rows == 0 then
    K.text(p1, 34, kit.HEAD, kit.P1W - 68, kit.ROW, kit.T.empty, 44, K.GREY)
    return
  end
  kit.list(p1, "p1", "p1", kit.P1X, kit.P1Y, kit.HEAD, kit.P1W - 2 * kit.PAD, rows, kit.LIST_H, kit.S.keepScroll)
end

local function contactPanel(kit, p2, c)
  local T, S = kit.T, ph(kit)
  local w = kit.P2W
  local H2 = kit.at(kit.P2X, kit.P1Y)
  kit.glass(p2, w, kit.HEAD + (c.avatar ~= nil and 216 or 0) + 2 * (kit.ROW + 30) + 10, kit.P2X, kit.P1Y)
  kit.header(p2, w, K.clip(K.upper(c.name), 18), K.YELLOW)
  local y = kit.HEAD
  if c.avatar ~= nil then
    K.roundFill(p2, kit.PAD, y, 196, 196, K.YELLOW, 0.8, 10)
    K.image(p2, kit.PAD + 3, y + 3, 190, 190, c.avatar.atlas, c.avatar.part, K.WHITE, 1.0)
    y = y + 216
  end
  kit.action(p2, "p2", H2, kit.PAD, y, w - 2 * kit.PAD, kit.ROW + 10, T.call, c.callable and D.callingEnabled(), function()
    local ok, why = D.call(c)
    kit.toast(ok and T.calling or (T[why] or T.blocked))
    if ok then kit.S.closeAt = kit.S.time + 0.9 end
  end, nil, kit.GLYPH.call)
  y = y + kit.ROW + 30
  kit.action(p2, "p2", H2, kit.PAD, y, w - 2 * kit.PAD, kit.ROW + 10, T.messages, c.messages, function()
    -- that contact's threads, in the messages tab; the only one opens at once
    S.tab, S.filter, S.contact = "messages", { hash = c.hash, name = c.name }, nil
    local list = D.threads(true, c.hash)
    S.thread = #list == 1 and list[1] or nil
    kit.rebuild(true, S.thread ~= nil)
  end, nil, kit.GLYPH.message)
end

---------------------------------------------------------------- driven by menu.lua

function Msg.build(kit, p1, p2)
  local S = ph(kit)
  if S.tab == "contacts" then
    contactsList(kit, p1)
    if S.contact ~= nil then contactPanel(kit, p2, S.contact) end
  else
    messagesList(kit, p1)
    if S.thread ~= nil then threadPanel(kit, p2, S.thread) end
  end
end

-- The section closed or another opened: the next visit starts from the lists, fresh.
function Msg.leave(kit)
  local S = ph(kit)
  S.thread, S.contact, S.filter, S.contacts, S.typing = nil, nil, nil, nil, nil
end

-- Every frame the section is open: the journal's counters; a change redraws the panels, a message
-- scheduled for the open thread shows the typing dots, a new one from the other side sounds as the
-- phone's does. Returns true when it redrew.
function Msg.update(kit, dt)
  local S = ph(kit)
  if S.dots ~= nil then
    local lit = math.floor((kit.S.time * 3) % 3) + 1
    for i, d in ipairs(S.dots) do pcall(function() d:SetOpacity(i == lit and 1.0 or 0.35) end) end
  end
  S.pollIn = (S.pollIn or 0) - dt
  if S.pollIn > 0 then return false end
  S.pollIn = POLL
  local w = D.journalWatch()
  local redraw = false
  if w ~= nil then
    local updates, scheduled = w.updates, w.scheduled
    if S.seenScheduled ~= nil and scheduled ~= S.seenScheduled then
      local delay = tonumber(w.lastDelay) or 0
      local now = kit.S.time
      -- the phone starts the dots halfway to a message from the other side, a little later for more
      -- from the same side; they go when it lands
      S.typing = {
        contact = w.lastContact, conversation = w.lastConversation, player = w.lastFromPlayer,
        from = now + delay * 0.5, till = now + delay + 0.5,
      }
      if not w.lastTyping then S.typing = nil end
    end
    if S.seenUpdates ~= nil and updates ~= S.seenUpdates then
      redraw = true
      if S.typing ~= nil and kit.S.time >= S.typing.from then S.typing = nil end
      S.contacts = nil
    end
    S.seenUpdates, S.seenScheduled = updates, scheduled
  end
  local nowTyping = S.thread ~= nil and typingFor(kit, S.thread) ~= nil
  if nowTyping ~= (S.typingShown == true) then redraw = true end
  if not redraw then return false end
  local before = S.thread ~= nil and S.threadCount or nil
  kit.rebuild(false, false)
  if before ~= nil and S.thread ~= nil and (S.threadCount or 0) > before and not S.threadLastPlayer then
    -- something new from the other side in the open thread: the phone's own arrival sound
    kit.sound("ui_messenger_recieved")
  end
  return true
end

Msg.THREAD_W = THREAD_W

return Msg
