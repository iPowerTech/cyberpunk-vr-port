-- Run from the repository root with Lua (or Lupa): dofile("tools/vr_menu_tests/stick_scroll_test.lua").
local G=dofile("mods/cet/CyberpunkVRPort_Menu/modules/stick_scroll.lua")
local function view(scrollable)
  return {offset=300,vel=0,begins=0,ends=0,
    scrollable=function() return scrollable~=false end,
    beginDrag=function(v,y) v.begins=v.begins+1;v.dragging={y=y,offset=v.offset} end,
    dragTo=function(v,y) v.offset=v.dragging.offset+v.dragging.y-y;v.vel=100 end,
    endDrag=function(v) v.ends=v.ends+1;v.dragging=nil end}
end
local a,b=view(),view();local p={}
G.update(p,.6,true,a,"ray",600,400,0)
assert(a.begins==0,"partial squeeze must not grab")
G.update(p,.8,true,a,"ray",600,400,.1)
G.update(p,.5,true,b,"ray",500,400,.2)
assert(a.offset==400 and b.begins==0,"stick keeps its original list through hysteresis and crossing another list")
G.update(p,.8,true,nil,nil,450,400,.3)
assert(a.offset==450,"captured scroll continues outside the viewport")
G.update(p,.2,true,nil,nil,450,400,.4)
assert(not p.stickScroll and not a.dragging and a.vel==100,"release keeps inertia")
assert(G.blocksSwipe(p,.5) and not G.blocksSwipe(p,1),"release must swallow the trailing close swipe only")
G.update(p,.8,true,b,"tip",700,600,2)
G.update(p,.8,true,a,"ray",100,nil,2.1)
assert(not p.stickScroll and b.offset==300 and b.vel==0,"losing the fingertip must not jump to ray coordinates")
G.update(p,.8,true,a,"ray",100,600,2.2)
assert(a.begins==1,"tracking loss requires a release before a new grab")
G.update(p,0,true,a,"ray",100,600,2.3)
G.update(p,1,true,a,"ray",100,600,2.4)
G.update(p,1,false,a,"ray",500,600,2.5)
assert(not p.stickScroll and not a.dragging and a.vel==0,"tracking loss stops scrolling without inertia")
local owner,other={},{};local shared=view()
G.update(owner,1,true,shared,"ray",200,nil,3)
G.update(other,1,true,shared,"ray",400,nil,3.1)
assert(owner.stickScroll and not other.stickScroll and shared.begins==1,"two hands cannot drive one list")
G.cancel(owner)
assert(not shared.dragging and shared.vel==0,"rebuild/close releases the old view")
local short=view(false);local idle={}
G.update(idle,1,true,short,"ray",200,nil,4)
assert(not idle.stickScroll and not G.blocksSwipe(idle,4),"non-scrollable controls keep the ordinary close gesture")
print("PASS: stick hysteresis, capture, release, tracking loss, two-hand ownership and close-swipe suppression")
