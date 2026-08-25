-- The PINCH on this pistol's slide, from the game's own `first_equip` -- the only animation in which a
-- hand touches the Lexington's slide at all. Its reloads never do: measured across all four takes, the
-- left wrist never comes within 107 mm of the slide slot and the slide is simply released off its catch.
-- 
-- Sampled at t=1.967 s, where the weapon rig's own first_equip has barrel_back at its deepest (43.3 mm),
-- and converted GLB -> runtime by (x, -z, y, w) -- the single global export convention, confirmed live on
-- the Silverhand's fingers. FINGERS ONLY: a WRIST cannot be taken from these files, because the arm rig's
-- WeaponRight frame relates to the runtime hand by an unknown mount rotation (36 cm of residual when it
-- was fitted). The wrist offset in the config is transferred from the two pistols where it WAS measured.
-- Average joint angle 82 deg.
return {
  { 'LeftInHandThumb', -0.859786, 0.507191, 0.058721, -0.008802 },
  { 'LeftInHandIndex', -0.715060, 0.004843, 0.011499, -0.698952 },
  { 'LeftInHandMiddle', 0.689754, 0.036005, 0.028503, 0.722586 },
  { 'LeftInHandRing', 0.647665, 0.106221, 0.031547, 0.753825 },
  { 'LeftInHandPinky', 0.554447, 0.184801, 0.099148, 0.805361 },
  { 'LeftHandThumb1', -0.025725, 0.005465, -0.275508, 0.960939 },
  { 'LeftHandThumb2', 0.000000, -0.000000, -0.414412, 0.910090 },
  { 'LeftHandIndex1', -0.104747, 0.014972, -0.554764, 0.825252 },
  { 'LeftHandIndex2', 0.000000, -0.000000, -0.680459, 0.732786 },
  { 'LeftHandIndex3', 0.000000, -0.000000, -0.557156, 0.830408 },
  { 'LeftHandMiddle1', -0.087198, 0.017605, -0.654620, 0.750706 },
  { 'LeftHandMiddle2', 0.000000, -0.000000, -0.804345, 0.594162 },
  { 'LeftHandMiddle3', -0.036917, 0.037950, -0.391962, 0.918457 },
  { 'LeftHandRing1', -0.017101, 0.013274, -0.691076, 0.722458 },
  { 'LeftHandRing2', 0.000000, -0.000000, -0.740099, 0.672498 },
  { 'LeftHandRing3', 0.000000, -0.000000, -0.731343, 0.682009 },
  { 'LeftHandPinky1', -0.002919, 0.032680, -0.684782, 0.728009 },
  { 'LeftHandPinky2', 0.000000, -0.000000, -0.663812, 0.747899 },
  { 'LeftHandPinky3', -0.002361, 0.018660, -0.558026, 0.829610 },
}
