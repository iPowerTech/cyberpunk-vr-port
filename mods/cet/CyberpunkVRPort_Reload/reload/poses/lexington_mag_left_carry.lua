-- How this pistol's hand carries a magazine in the air, take EMPTYLEX2 (t=1.71, magazine 117 mm out of the well).
-- THREE of the four takes hold it this way and agree to 1.6-7.8 deg between them; the fourth is a different hold
-- entirely (32.8-34.1 deg, up to 72 deg at the middle fingertip) and is shipped beside this one as `_carry_b`.
-- The module picks between the two at random once per grab -- the game itself has three reload variants.
-- The user's own correction to this hold is baked in: index base x1.01, index middle x1.53 (printed from the game
return {
  { 'LeftInHandThumb', -0.908465, 0.228802, 0.109597, -0.332157 },
  { 'LeftInHandIndex', -0.630309, 0.034915, 0.012516, -0.775458 },
  { 'LeftInHandMiddle', 0.624125, 0.062328, 0.005413, 0.778815 },
  { 'LeftInHandRing', 0.528753, 0.126909, -0.052548, 0.837588 },
  { 'LeftInHandPinky', 0.502192, 0.208204, -0.047719, 0.837960 },
  { 'LeftHandThumb1', 0.023073, -0.010119, -0.178001, 0.983708 },
  { 'LeftHandThumb2', -0.000006, -0.000004, -0.049647, 0.998767 },
  { 'LeftHandIndex1', -0.241102, 0.236259, -0.138973, 0.930988 },
  { 'LeftHandIndex2', -0.000009, -0.000002, -0.304165, 0.952619 },
  { 'LeftHandIndex3', 0.003405, -0.002205, -0.446178, 0.894935 },
  { 'LeftHandMiddle1', -0.199828, 0.116987, -0.211737, 0.949500 },
  { 'LeftHandMiddle2', 0.000000, 0.000001, -0.621142, 0.783698 },
  { 'LeftHandMiddle3', 0.054715, 0.012611, -0.303524, 0.951168 },
  { 'LeftHandRing1', -0.128594, 0.138476, -0.510135, 0.839077 },
  { 'LeftHandRing2', -0.000006, 0.000006, -0.506465, 0.862260 },
  { 'LeftHandRing3', 0.046636, 0.001827, -0.501174, 0.864087 },
  { 'LeftHandPinky1', -0.141325, 0.065222, -0.633801, 0.757674 },
  { 'LeftHandPinky2', -0.000001, -0.000004, -0.260464, 0.965484 },
  { 'LeftHandPinky3', -0.000001, 0.000003, -0.437213, 0.899358 },
}
