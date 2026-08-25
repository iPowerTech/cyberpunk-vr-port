-- The SECOND way this pistol's hand carries a magazine: the recorded carry pose with the per-joint weights the
-- user dialled in in the overlay baked in (1.23, 1.05, 0.87, 0.86, 0.79, 1.23, 1.23, 0.78, 0.73, 1.00, 0.74,
-- 0.98, 1.00, 0.78, 1.00, 1.00, 0.88, 1.00, 1.00 -- printed from the game).
-- Scaled by ANGLE, the way the overlay itself does it: scaling the components of a quaternion does not give a
-- rotation at all.
-- The module picks between this and kenshin_mag_left_d122 at random each time a magazine comes into the hand.
return {
  { 'LeftInHandThumb', -0.799633, 0.466075, 0.163375, -0.341570 },
  { 'LeftInHandIndex', -0.692051, 0.033141, 0.004554, -0.721073 },
  { 'LeftInHandMiddle', 0.613270, 0.021178, 0.033299, 0.788887 },
  { 'LeftInHandRing', 0.574107, 0.085401, 0.043724, 0.813139 },
  { 'LeftInHandPinky', 0.449456, 0.149810, 0.080373, 0.876976 },
  { 'LeftHandThumb1', -0.048513, -0.019884, -0.583744, 0.810243 },
  { 'LeftHandThumb2', -0.000000, 0.000000, -0.610550, 0.791978 },
  { 'LeftHandIndex1', -0.121761, 0.058670, -0.512412, 0.848037 },
  { 'LeftHandIndex2', -0.000002, -0.000006, -0.448422, 0.893822 },
  { 'LeftHandIndex3', -0.000007, 0.000000, -0.484995, 0.874517 },
  { 'LeftHandMiddle1', -0.034201, 0.022505, -0.479746, 0.876451 },
  { 'LeftHandMiddle2', 0.000004, -0.000001, -0.639631, 0.768682 },
  { 'LeftHandMiddle3', 0.007912, -0.004635, -0.412487, 0.910917 },
  { 'LeftHandRing1', -0.012482, 0.020283, -0.504211, 0.863252 },
  { 'LeftHandRing2', -0.000005, 0.000005, -0.643356, 0.765567 },
  { 'LeftHandRing3', 0.007473, -0.004322, -0.398547, 0.917107 },
  { 'LeftHandPinky1', 0.070261, 0.037996, -0.592390, 0.801682 },
  { 'LeftHandPinky2', -0.000006, -0.000006, -0.491374, 0.870949 },
  { 'LeftHandPinky3', -0.000002, 0.000006, -0.370620, 0.928785 },
}
