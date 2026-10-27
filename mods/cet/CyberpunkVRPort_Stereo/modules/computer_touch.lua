-- Computer controls do not carry DeviceAction arrays. Route a physically hit
-- widget through the same methods as ComputerInkGameController's pointer callbacks.
local C={}
local function ref(value) return value and inkWidgetRef.Get(value) or nil end
local function entry(context,widget,kind,control,key)
    if not widget then return nil end
    return {widget=widget,computer=context,kind=kind,control=control,key=key or kind}
end
function C.context(controller,root)
    if not controller:IsA('ComputerInkGameController') then return nil end
    local owner=controller:GetOwnerEntity()
    local layout=controller:GetMainLayoutController()
    if not owner or not layout then return nil end
    return {controller=controller,owner=owner,layout=layout,root=root}
end
function C.sleepTarget(context)
    if not context or not context.owner:IsInSleepMode() then return nil end
    return entry(context,ref(context.layout.screenSaverSlot) or context.root,'computer_wake')
end
function C.collect(context,w,controller,path,buttons)
    if not context or not controller then return false end
    local b
    if controller:IsA('ComputerMenuButtonController') then
        b=entry(context,controller.targetWidget,'computer_menu',controller,path)
    elseif controller:IsA('ComputerDocumentThumbnailWidgetController') then
        b=entry(context,controller.targetWidget,'computer_document',controller,path)
    elseif controller:IsA('ComputerFullBannerWidgetController') then
        b=entry(context,ref(controller.closeButtonWidget),'computer_banner_close',nil,path..'/close')
    elseif controller:IsA('ComputerDocumentWidgetController') then
        local kind=controller:GetDocumentType()==EDocumentType.MAIL and 'computer_mail_close' or 'computer_file_close'
        b=entry(context,ref(controller.closeButtonWidget),kind,nil,path..'/close')
    elseif controller:IsA('ComputerBannerWidgetController') then
        b=entry(context,w,'computer_banner',controller,path)
    elseif controller:IsA('LinkController') then
        local browser=context.layout:GetInternetController()
        if browser then
            b=entry(context,w,'computer_link',controller,path);b.browser=browser
            local parent=w:GetParentWidget()
            for _=1,32 do
                if not parent then break end
                local c=parent:GetController()
                if c and c:IsA('WebPage') then b.webPage=c;break end
                if c and c:IsA('BrowserController') then break end
                parent=parent:GetParentWidget()
            end
        end
    end
    if b then buttons[#buttons+1]=b end
    return b and (b.kind=='computer_menu' or b.kind=='computer_document' or b.kind=='computer_banner' or b.kind=='computer_link') or false
end
function C.addWindowClose(context,buttons)
    if not context then return end
    local b=entry(context,context.layout:GetWindowCloseButton(),'computer_window_close')
    if b then buttons[#buttons+1]=b end
end
function C.usable(b)
    local context=b.computer
    if context.controller:IsInteractivityBlocked() or not context.owner:GetDevicePS():IsON() then return false end
    local sleeping=context.owner:IsInSleepMode()
    if b.kind=='computer_wake' then return sleeping end
    if b.kind=='computer_link' and b.control:GetLinkAddress()=='' then return false end
    return not sleeping and b.widget:IsInteractive()
end
function C.activate(b)
    if not C.usable(b) then return false end
    local c=b.computer
    if b.kind=='computer_wake' then
        -- The native event updates sleep state and routes to this owner's UI.
        -- GoToMenu still applies authorization / hacking restrictions normally.
        local evt=GoToMenuEvent.new()
        evt.menuType=EComputerMenuType.MAIN;evt.wakeUp=true
        c.owner:QueueEvent(evt)
    elseif b.kind=='computer_menu' then
        c.controller:GoToMenu(b.control:GetMenuID())
    elseif b.kind=='computer_document' then
        -- Retains native read flags and quest callbacks; never sets facts here.
        c.controller:OpenDocument(b.control)
    elseif b.kind=='computer_window_close' then c.layout:HideWindow()
    elseif b.kind=='computer_mail_close' then c.layout:HideMails()
    elseif b.kind=='computer_file_close' then c.layout:HideFiles()
    elseif b.kind=='computer_banner_close' then c.layout:HideFullBanner()
    elseif b.kind=='computer_banner' then c.layout:ShowFullBanner(c.controller,b.control:GetBannerData())
    elseif b.kind=='computer_link' then
        if b.webPage then
            -- Preserve WebPage's callback route, including listeners installed
            -- by that page, just as its OnLinkCallback does.
            b.webPage.lastClickedLinkAddress=b.control:GetLinkAddress()
            b.webPage:CallCustomCallback('OnLinkPressed')
        else b.browser:LoadWebPage(b.control:GetLinkAddress()) end
    else return false end
    return true
end
return C
