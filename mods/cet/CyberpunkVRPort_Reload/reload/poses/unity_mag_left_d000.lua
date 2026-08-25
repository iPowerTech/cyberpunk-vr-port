-- Magazine hold at 0 mm out of the well, LEFT hand. RECORDED from the game's own reload.
-- (reload_record_03.lua, frames t=2.31..2.35, depth 0..0 mm): parent-local quats.
-- One stage of the insertion. The animation does not swap between two grips, it rolls through
-- them: fingers around the magazine on the way in, palm on its base at the end.
-- Then TUNED in VR through the CET overlay and baked back in: the animation's hand re-grips the
-- magazine and a VR hand cannot, so a few joints are curled harder than the take has them
-- (thumb base 1.22, index base 1.20, three metacarpals 1.03-1.05). Angle-scaled, so this is
-- still the recorded direction of each joint, only further along it.
return {
  { 'LeftInHandThumb', -0.867065, 0.401552, 0.292376, 0.038340 },
  { 'LeftInHandIndex', 0.775373, -0.051173, -0.028385, 0.628786 },
  { 'LeftInHandMiddle', 0.688265, 0.014604, 0.046979, 0.723789 },
  { 'LeftInHandRing', 0.653119, 0.076763, 0.088869, 0.748094 },
  { 'LeftInHandPinky', 0.565124, 0.125869, 0.191001, 0.792660 },
  { 'LeftHandThumb1', -0.046097, -0.059526, -0.242790, 0.967153 },
  { 'LeftHandThumb2', 0.000023, 0.000023, -0.018671, 0.999826 },
  { 'LeftHandIndex1', 0.001522, 0.049275, -0.262160, 0.963764 },
  { 'LeftHandIndex2', 0.000023, 0.000016, -0.384280, 0.923216 },
  { 'LeftHandIndex3', 0.000020, 0.000019, -0.216267, 0.976334 },
  { 'LeftHandMiddle1', 0.040865, -0.007823, -0.246799, 0.968173 },
  { 'LeftHandMiddle2', 0.000021, 0.000020, -0.504127, 0.863629 },
  { 'LeftHandMiddle3', -0.075105, 0.008416, -0.142580, 0.986894 },
  { 'LeftHandRing1', 0.026934, -0.052309, -0.150282, 0.986891 },
  { 'LeftHandRing2', 0.000020, 0.000022, -0.511840, 0.859081 },
  { 'LeftHandRing3', -0.002844, 0.005244, -0.252589, 0.967555 },
  { 'LeftHandPinky1', 0.052292, -0.085141, -0.234465, 0.966976 },
  { 'LeftHandPinky2', 0.000021, 0.000023, -0.450596, 0.892728 },
  { 'LeftHandPinky3', 0.000022, 0.000016, -0.241969, 0.970284 },
}
