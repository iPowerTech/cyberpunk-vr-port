-- WHICH WEAPON CONFIGS TO LOAD. A list, because CET's sandbox has no directory enumeration -- but a list in a
-- DATA file, not in the module: adding a pistol should never mean editing reload.lua, the same reason the rig
-- signatures moved out to reload/rigs.lua.
--
-- Each name is a file beside this one: reload/weapons/<name>.lua.
return {
    'malorian_silverhand',
    'constitutional_unity',
    'arasaka_tamayura',
    'tsunami_nue',
    'constitutional_liberty',
    'arasaka_kenshin',
    'militech_lexington',
    'militech_omaha',
    'kangtao_chao',
    'tsunami_kappa',
    'arasaka_yukimura',
    'militech_ticon',
    'malorian_overture',
    -- the shotguns, all driven by reload/shotgun.lua
    'rostovic_satara',
    'rostovic_igla',
    'rostovic_testera',
    'rostovic_palica',
    'budget_carnage',
    'constitutional_tactician',
    -- ...and the two with box magazines, which are reload.lua's like the pistols
    'militech_crusher',
    'kangtao_zhuo',
    'techtronika_pozhar',
    -- the last pistols (2026-10-20)
    'krausser_grit',
    'darra_nova',
    'darra_quasar',
    'techtronika_burya',
    'techtronika_metel',
    -- the rifles (2026-10-20)
    'arasaka_masamune',
    'nokota_copperhead',
    'darra_umbra',
    'nokota_sidewinder',
    'militech_ajax',
    'tsunami_kyubi',
    'rostovic_kolac',
    'nokota_osprey',
    'techtronika_grad',
    'midnight_sor22',
    'tsunami_nekomata',
    'tsunami_rasetsu',
    'militech_achilles',
    'tsunami_ashura',
    'militech_hercules',
    -- the SMGs and LMGs (2026-10-25)
    'arasaka_shingen',
    'krausser_warden',
    'militech_saratoga',
    'budget_guillotine',
    'darra_pulsar',
    'arasaka_senkoh',
    'kangtao_dian',
    'constitutional_defender',
    'midnight_ma70',
}
