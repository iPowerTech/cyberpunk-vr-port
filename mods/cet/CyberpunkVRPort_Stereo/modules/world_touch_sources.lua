-- A door's visible terminal can belong to a separate entity. Discover actual
-- loaded world-screen owners instead of relying on the gaze interaction target.
local S={}
function S.resolve(system,view)
    local result,seen={},{}
    if not system or not view then return result end
    local world=system:GetWorldWidgets()
    -- Native weak-handle arrays contain expired/nil slots. ipairs would stop at
    -- the first expired screen, hiding every live terminal after it.
    for i=1,#world do
        local controller=world[i]
        if controller and controller:IsA('DeviceInkGameControllerBase') then
            local owner=controller:GetOwnerEntity()
            if owner and owner:IsA('Device') then
                local key=tostring(owner:GetEntityID().hash)
                if not seen[key] then
                    seen[key]=true
                    for _,component in ipairs(owner:GetComponents()) do
                        if component:IsA('WorldWidgetComponent') and component:IsEnabled() and component.sceneWidgetProperties.isInteractable then
                            local centre=component:GetLocalToWorld().W
                            local dx,dy,dz=centre.x-view.x,centre.y-view.y,centre.z-view.z
                            if dx*dx+dy*dy+dz*dz<2.25 then
                                result[#result+1]={component=component,owner=key,key=key..'/'..NameToString(component:GetName()),
                                    dropPoint=owner:IsA('DropPoint')}
                            end
                        end
                    end
                end
            end
        end
    end
    return result
end
return S
