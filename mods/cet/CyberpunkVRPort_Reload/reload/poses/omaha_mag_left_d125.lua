-- Magazine 115 mm out of the well, LEFT hand, from the Omaha's own reload (t=2.04) -- carried in.
-- Depth is measured along the magazine's OWN axis, which on this weapon is straight down in the
-- weapon's frame -- see the config for why its animated path could not give the axis.
-- The user's own correction is baked in, printed from the game: thumb metacarpal x1.03, ring x1.07,
-- little x1.06, thumb base x1.99 and tip x1.37, middle base x0.80, ring base x0.73 and middle x0.92.
-- Scaled by ANGLE, the way the overlay does it. Only the CARRY pose: the insertion poses below it are the
-- game's own hand around the game's own magazine and were not what was being judged.
return {
  { 'LeftInHandThumb', -0.814324, 0.338521, 0.466947, 0.065126 },
  { 'LeftInHandIndex', -0.753102, 0.056661, 0.035535, -0.654495 },
  { 'LeftInHandMiddle', 0.689355, -0.007181, 0.026237, 0.723913 },
  { 'LeftInHandRing', 0.651311, 0.085574, 0.065698, 0.751103 },
  { 'LeftInHandPinky', 0.516580, 0.207054, 0.086855, 0.826275 },
  { 'LeftHandThumb1', 0.022593, 0.001651, -0.484587, 0.874450 },
  { 'LeftHandThumb2', -0.000005, 0.000000, -0.027093, 0.999633 },
  { 'LeftHandIndex1', -0.010371, 0.104874, -0.487382, 0.866806 },
  { 'LeftHandIndex2', 0.000001, 0.000004, -0.163815, 0.986491 },
  { 'LeftHandIndex3', -0.000005, -0.000006, -0.028180, 0.999603 },
  { 'LeftHandMiddle1', -0.124620, 0.274017, -0.373291, 0.877518 },
  { 'LeftHandMiddle2', 0.000003, -0.000001, -0.366774, 0.930310 },
  { 'LeftHandMiddle3', -0.027330, -0.001608, 0.058762, 0.997897 },
  { 'LeftHandRing1', 0.034143, 0.070835, -0.473364, 0.877350 },
  { 'LeftHandRing2', -0.000001, -0.000001, -0.749023, 0.662544 },
  { 'LeftHandRing3', 0.000002, -0.000004, -0.053602, 0.998562 },
  { 'LeftHandPinky1', 0.090123, 0.180398, -0.793935, 0.573586 },
  { 'LeftHandPinky2', -0.000004, -0.000002, -0.680094, 0.733125 },
  { 'LeftHandPinky3', -0.000003, 0.000004, 0.111263, 0.993791 },
}
