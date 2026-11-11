-- A door's visible terminal can belong to a separate entity. Discover actual
-- loaded world-screen owners instead of relying on the gaze interaction target.
local S={}
-- An owner farther than this from the eyes cannot hold a screen within touch reach
-- (screens sit at most ~4 m from their owner's origin); its components are not listed.
-- A lift is the exception: its origin is not where its car is. Measured 2026-11-11 inside a lift: LiftDevice origin
-- 93-94 m from the eyes while its panel's centre was within 1.5 m, so the panel was never listed and could not be
-- touched. A lift's screens are always listed and judged, like every screen, by their own centre.
local OWNER_REACH_SQ=15*15
-- note(key,text), optional: the diagnostic report (world_touch.lua). It is told why a screen near the eyes is
-- not listed, which is the part of the chain that otherwise leaves no trace.
function S.resolve(view,note)
    local result,seen={},{}
    if not view then return result end
    local world=Game.VRInkWorldControllers()
    -- A numeric loop, not ipairs: should a slot ever come back nil, ipairs would
    -- stop there and hide every live terminal after it.
    for i=1,#world do
        local controller=world[i]
        if controller and controller:IsA('DeviceInkGameControllerBase') then
            local owner=controller:GetOwnerEntity()
            if owner and owner:IsA('Device') then
                local key=tostring(owner:GetEntityID().hash)
                local o=not seen[key] and owner:GetWorldPosition()
                seen[key]=true
                if o and ((o.x-view.x)^2+(o.y-view.y)^2+(o.z-view.z)^2<OWNER_REACH_SQ or owner:IsA('LiftDevice')) then
                    for _,component in ipairs(Game.VREntityComponents(owner)) do
                        if component:IsA('WorldWidgetComponent') then
                            local listed=component:IsEnabled() and component.sceneWidgetProperties.isInteractable
                            if listed or note then
                                local centre=component:GetLocalToWorld().W
                                local dx,dy,dz=centre.x-view.x,centre.y-view.y,centre.z-view.z
                                local d2=dx*dx+dy*dy+dz*dz
                                if listed and d2<2.25 then
                                    result[#result+1]={component=component,owner=key,key=key..'/'..NameToString(component:GetName()),
                                        dropPoint=owner:IsA('DropPoint'),cls=note and NameToString(owner:GetClassName()) or nil}
                                elseif note and d2<9 then
                                    note(key..'/'..NameToString(component:GetName()),string.format('not listed: %s %s enabled=%s interactable=%s centre %s',
                                        NameToString(owner:GetClassName()),NameToString(component:GetName()),tostring(component:IsEnabled()),
                                        tostring(component.sceneWidgetProperties.isInteractable),d2<2.25 and 'within 1.5 m' or 'beyond 1.5 m'))
                                end
                            end
                        end
                    end
                end
            elseif owner and note then
                local o=owner:GetWorldPosition()
                if o and (o.x-view.x)^2+(o.y-view.y)^2+(o.z-view.z)^2<9 then
                    note('owner/'..tostring(owner:GetEntityID().hash),string.format('not listed: %s owns a %s and is no Device',
                        NameToString(owner:GetClassName()),NameToString(controller:GetClassName())))
                end
            end
        end
    end
    return result
end
return S
