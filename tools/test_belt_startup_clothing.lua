-- Pure mocks: run with LuaJIT or pass the source to the live CET LuaJIT compiler.
-- Returns a test function; no live Game API or real inventory is accessed.
return function(source)
    local cases = 0
    local function check(value, message) assert(value, message) end
    local chunk = assert(loadstring(source))
    local module = chunk()
    local waist, shirt, head = "OutfitSlots.Waist", "OutfitSlots.TorsoAux", "OutfitSlots.Head"
    local function item(record, identity)
        return { GetItemID = function() return record end, GetEntityID = function() return identity end }
    end
    local function part(record, slot)
        return { GetItemID = function() return record end, GetSlotID = function() return slot end }
    end
    local function fixture(active, custom)
        local state = { slots = {}, parts = { waist }, equipped = {}, refreshed = {}, active = active,
            base = { part("helmet", "AttachmentSlots.Head"), part("vest", "AttachmentSlots.Torso") } }
        state.slots[waist] = item("belt", 10)
        if custom then state.parts[#state.parts + 1] = shirt; state.slots[shirt] = item("custom", 11) end
        local outfit = {
            IsBlocked = function() return state.blocked end,
            IsActive = function() return state.active end,
            GetUsedSlots = function()
                if state.readFails then state.readFails = false; error("not ready") end
                return state.parts
            end,
            GetEquipmentParts = function() return state.base end,
            GetItemSlot = function(_, id) return id == "helmet" and head or shirt end,
            IsOutfitSlot = function(_, slot) return slot == head or slot == shirt end,
            IsOccupied = function(_, slot)
                for _, s in ipairs(state.parts) do if s == slot then return true end end
                return false
            end,
            EquipItem = function(_, id, slot)
                if state.equipFails == id then state.equipFails = nil; return false end
                state.parts[#state.parts + 1] = slot
                state.equipped[#state.equipped + 1] = { record = id, slot = slot }
                return true -- accepted, still absent until the async attach completes
            end,
        }
        local transactions = {
            GetItemInSlot = function(_, _, slot) return state.slots[slot] end,
            RefreshAttachment = function(_, _, slot)
                if state.refreshFails == slot then state.refreshFails = nil; error("temporary refresh failure") end
                state.refreshed[#state.refreshed + 1] = slot
            end,
        }
        local job = module.new({
            key = tostring,
            record = function(i) return i:GetItemID() end,
            entityKey = function(i) return tostring(i:GetEntityID()) end,
        })
        local function tick(now) return job:Tick(now, {}, transactions, outfit, waist, "belt") end
        local function finishAttach()
            for _, p in ipairs(state.equipped) do state.slots[p.slot] = item(p.record, p.slot) end
        end
        return state, job, tick, finishAttach
    end

    do
        local s, job, tick, attach = fixture(true)
        s.slots[waist] = nil
        tick(0); tick(5); check(not job.done and #s.equipped == 0, "refresh ran before belt attachment")
        s.slots[waist] = item("belt", 10)
        tick(6); tick(6.9); check(#s.equipped == 0, "belt did not get a settling interval")
        local queued = tick(7)
        check(queued.changed and not queued.done and #s.equipped == 2 and #s.refreshed == 0,
            "async restore did not invalidate cached garment components")
        tick(8); check(not job.done and #s.refreshed == 0, "refresh ran before restored items appeared")
        attach(); local result = tick(9)
        check(result.ok and result.changed and result.restored == 2 and result.refreshed == 2, "first refresh was incomplete")
        for t = 10, 100 do tick(t) end
        check(#s.refreshed == 2 and #s.equipped == 2, "startup refresh repeated during ordinary ticks")
        job:Reset(); tick(101); result = tick(102)
        check(result.ok and #s.refreshed == 4 and #s.equipped == 2, "new save with reused entity ID was not refreshed once")
        cases = cases + 1
    end
    do
        local s, _, tick = fixture(true, true)
        s.parts[#s.parts + 1] = "OutfitSlots.HandPropRight"
        s.slots["OutfitSlots.HandPropRight"] = item("prop", 12)
        tick(0); local result = tick(1)
        check(result.ok and #s.equipped == 0 and #s.refreshed == 1, "custom outfit or held prop was changed")
        check(s.slots[shirt]:GetItemID() == "custom" and s.refreshed[1] == shirt, "custom item was replaced")
        cases = cases + 1
    end
    do
        local s, _, tick = fixture(false)
        s.slots["AttachmentSlots.Head"] = item("helmet", 20)
        s.slots["AttachmentSlots.Torso"] = item("vest", 21)
        tick(0); local result = tick(1)
        check(result.ok and #s.equipped == 0 and #s.refreshed == 2 and not s.active, "direct attach activated wardrobe mode")
        cases = cases + 1
    end
    do
        local s, _, tick, attach = fixture(true)
        s.equipFails = "vest"
        tick(0); check(not tick(1).ok, "failed second equip was ignored")
        tick(2); check(#s.equipped == 2, "partial repair was mistaken for a custom outfit")
        attach(); check(tick(3).ok and #s.refreshed == 2, "partial repair did not finish")
        cases = cases + 1
    end
    do
        local s, _, tick, attach = fixture(true)
        s.readFails = true
        tick(0); check(not tick(1).ok, "unavailable outfit state was accepted")
        tick(2); attach(); check(tick(3).ok and #s.equipped == 2, "failed plan read silently lost the repair")
        cases = cases + 1
    end
    do
        local s, _, tick = fixture(true, true)
        s.parts[#s.parts + 1] = head; s.slots[head] = item("helmet", 20)
        s.refreshFails = head
        tick(0); check(not tick(1).ok, "partial refresh failure was ignored")
        check(tick(2).ok and #s.refreshed == 2, "successful slot refreshed twice after an error")
        cases = cases + 1
    end
    do
        local s, job, tick = fixture(true)
        s.blocked = true
        tick(0); tick(1); check(not job.done and #s.equipped == 0, "quest wardrobe lock was bypassed")
        for t = 2, 125 do tick(t) end
        check(job.done and #s.equipped == 0 and #s.refreshed == 0, "blocked startup repair retried indefinitely")
        cases = cases + 1
    end
    return { cases = cases, ok = true }
end
