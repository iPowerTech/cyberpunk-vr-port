-- Magazine hold at 115 mm out of the well, LEFT hand. RECORDED from the game's own reload.
-- (reload_record_03.lua, frames t=1.93..2.02, depth 116..113 mm): parent-local quats.
-- One stage of the insertion. The animation does not swap between two grips, it rolls through
-- them: fingers around the magazine on the way in, palm on its base at the end.
-- Then TUNED in VR through the CET overlay and baked back in: the animation's hand re-grips the
-- magazine and a VR hand cannot, so a few joints are curled harder than the take has them
-- (thumb base 1.22, index base 1.20, three metacarpals 1.03-1.05). Angle-scaled, so this is
-- still the recorded direction of each joint, only further along it.
return {
  { 'LeftInHandThumb', 0.922387, -0.217875, -0.315807, -0.044713 },
  { 'LeftInHandIndex', 0.775020, -0.058311, -0.036576, 0.628176 },
  { 'LeftInHandMiddle', 0.689372, -0.007191, 0.026229, 0.723897 },
  { 'LeftInHandRing', 0.615644, 0.080874, 0.062097, 0.781400 },
  { 'LeftInHandPinky', 0.490640, 0.196666, 0.082509, 0.844859 },
  { 'LeftHandThumb1', 0.014137, 0.001625, -0.355010, 0.934754 },
  { 'LeftHandThumb2', 0.000022, 0.000026, -0.020380, 0.999792 },
  { 'LeftHandIndex1', 0.018415, 0.043309, -0.515855, 0.855383 },
  { 'LeftHandIndex2', 0.000022, 0.000019, -0.291912, 0.956445 },
  { 'LeftHandIndex3', 0.000019, 0.000024, 0.042050, 0.999116 },
  { 'LeftHandMiddle1', -0.045304, 0.201647, -0.478638, 0.853342 },
  { 'LeftHandMiddle2', 0.000022, 0.000020, -0.366785, 0.930306 },
  { 'LeftHandMiddle3', -0.027326, -0.001619, 0.058775, 0.997896 },
  { 'LeftHandRing1', 0.093915, 0.105047, -0.526257, 0.838570 },
  { 'LeftHandRing2', 0.000022, 0.000021, -0.546833, 0.837242 },
  { 'LeftHandRing3', 0.000021, 0.000019, -0.254313, 0.967122 },
  { 'LeftHandPinky1', 0.090136, 0.180406, -0.793926, 0.573595 },
  { 'LeftHandPinky2', 0.000020, 0.000021, -0.680090, 0.733129 },
  { 'LeftHandPinky3', 0.000024, 0.000020, 0.111287, 0.993788 },
}
