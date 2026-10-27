-- Holding the thumbstick button grabs a list. Release never becomes an item click.
local G={}
function G.held(p,value) return value>(p.stickDown and .4 or .7) end
function G.cancel(p)
  if p.stickScroll then
    local view=p.stickScroll.view
    view:endDrag();view.vel=0
    p.stickScroll=nil
  end
end
function G.update(p,value,tracked,view,mode,rayY,tipY,now)
  if not tracked then G.cancel(p);return end
  p.stickDown=G.held(p,value)
  if not p.stickDown then
    if p.stickScroll then p.stickScroll.view:endDrag();p.stickScroll=nil end
    p.stickUsed=false
    return
  end
  if not p.stickUsed and view and view:scrollable() and not view.dragging then
    local y=mode=="tip" and tipY or rayY
    if y then
      view:beginDrag(y)
      p.stickScroll={view=view,mode=mode};p.stickUsed=true
    end
  end
  if p.stickUsed then p.stickSwipeUntil=now+.6 end
  local grab=p.stickScroll
  if grab then
    local y=rayY;if grab.mode=="tip" then y=tipY end
    if y then grab.view:dragTo(y,now) else G.cancel(p) end
  end
end
function G.blocksSwipe(p,now)
  return p.stickScroll~=nil or (p.stickDown and p.stickUsed) or now<(p.stickSwipeUntil or 0)
end
return G
