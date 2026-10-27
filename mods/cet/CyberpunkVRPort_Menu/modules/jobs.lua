-- Background reads for the menu. CET's sandbox has no `coroutine`, so a producer is a factory that
-- returns a step function: each call does a small piece of work and returns `done, value, stamp`.
-- J.step runs queued producers a step at a time inside a per-frame time budget, so a journal or
-- inventory read never stalls one frame. Results outlive the producer (and a menu close): a panel
-- draws the last result at once and redraws when a newer one differs (`stamp`).
local J={}
local results,running,queue={}, {}, {}
local clock=os.clock

-- The last finished result of `key`, and its record {value, stamp, at, seq}.
function J.get(key) local r=results[key];return r and r.value,r end
function J.has(key) return results[key]~=nil end
function J.busy(key) return running[key]~=nil end
-- Forget a result (the next get is empty until the producer runs again).
function J.drop(key) results[key]=nil end
-- Forget every result whose key starts with `prefix`.
function J.dropPrefix(prefix)
  for key in pairs(results) do if key:sub(1,#prefix)==prefix then results[key]=nil end end
end
-- Replace a result in place (a local edit the next read would only confirm).
function J.put(key,value,stamp)
  local old=results[key]
  results[key]={value=value,stamp=stamp,at=clock(),seq=(old and old.seq or 0)+1}
end

-- Run a producer to its end now and return its value and stamp (for a caller that cannot wait).
function J.now(factory)
  local step=factory()
  while true do
    local done,value,stamp=step()
    if done then return value,stamp end
  end
end

-- Queue `factory` under `key` unless it is already running; a stamp equal to the previous result's
-- keeps that record (and its seq), so a reader can skip a redraw. `front` puts it ahead of the queue
-- (what the panel on screen waits for).
function J.run(key,factory,front)
  if running[key] then
    if front then for i,k in ipairs(queue) do if k==key then table.remove(queue,i);table.insert(queue,1,key);break end end end
    return
  end
  running[key]={factory=factory}
  if front then table.insert(queue,1,key) else queue[#queue+1]=key end
end

local function finish(key,ok,value,stamp)
  running[key]=nil
  for i,k in ipairs(queue) do if k==key then table.remove(queue,i);break end end
  if not ok then print("[VRMenu] job "..key..": "..tostring(value));return end
  local old=results[key]
  if old and stamp~=nil and old.stamp==stamp then old.at=clock();return end
  results[key]={value=value,stamp=stamp,at=clock(),seq=(old and old.seq or 0)+1}
end

-- One step of `key`; true when it has finished (or failed).
local function advance(key)
  local job=running[key];if not job then return true end
  local ok,done,value,stamp=pcall(function()
    if not job.step then job.step=job.factory();return false end
    return job.step()
  end)
  if not ok then finish(key,false,done);return true end
  if done then finish(key,true,value,stamp);return true end
  return false
end

-- Run queued producers until `budget` seconds of this frame are spent.
function J.step(budget)
  local t0=clock()
  while #queue>0 and clock()-t0<budget do advance(queue[1]) end
end

-- Finish `key` now if it takes no more than `budget` seconds; true when its result is in.
function J.finish(key,budget)
  local t0=clock()
  while running[key] do
    if advance(key) then break end
    if clock()-t0>=budget then return false end
  end
  return results[key]~=nil
end

-- Seconds since `key`'s result was last confirmed (math.huge without one).
function J.age(key) local r=results[key];return r and clock()-r.at or math.huge end

function J.reset() results,running,queue={}, {}, {} end
return J
