-- Evaluate as a function with "snapshot", "show_alt" or "hide_alt" in the live
-- bridge. The last two intentionally change only the alternate minimap slot's
-- desired visibility; restore it with hide_alt after the bounded regression test.
return function(action)
    local layer = Game.GetInkSystem():GetLayer(CName.new("inkHUDLayer"))
    local window = assert(layer and layer:GetVirtualWindow(), "HUD window missing")
    local root = assert(window:GetWidgetByPathName("Root"), "HUD root missing")
    local main = assert(root:GetWidgetByPathName("TopRightMain"), "TopRightMain missing")
    local right = assert(main:GetWidgetByPathName("TopRight"), "TopRight missing")
    local maps = assert(right:GetWidgetByPathName("TopRightMapContainer"), "Map container missing")
    local row = assert(maps:GetWidgetByPathName("TopRightWantedMinimap"), "Minimap row missing")
    local map = assert(row:GetWidgetByPathName("minimap"), "Minimap missing")
    local alt = assert(row:GetWidgetByPathName("minimap_q305"), "Alternate minimap missing")
    local quest = assert(right:GetWidgetByPathName("quest_list"), "Quest list missing")
    if action == "show_alt" then
        assert(not alt:IsVisible() and not alt:GetAffectsLayoutWhenHidden(), "Alternate minimap already active")
        alt:SetVisible(true)
    elseif action == "hide_alt" then
        alt:SetVisible(false)
    else
        assert(action == "snapshot", "Unknown action")
    end
    local function rect(w)
        local parent = w:GetParentWidget()
        local size = parent:GetChildSize(w)
        local x, y = 0, 0
        local current = w
        for _ = 1, 16 do
            if current == window then break end
            local p = current:GetParentWidget()
            if not p then break end
            local pos = p:GetChildPosition(current)
            x, y = x + pos.X, y + pos.Y
            current = p
        end
        return {x=x, y=y, width=size.X, height=size.Y,
                visible=w:IsVisible(), affects=w:GetAffectsLayoutWhenHidden()}
    end
    return {main=rect(main), row=rect(row), map=rect(map), alt=rect(alt), quest=rect(quest),
            count=GetMod("CyberpunkVRPort_Stereo").HudPanel.count}
end
