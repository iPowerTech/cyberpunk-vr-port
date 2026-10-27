-- Replace WEAPON_MODULE with the full production Weapon/init.lua source.
-- This isolated environment has no fallback to CET/game globals. It runs the
-- real onUpdate/recoil-classification path and captures only its publication.
local source = [==[-- WEAPON_MODULE]==]
local callbacks, current, published, entity = {}, nil, nil, 0
local noop = function() end
local weapon = {
    GetItemID = function() return current end,
    GetEntityID = function() return {hash = entity} end,
    GetMuzzleSlotWorldTransform = function() return nil end,
}
local player = {
    GetActiveWeapon = function() return weapon end,
    GetEntityID = function() return {hash = 1} end,
}
local env = {
    registerForEvent = function(name, fn) callbacks[name] = fn end,
    spdlog = {info=noop}, io = {open=function() return nil end}, os={clock=function() return 0 end},
    Game = {
        GetPlayer = function() return player end,
        GetTransactionSystem = function() return {GetItemData=function() return nil end} end,
        GetStatsSystem = function() return {AddModifier=noop, RemoveModifier=noop} end,
    },
    TweakDB = {GetFlat=function(_, key)
        if key:sub(-9) == '.itemType' then return current.itemType end
        return nil
    end},
    TDBID = {ToStringDEBUG=function(v) return v end},
    ItemID = {GetTDBID=function(item) return item.key end},
    WeaponObject = {IsMelee=function(item)
        if item.predicateError then error('unavailable') end
        return item.melee
    end},
    SetVRWeaponClass = function(value) published=value end,
    SetVRWeaponKick=noop, SetVRWeaponName=noop,
    gamedataStatType={}, gameStatModifierType={Multiplier=1},
}
-- CET intentionally omits setfenv. Bind every game/native dependency lexically;
-- unused APIs are nil so an accidental path fails inside production pcall.
local bindings = [[return function(env)
local registerForEvent, spdlog, io, os = env.registerForEvent, env.spdlog, env.io, env.os
local Game, TweakDB, TDBID, ItemID = env.Game, env.TweakDB, env.TDBID, env.ItemID
local WeaponObject, SetVRWeaponClass = env.WeaponObject, env.SetVRWeaponClass
local SetVRWeaponKick, SetVRWeaponName = env.SetVRWeaponKick, env.SetVRWeaponName
local gamedataStatType, gameStatModifierType = env.gamedataStatType, env.gameStatModifierType
local VRP_lastClass, VRP_lastItemType
local CName, GameObject, GetSingleton, RPGManager, TweakDBID, Vector3, Vector4
local GetVRCarryNear, GetVRMeleeTrigger, GetVRSharedSlot
local InstallVRProvInstrument, InstallWeaponAimHook, SetVRAimHit, SetVRCarryLeft
local SetVRMuzzlePos, SetVRMuzzleQuat, SetVRZoomLevel
]]
local module=assert(loadstring(bindings..source..'\nend', '@Weapon/init.lua'))()
module(env)
assert(callbacks.onUpdate, 'production onUpdate missing')
local cases = {
    {'Gorilla Arms', 'Cyb_StrongArms', true, 5, 'Items.AdvancedStrongArmsLegendary'},
    {'Mantis Blades', 'Cyb_MantisBlades', true, 5},
    {'Monowire', 'Cyb_NanoWires', true, 5},
    {'Fists', 'Wea_Fists', true, 5},
    {'Katana', 'Wea_Katana', true, 5},
    {'Knife', 'Wea_Knife', true, 5},
    {'Club', 'Wea_OneHandedClub', true, 5},
    {'Hammer', 'Wea_Hammer', true, 5},
    {'Modded melee', 'Unknown', true, 5},
    {'Handgun', 'Wea_Handgun', false, 1},
    {'Revolver', 'Wea_Revolver', false, 1},
    {'Rifle', 'Wea_AssaultRifle', false, 2},
    {'SMG', 'Wea_SubmachineGun', false, 2},
    {'Shotgun', 'Wea_Shotgun', false, 3},
    {'Dual shotgun', 'Wea_ShotgunDual', false, 3},
    {'Sniper', 'Wea_SniperRifle', false, 4},
    {'Precision rifle', 'Wea_PrecisionRifle', false, 4},
    {'Projectile launcher', 'Cyb_Launcher', false, 0},
    {'Unknown ranged', 'Unknown', false, 0},
    {'Melee without record name', nil, true, 5},
    {'Katana predicate unavailable', 'Wea_Katana', nil, 5, nil, true},
    {'Handgun after melee', 'Wea_Handgun', false, 1},
}
for _, case in ipairs(cases) do
    entity=entity+1;published=nil
    current={key=case[2] and (case[5] or ('Items.TestWeapon'..entity)),
        itemType=case[2] and ('ItemType.'..case[2]), melee=case[3], predicateError=case[6]}
    callbacks.onUpdate(0)
    assert(published==case[4], case[1]..': expected class '..case[4]..', got '..tostring(published))
end
print('PASS: '..#cases..' production weapon classification cases; no live state modified')
