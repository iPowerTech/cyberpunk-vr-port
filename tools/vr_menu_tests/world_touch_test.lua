local C=dofile('mods/cet/CyberpunkVRPort_Stereo/modules/touch_contact.lua')
local function sample(d,valid) return {x=20,y=40,z=d,w=valid==false and 0 or 1} end
local a,b={name='floor A'},{name='floor B'}
local s={}
-- No accidental activation on load while already touching, or from behind.
assert(not C.step(s,sample(0),a,.016))
assert(not C.step(s,sample(-.02),a,.016))
assert(not C.step(s,sample(.003),a,.016))
assert(not C.step(s,sample(.04),a,.016))
assert(C.step(s,sample(.002),b,.016)) -- current fingertip target, not old gaze/hover
assert(s.target==b)
for _=1,100 do assert(not C.step(s,sample(0),b,.016)) end
assert(not C.step(s,sample(.012),a,.016)) -- jitter/partial retreat does not rearm
assert(not C.step(s,sample(0),a,.016))
assert(not C.step(s,sample(.035),a,.016))
assert(C.step(s,sample(.001),a,.016))
-- Blank glass consumes contact; sliding into a button must not click.
C.reset(s);C.step(s,sample(.04),nil,.016)
assert(not C.step(s,sample(.002),nil,.016))
assert(not C.step(s,sample(0),a,.016))
-- Tracking loss, deep penetration and stalled frames cancel armed approaches.
for _,bad in ipairs({{value=nil,dt=.016},{value=sample(0,false),dt=.016},
                     {value=sample(-.04),dt=.016},{value=sample(0),dt=.3}}) do
    C.reset(s);C.step(s,sample(.04),a,.016)
    assert(not C.step(s,bad.value,a,bad.dt))
    assert(not C.step(s,sample(.002),a,.016))
end
-- Each hand owns an independent approach; one held finger cannot repeat.
local left,right={},{}
C.step(left,sample(.04),a,.016);C.step(right,sample(.04),b,.016)
assert(C.step(left,sample(.002),a,.016))
assert(C.step(right,sample(.002),b,.016))
assert(not C.step(left,sample(.001),a,.016))
-- Drop-point approach: 10 cm triggers only after approaching from beyond 13 cm.
C.reset(s)
assert(not C.step(s,sample(.09),a,.016,C.dropPoint))
assert(not C.step(s,sample(.15),a,.016,C.dropPoint))
assert(not C.step(s,sample(.101),a,.016,C.dropPoint))
assert(C.step(s,sample(.10),a,.016,C.dropPoint))
for _=1,20 do assert(not C.step(s,sample(.09),a,.016,C.dropPoint)) end
assert(not C.step(s,sample(.12),a,.016,C.dropPoint))
assert(not C.step(s,sample(.095),a,.016,C.dropPoint))
assert(not C.step(s,sample(.14),a,.016,C.dropPoint))
assert(C.step(s,sample(.099),a,.016,C.dropPoint))
-- The same approach must not activate an ordinary door/elevator screen.
C.reset(s);C.step(s,sample(.15),a,.016)
assert(not C.step(s,sample(.099),a,.016))
print('world_touch: contact edges, target selection, repeat/rearm and tracking loss passed')
