-- Rostovic Testera — physical-reload configuration.
--
-- The Satara's mechanism again, on the shortest barrels of the three (`muzzle_slot` 271.8 mm out against the
-- Satara's 477.4): nine frame bones, `barrel_hinge` at 2 and `mag_slot` under it exactly where the Satara has
-- it, the Satara's receiver, magazine rig and shells. One part of its own -- `barrel_opener` (3), a cosmetic
-- plate on the side of the frame that the native clips do not move -- so nothing extra is driven.
--
-- Its takes (testera_empty_a / empty_b / one_a) hold the barrels at 43.1 .. 44.6 deg and put the lever at
-- 33.7 .. 42.6, the Satara's numbers; the sounds are its own (pwa_w_shotgun_dual__rostovic_testera.anims).
local function copy(t)
    if type(t) ~= 'table' then return t end
    local o = {}
    for k, v in pairs(t) do o[k] = copy(v) end
    return o
end
local cfg = copy(require('reload/weapons/rostovic_satara'))
cfg.id    = 'rostovic_testera'
cfg.match = 'rostovic_testera__base'
cfg.sounds = {
    lever   = 'w_gun_shotgun_power_testera_top_lever',
    open    = 'w_gun_shotgun_power_testera_open',
    close   = 'w_gun_shotgun_power_testera_close',
    shellIn = 'w_gun_shotgun_power_testera_shell_in',
    grab    = 'w_gun_shotgun_power_testera_mag_new_grab',
    caseOut = 'w_gun_shotgun_power_testera_eject',
}
cfg.shotgun.sounds = nil
return cfg
