-- Magazine hold at 50 mm out of the well, LEFT hand. RECORDED from the game's own reload.
-- (reload_record_03.lua, frames t=2.13..2.17, depth 73..43 mm): parent-local quats.
-- One stage of the insertion. The animation does not swap between two grips, it rolls through
-- them: fingers around the magazine on the way in, palm on its base at the end.
-- Then TUNED in VR through the CET overlay and baked back in: the animation's hand re-grips the
-- magazine and a VR hand cannot, so a few joints are curled harder than the take has them
-- (thumb base 1.22, index base 1.20, three metacarpals 1.03-1.05). Angle-scaled, so this is
-- still the recorded direction of each joint, only further along it.
return {
  { 'LeftInHandThumb', -0.815869, 0.458263, 0.352146, -0.018574 },
  { 'LeftInHandIndex', 0.775332, -0.052039, -0.029350, 0.628722 },
  { 'LeftInHandMiddle', 0.688434, 0.011983, 0.044488, 0.723834 },
  { 'LeftInHandRing', 0.648762, 0.077156, 0.085636, 0.752211 },
  { 'LeftInHandPinky', 0.557065, 0.133979, 0.177760, 0.800081 },
  { 'LeftHandThumb1', -0.144495, 0.056978, -0.460430, 0.874002 },
  { 'LeftHandThumb2', 0.000019, 0.000021, -0.102081, 0.994776 },
  { 'LeftHandIndex1', -0.016788, 0.046974, -0.117498, 0.991819 },
  { 'LeftHandIndex2', 0.000024, 0.000024, -0.263695, 0.964606 },
  { 'LeftHandIndex3', 0.000023, 0.000020, -0.140889, 0.990025 },
  { 'LeftHandMiddle1', -0.012263, 0.022288, -0.162926, 0.986310 },
  { 'LeftHandMiddle2', 0.000026, 0.000017, -0.377094, 0.926175 },
  { 'LeftHandMiddle3', -0.003445, 0.000266, -0.111554, 0.993752 },
  { 'LeftHandRing1', 0.025851, -0.031491, -0.133048, 0.990272 },
  { 'LeftHandRing2', 0.000018, 0.000023, -0.395389, 0.918514 },
  { 'LeftHandRing3', -0.002510, 0.004619, -0.252804, 0.967503 },
  { 'LeftHandPinky1', 0.021274, -0.055718, -0.233398, 0.970550 },
  { 'LeftHandPinky2', 0.000020, 0.000024, -0.350593, 0.936528 },
  { 'LeftHandPinky3', 0.000023, 0.000019, -0.200329, 0.979729 },
}
