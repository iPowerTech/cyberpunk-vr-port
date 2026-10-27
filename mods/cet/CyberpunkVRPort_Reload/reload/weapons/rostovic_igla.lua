-- Rostovic Igla — physical-reload configuration.
--
-- THE SATARA'S MECHANISM IN ANOTHER BODY. Its own frame rig has eight bones against the Satara's thirty-six (no
-- magnet rails), but `barrel_hinge` is bone 2 in both, `mag_slot` hangs off it 25.8 mm behind and 31 mm up just
-- the same, the receiver is the Satara's (`w_shotgun_dual__rostovic_satara__rcv1_01`) and so are the magazine rig
-- and the two shells -- its magazine .app files point at the Satara's meshes and nothing of their own.
--
-- Measured off its own takes (igla_empty_a / empty_b / one_a): the barrels hold at 43.1 .. 44.6 deg, the lever
-- reaches 41.1 .. 42.6 -- the Satara's numbers to the tenth. So this is the Satara's config with the Igla's
-- name and the Igla's own sounds, out of pwa_w_shotgun_dual__rostovic_igla.anims.
local function copy(t)
    if type(t) ~= 'table' then return t end
    local o = {}
    for k, v in pairs(t) do o[k] = copy(v) end
    return o
end
local cfg = copy(require('reload/weapons/rostovic_satara'))
cfg.id    = 'rostovic_igla'
cfg.match = 'rostovic_igla__base'
cfg.sounds = {
    lever   = 'w_gun_shotgun_power_igla_top_lever',
    open    = 'w_gun_shotgun_power_igla_open_eject',
    close   = 'w_gun_shotgun_power_igla_close',
    shellIn = 'w_gun_shotgun_power_igla_shell_insert',
    grab    = 'w_gun_shotgun_power_igla_mag_new_grab',
}
cfg.shotgun.sounds = nil
return cfg
