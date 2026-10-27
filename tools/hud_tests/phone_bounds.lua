-- Inject the production module at HUD_MODULE. Everything it sees is lexical and
-- mocked; this can run in CET without changing the game's globals or UI objects.
local testPrint = print
local function run()
    local second, nativeCalls, nativeLoot = 0, 0, false
    local os = { time = function() return second end }
    local CName = { new = function(value) return value end }
    local NameToString = function(value) return value end
    local Vector2 = { new = function() return { X = 0, Y = 0 } end }
    local inkHudEntryInfo = { new = function() return { size = Vector2.new(), offset = Vector2.new() } end }
    local print = function() end
    local function window()
        return {
            size = { X = 3840, Y = 2160 }, writes = 0,
            IsA = function(_, name) return name == "inkVirtualWindow" end,
            GetSize = function(self) return self.size end,
            SetSize = function(self, x, y) self.size = { X = x, Y = y }; self.writes = self.writes + 1 end,
            FlagForVisualInvalidation = function(self) self.refreshed = (self.refreshed or 0) + 1 end,
        }
    end
    local function root(name, x, y)
        return {
            name = name, size = { X = x, Y = y }, window = window(), dirty = 0,
            GetName = function(self) return self.name end,
            GetSize = function(self) if self.expired then error("expired root") end; return self.size end,
            GetParentWidget = function(self) return self.window end,
            IsVisible = function(self) return self.visible~=false end,
            GetOpacity = function(self) return self.opacity or 1 end,
            GetUserData = function(self) return self.info end,
            SetUserData = function(self, value) self.info = value end,
            FlagForVisualInvalidation = function(self) self.dirty = self.dirty + 1 end,
        }
    end
    local function controller(class, widget)
        return { GetClassName = function() return class end, GetRootWidget = function() return widget end }
    end
    local phone, holo, audio = root("Root", 3840, 3840), root("Root", 3840, 2160), root("songbird_audiocall", 560, 200)
    local interactions, subtitles = root("Root",3840,2160),root("Root",1200,100)
    local basilisk=root("Root",3840,3840)
    local briefing=root("Root",3840,2160)
    local dossier,imprint,otherCustom=root("Root",920,1000),root("Root",700,150),root("Root",3840,2160)
    local function pathTree(widget,names)
        local current=widget
        for _,name in ipairs(names) do
            local child=root(name,1,1)
            current.GetWidgetByPathName=function(_,key) if key==name then return child end;return widget end
            current=child
        end
    end
    pathTree(dossier,{"SC_01","CARD_00_info"})
    pathTree(imprint,{"Main","Pane1","right","right_panel","text_up","imprint_text"})
    otherCustom.GetWidgetByPathName=function()return otherCustom end
    local top, loot = root("topWidgets",1024,275),root("looting",1024,275)
    top.window=interactions;loot.window=top
    interactions.GetWidgetByPathName=function(_,path) assert(path=="topWidgets");return top end
    top.GetWidgetByPathName=function(_,path) assert(path=="looting");return loot end
    subtitles.info={size={X=1900,Y=300},offset={X=-350,Y=0}}
    local subtitlePanel={}
    local subtitlePosition={X=0,Y=-255}
    local subtitleSize={X=1200,Y=85}
    subtitles.GetWidgetByPathName=function()return subtitlePanel end
    subtitles.GetChildPosition=function(_,p)assert(p==subtitlePanel);return subtitlePosition end
    subtitles.GetChildSize=function(_,p)assert(p==subtitlePanel);return subtitleSize end
    audio.info = { size = { X = 123, Y = 45 }, offset = { X = 8, Y = 9 } }
    local controllers = {
        controller("NewHudPhoneGameController", phone),
        controller("HudPhoneGameController", holo),
        controller("HudPhoneGameController", audio),
        controller("gameuiInteractionsHubGameController",interactions),
        controller("SubtitlesGameController",subtitles),
        controller("gameuiPanzerHUDGameController",basilisk),
        controller("gameuiBriefingGameController",briefing),
        controller("CustomAnimationsHudGameController",dossier),
        controller("CustomAnimationsHudGameController",imprint),
        controller("CustomAnimationsHudGameController",otherCustom),
    }
    local layer = { GetGameControllers = function() return controllers end, GetVirtualWindow = function() return {} end }
    local Game = {
        GetInkSystem = function() return { GetLayer = function() return layer end } end,
        VRHudPanelUpdate = function(_,visible) nativeCalls = nativeCalls + 1;nativeLoot=visible;return 23 end,
    }
    -- Cold module load: none of CET's game types/functions exists yet.
    local readyCName, readyVector2, readyInfo = CName, Vector2, inkHudEntryInfo
    local readyGame, readyNameToString = Game, NameToString
    CName, Vector2, inkHudEntryInfo, Game, NameToString = nil, nil, nil, nil, nil
    local module = (function()
-- HUD_MODULE
    end)()
    assert(module and module.count == 0, "HUD module failed before CET onInit")
    CName, Vector2, inkHudEntryInfo = readyCName, readyVector2, readyInfo
    Game, NameToString = readyGame, readyNameToString
    module.tick()
    assert(phone.info.size.X == 3840 and phone.info.size.Y == 3840, "fullscreen phone bounds missing")
    assert(holo.info.size.X == 3840 and holo.info.size.Y == 2160, "holocall bounds missing")
    assert(basilisk.info.size.X==3840 and basilisk.info.size.Y==3840 and basilisk.window.size.Y==3840,
        "fullscreen Basilisk HUD capture bounds missing")
    assert(interactions.info.size.X==3840 and interactions.info.size.Y==2160 and interactions.window.size.X==3840,"interaction window bounds missing")
    assert(subtitles.info.size.X==1900 and subtitles.info.size.Y==300 and subtitles.info.offset.X==-350 and subtitles.info.offset.Y==-287,"negative subtitle position is clipped")
    assert(subtitles.window.writes==0,"subtitle crop must not change the native window layout")
    assert(phone.window.size.Y == 3840, "source window did not follow square viewport")
    assert(briefing.info.size.X==3840 and briefing.info.size.Y==2160 and briefing.info.offset.X==0,
        "story briefing omitted its two imprint panels")
    briefing.size={X=0,Y=0};module.tick()
    assert(briefing.info.size.X==3840 and briefing.window.size.X==3840,"empty briefing transition collapsed its window")
    briefing.size={X=3840,Y=2160}
    assert(dossier.info.size.X==984 and dossier.info.size.Y==1064 and dossier.info.offset.X==-32,
        "identity dossier crop clips its negative-offset input icon")
    assert(imprint.info.size.X==764 and imprint.info.size.Y==214 and imprint.info.offset.Y==-32,
        "identity imprint capture bounds missing")
    assert(dossier.window.size.X==920 and dossier.window.size.Y==1000 and imprint.window.size.Y==150,
        "capture padding changed quest widget layout")
    assert(otherCustom.info==nil,"unrelated custom animation was changed by a failed path lookup")
    assert(audio.info.size.X == 123 and audio.info.offset.X == 8, "authored audio bounds overwritten")
    local writes = phone.window.writes
    module.tick()
    assert(phone.window.writes == writes, "stable dimensions cause needless layout invalidation")
    assert(phone.window.refreshed == 3, "fullscreen phone image not refreshed each update")
    assert(nativeLoot and module.lootVisible,"visible loot did not select its cone")
    loot.visible=false;module.tick();assert(not nativeLoot,"hidden loot kept its cone")
    loot.visible=true;top.visible=false;module.tick();assert(not nativeLoot,"hidden ancestor did not clear loot")
    top.visible=true;interactions.opacity=0;module.tick();assert(not nativeLoot,"transparent root did not clear loot")
    interactions.opacity=1;loot=root("looting",1024,275);loot.window=top
    module.tick();assert(nativeLoot,"replaced loot widget retained a stale visibility reference")
    local savedLoot=loot;loot=nil;module.tick();assert(not nativeLoot,"removed loot widget kept its cone")
    loot=savedLoot;module.tick();assert(nativeLoot,"respawned loot did not restore its cone")
    local savedTop=top;top=interactions;module.tick();assert(not nativeLoot,"Codeware root fallback was mistaken for loot")
    top=savedTop
    subtitlePosition.Y=-500;subtitleSize.Y=380;module.tick()
    assert(subtitles.info.offset.Y==-532 and subtitles.info.size.Y==444,"larger subtitle/font did not expand the crop")
    subtitleSize.Y=0;module.tick()
    assert(subtitles.info.size.Y==444,"transient empty subtitle layout collapsed the capture")
    second=second+1;subtitlePosition.Y=-255;subtitleSize.Y=85;module.tick()
    assert(subtitles.info.offset.Y==-287 and subtitles.info.size.Y==300,"rescan accumulated crop growth instead of keeping authored bounds")
    phone.size = { X = 5120, Y = 2880 }
    module.tick()
    assert(phone.info.size.X == 5120 and phone.window.size.Y == 2880, "resolution change did not update bounds")
    phone.expired = true
    local before = nativeCalls
    module.tick()
    assert(nativeCalls == before + 1 and module.phoneError and not nativeLoot, "phone failure stopped HUD update or retained stale loot context")
    phone = root("Root", 3840, 3840)
    controllers = { controller("NewHudPhoneGameController", phone) }
    second = second + 1
    module.tick()
    assert(phone.info.size.Y == 3840 and not module.phoneError, "save/layer replacement retained old root")
    assert(not nativeLoot,"layer replacement retained loot from the previous scene")
    testPrint("PASS HUD bounds: cold load, phone/holocall, Basilisk, story briefings, identity cards/padding, unrelated custom roots, subtitles, refresh, resize, failure isolation, replacement, loot")
end
run()
