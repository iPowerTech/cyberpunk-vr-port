-- Claim input only for a visible numeric keypad owned by the current DeviceZoom
-- entity. Looking past the device does not change that identity.
local M = { active=false, error=nil, owner=nil }
local elapsed = 0
local keypadType

local function visible(widget)
    for _=1,24 do
        if not widget then return true end
        if not widget:IsVisible() or widget:GetOpacity() <= 0 then return false end
        widget = widget:GetParentWidget()
    end
    return false
end

function M.resolve(overlayOpen)
    if overlayOpen then return nil end
    local player = Game.GetPlayer()
    if not player then return nil end
    local definitions = GetAllBlackboardDefs()
    local boards = Game.GetBlackboardSystem()
    local menu = boards:Get(definitions.UI_System)
    if menu and menu:GetBool(definitions.UI_System.IsInMenu) then return nil end
    local psm = boards:GetLocalInstanced(player:GetEntityID(), definitions.PlayerStateMachine)
    if not psm or not psm:GetBool(definitions.PlayerStateMachine.IsUIZoomDevice) then return nil end
    local owner = Game.FindEntityByID(psm:GetEntityID(definitions.PlayerStateMachine.UIZoomDeviceID))
    if not owner or not owner.cameraZoomActive then return nil end
    local controller = owner:GetGameController()
    if not controller or controller:IsInteractivityBlocked() then return nil end
    keypadType = keypadType or CName.new("KeypadDeviceController")
    for _, item in ipairs(controller.deviceWidgetsData or {}) do
        local root = item.widget
        local keypad = root and root:GetController()
        if keypad and keypad:IsA(keypadType) and visible(root) then
            return root, owner:GetEntityID()
        end
    end
    return nil
end

function M.tick(dt, overlayOpen)
    local step = tonumber(dt) or 0
    elapsed = elapsed + (step > 0 and step or .05)
    if elapsed < .05 then return end
    elapsed = 0
    local ok, root, owner = pcall(M.resolve, overlayOpen)
    if ok then M.error=nil else M.error=tostring(root) end
    if not ok then root, owner = nil, nil end
    M.owner = owner
    if type(VRKeypadUpdate) ~= "function" then M.active=false; return end
    local published, active = pcall(VRKeypadUpdate, root)
    M.active = published and tonumber(active) == 1
    if not published then M.error=tostring(active) end
end

function M.shutdown()
    M.active=false; M.owner=nil; elapsed=0
    if type(VRKeypadUpdate)=="function" then pcall(VRKeypadUpdate,nil) end
end
return M
