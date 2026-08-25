-- Magazine hold at 80 mm out of the well, LEFT hand. RECORDED from the game's own reload.
-- (reload_record_03.lua, frames t=2.09..2.13, depth 86..73 mm): parent-local quats.
-- One stage of the insertion. The animation does not swap between two grips, it rolls through
-- them: fingers around the magazine on the way in, palm on its base at the end.
-- Then TUNED in VR through the CET overlay and baked back in: the animation's hand re-grips the
-- magazine and a VR hand cannot, so a few joints are curled harder than the take has them
-- (thumb base 1.22, index base 1.20, three metacarpals 1.03-1.05). Angle-scaled, so this is
-- still the recorded direction of each joint, only further along it.
return {
  { 'LeftInHandThumb', 0.866974, -0.348056, -0.345990, -0.086623 },
  { 'LeftInHandIndex', 0.775208, -0.054853, -0.032577, 0.628476 },
  { 'LeftInHandMiddle', 0.688910, 0.003398, 0.036307, 0.723929 },
  { 'LeftInHandRing', 0.634185, 0.078651, 0.075062, 0.765499 },
  { 'LeftInHandPinky', 0.528863, 0.161401, 0.134658, 0.822266 },
  { 'LeftHandThumb1', -0.075435, 0.029200, -0.417886, 0.904891 },
  { 'LeftHandThumb2', 0.000020, 0.000021, -0.066696, 0.997773 },
  { 'LeftHandIndex1', 0.000792, 0.048012, -0.302363, 0.951983 },
  { 'LeftHandIndex2', 0.000019, 0.000022, -0.285692, 0.958321 },
  { 'LeftHandIndex3', 0.000027, 0.000023, -0.059822, 0.998209 },
  { 'LeftHandMiddle1', -0.039393, 0.099864, -0.317779, 0.942068 },
  { 'LeftHandMiddle2', 0.000022, 0.000019, -0.374005, 0.927427 },
  { 'LeftHandMiddle3', -0.014072, 0.000218, -0.036409, 0.999238 },
  { 'LeftHandRing1', 0.044796, 0.034587, -0.318109, 0.946364 },
  { 'LeftHandRing2', 0.000023, 0.000025, -0.481973, 0.876186 },
  { 'LeftHandRing3', -0.001392, 0.002556, -0.253496, 0.967332 },
  { 'LeftHandPinky1', 0.011249, 0.060811, -0.527962, 0.847013 },
  { 'LeftHandPinky2', 0.000024, 0.000022, -0.517110, 0.855919 },
  { 'LeftHandPinky3', 0.000021, 0.000019, -0.061384, 0.998114 },
}
