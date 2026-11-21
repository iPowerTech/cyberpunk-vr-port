-- Fork: SCREENS DRAWN ON A MESH WHOSE COMPONENT SITS OFF THE GLASS. A fast travel terminal (DataTerm, dataTerm_ui) draws
-- its screen on its mesh "Component0", while the widget component's own frame lies at the pillar's foot, 1.1-1.2 m under
-- the glass, and the glass faces the component's -X, not the -Y the touch gives every screen: the touch never listed it
-- (its centre beyond 1.5 m of the eyes) and the picker's ray, run along -Y, never met it (measured 2026-11-22).
-- For a screen with a mesh binding, the game's own mesh picker (VRWorldTouchPointer) is asked along each of the
-- component's six axes from each fingertip near it; the axis whose hit lies closest to a finger, within 15 cm, three looks
-- running is the glass's, its outward side the eyes' side, and those hits its centre. Kept per screen in the component's
-- own frame (a moving screen keeps it). A screen the picker meets along its own face first keeps that face, untouched.
local F={}
F.REACH_SQ=2.5*2.5             -- eyes to a mesh-bound component's origin: listed and probed within this
local NEAR,RUNS,EVERY=.15,3,.1  -- a hit within 15 cm of the finger along its axis; three looks running; ten a second
local AXES={'X','Y','Z'}
local faces,pend={},{}          -- key -> {own=true} or {axis,sign,centre}; key -> the looks so far
local function dot(a,b) return a.x*b.x+a.y*b.y+a.z*b.z end
-- The mesh a screen is drawn on (its mesh target binding's name), or nil.
function F.binding(component)
    local ok,name=pcall(function() local b=component.meshTargetBinding;return b and NameToString(b.bindName) end)
    if ok and name and name~='' and name~='None' then return name end
end
-- Not yet known: neither its own face nor a found one.
function F.pending(key) return faces[key]==nil end
-- A found face in the world, now: {centre, normal}; nil while pending or for a screen keeping its own face.
function F.get(key,matrix)
    local f=faces[key]
    if not f or f.own then return nil end
    local W,X,Y,Z,l=matrix.W,matrix.X,matrix.Y,matrix.Z,f.centre
    local a=matrix[f.axis]
    local n=math.sqrt(dot(a,a))
    if n<.001 then return nil end
    return {centre=Vector4.new(W.x+X.x*l[1]+Y.x*l[2]+Z.x*l[3],W.y+X.y*l[1]+Y.y*l[2]+Z.y*l[3],W.z+X.z*l[1]+Y.z*l[2]+Z.z*l[3],1),
        normal=Vector4.new(a.x*f.sign/n,a.y*f.sign/n,a.z*f.sign/n,0)}
end
local function met(s,h) local pt=s and s[h*2];return pt and (pt.x~=0 or pt.y~=0 or pt.z~=0) and pt or nil end
-- One look for a pending screen (SetVRDiagCapture(1) already set): own = the outward normal the touch gives it.
function F.probe(key,bind,c,matrix,view,own,dt,note)
    local p=pend[key]
    if not p then p={wait=0,n=0};pend[key]=p end
    p.wait=p.wait-dt
    if p.wait>0 then return end
    p.wait=EVERY
    local W=matrix.W
    local s=VRWorldTouchPointer(c,W,own)
    if met(s,1) or met(s,2) then
        faces[key]={own=true};pend[key]=nil
        note('face/'..key,string.format('screen %s, drawn on mesh %s: the picker meets it along its own face',key,bind))
        return
    end
    local X,Z=matrix.X,matrix.Z
    local nz=Vector4.new(X.y*Z.z-X.z*Z.y,X.z*Z.x-X.x*Z.z,X.x*Z.y-X.y*Z.x,0)
    local tips=VRMenuPointer(W,X,Z,Vector4.new(1,1,0,0))
    local fingers={}
    for h=1,2 do
        local t=tips[h*4-3]
        if t and t.w>.5 then
            local a,b,d=t.x-.5,.5-t.y,t.z
            fingers[h]={x=W.x+X.x*a+Z.x*b+nz.x*d,y=W.y+X.y*a+Z.y*b+nz.y*d,z=W.z+X.z*a+Z.z*b+nz.z*d}
        end
    end
    if not fingers[1] and not fingers[2] then return end
    local best
    for _,axis in ipairs(AXES) do
        local A=matrix[axis]
        for sign=-1,1,2 do
            local r=VRWorldTouchPointer(c,W,Vector4.new(A.x*sign,A.y*sign,A.z*sign,0))
            for h=1,2 do
                local f,pt=fingers[h],met(r,h)
                if f and pt then
                    local d=math.abs((f.x-pt.x)*A.x+(f.y-pt.y)*A.y+(f.z-pt.z)*A.z)
                    if d<=NEAR and (not best or d<best.d) then best={axis=axis,d=d,pt=pt} end
                end
            end
        end
    end
    if not best then return end
    if best.axis~=p.axis then p.axis=best.axis;p.n=0;p.sum={0,0,0} end
    local q={x=best.pt.x-W.x,y=best.pt.y-W.y,z=best.pt.z-W.z}
    p.n=p.n+1;p.sum[1]=p.sum[1]+dot(q,X);p.sum[2]=p.sum[2]+dot(q,matrix.Y);p.sum[3]=p.sum[3]+dot(q,Z)
    if p.n<RUNS then return end
    local A=matrix[best.axis]
    local e=(view.x-best.pt.x)*A.x+(view.y-best.pt.y)*A.y+(view.z-best.pt.z)*A.z
    if math.abs(e)<.05 then return end -- the eyes in the glass's plane: no side yet
    local f={axis=best.axis,sign=e>0 and 1 or -1,centre={p.sum[1]/p.n,p.sum[2]/p.n,p.sum[3]/p.n}}
    faces[key]=f;pend[key]=nil
    note('face/'..key,string.format('screen %s, drawn on mesh %s: its glass faces the component\'s %s%s, centre (%+.0f %+.0f %+.0f) '
        ..'cm in its frame, from %d looks',key,bind,f.sign>0 and '+' or '-',f.axis,f.centre[1]*100,f.centre[2]*100,
        f.centre[3]*100,p.n))
end
function F.reset() faces,pend={},{} end
return F
