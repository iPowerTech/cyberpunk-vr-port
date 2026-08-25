-- Magazine hold at 25 mm out of the well, LEFT hand. RECORDED from the game's own reload.
-- (reload_record_03.lua, frames t=2.17..2.29, depth 43..0 mm): parent-local quats.
-- One stage of the insertion. The animation does not swap between two grips, it rolls through
-- them: fingers around the magazine on the way in, palm on its base at the end.
-- Then TUNED in VR through the CET overlay and baked back in: the animation's hand re-grips the
-- magazine and a VR hand cannot, so a few joints are curled harder than the take has them
-- (thumb base 1.22, index base 1.20, three metacarpals 1.03-1.05). Angle-scaled, so this is
-- still the recorded direction of each joint, only further along it.
return {
  { 'LeftInHandThumb', -0.836080, 0.454367, 0.306454, 0.024630 },
  { 'LeftInHandIndex', 0.775373, -0.051175, -0.028382, 0.628787 },
  { 'LeftInHandMiddle', 0.688270, 0.014600, 0.046980, 0.723785 },
  { 'LeftInHandRing', 0.653118, 0.076762, 0.088871, 0.748095 },
  { 'LeftInHandPinky', 0.565124, 0.125868, 0.191004, 0.792660 },
  { 'LeftHandThumb1', -0.104684, 0.035520, -0.285496, 0.951983 },
  { 'LeftHandThumb2', 0.000023, 0.000024, -0.039450, 0.999222 },
  { 'LeftHandIndex1', -0.032123, 0.025167, -0.173356, 0.984013 },
  { 'LeftHandIndex2', 0.000020, 0.000022, -0.223840, 0.974626 },
  { 'LeftHandIndex3', 0.000021, 0.000021, -0.131197, 0.991356 },
  { 'LeftHandMiddle1', 0.001304, 0.008110, -0.172781, 0.984926 },
  { 'LeftHandMiddle2', 0.000022, 0.000020, -0.391148, 0.920328 },
  { 'LeftHandMiddle3', -0.021329, 0.000484, -0.072054, 0.997173 },
  { 'LeftHandRing1', 0.026569, -0.045952, -0.155556, 0.986400 },
  { 'LeftHandRing2', 0.000022, 0.000021, -0.365322, 0.930881 },
  { 'LeftHandRing3', -0.002849, 0.005244, -0.252586, 0.967556 },
  { 'LeftHandPinky1', 0.054159, -0.085897, -0.234349, 0.966834 },
  { 'LeftHandPinky2', 0.000023, 0.000023, -0.344559, 0.938765 },
  { 'LeftHandPinky3', 0.000020, 0.000021, -0.241964, 0.970285 },
}
