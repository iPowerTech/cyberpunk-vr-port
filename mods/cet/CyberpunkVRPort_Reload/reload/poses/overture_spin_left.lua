-- The Overture's LEFT hand SWEEPING the cylinder round -- the game's own, out of `reload_01` at t=1.500.
-- 
-- AN OPEN HAND, which is the whole point of it and what two earlier attempts got wrong. In the reload the left
-- palm comes across the cylinder with the fingers EXTENDED and rolls it three chambers round: at t=1.467 to 1.533 the
-- crane stands 94.3 deg out, the cylinder turns 35.5 -> 92.4 deg (about 850 deg/s) and the four fingers total only 71
-- to 85 deg of curl -- against 280 in a fist. That is a hand brushing across a cylinder, not gripping one.
-- 
-- The idle flourish (`idle_break_01`) was tried first and is the wrong gesture: there the hand CUPS the cylinder.
-- 
-- DELTA, NOT ABSOLUTE, and that is the lesson of two earlier finger poses that came out of the GLB looking like the
-- finger had gone sideways. An absolute pose replaces a joint's rotation and so brings the animation skeleton's whole
-- rest with it; a DELTA between two frames of one clip carries only what the hand did, and it is composed onto the
-- recorded runtime rest (the take's own left hand, t=0.35..0.75, doing nothing).
-- 
-- And the axis map was CHOSEN rather than assumed. The weapon rigs' GLB turn is (x, y, z) -> (x, -z, y), checked on
-- six bones at once -- but every recorder-measured pose in this project curls a phalanx about its own -Z, so the map
-- is the one whose delta comes out with |z| dominant on the six curl joints. It picked (x,-z,y), with a mean z share
-- of 0.99 against the next best.
return {
  { 'LeftInHandThumb', -0.928956, 0.272295, 0.115300, -0.222715 },
  { 'LeftInHandIndex', -0.752468, -0.025284, 0.077949, -0.653511 },
  { 'LeftInHandMiddle', 0.681226, 0.097591, -0.030284, 0.724907 },
  { 'LeftInHandRing', 0.641104, 0.146400, 0.000219, 0.753361 },
  { 'LeftInHandPinky', 0.555959, 0.185985, 0.078687, 0.806305 },
  { 'LeftHandThumb1', -0.012954, 0.002698, -0.144858, 0.989364 },
  { 'LeftHandThumb2', -0.000001, -0.000000, -0.033009, 0.999455 },
  { 'LeftHandIndex1', -0.140733, 0.058211, -0.025973, 0.987993 },
  { 'LeftHandIndex2', 0.000001, 0.000000, 0.079791, 0.996812 },
  { 'LeftHandIndex3', -0.000031, -0.000016, -0.088293, 0.996095 },
  { 'LeftHandMiddle1', -0.038191, -0.033695, -0.006211, 0.998683 },
  { 'LeftHandMiddle2', -0.000029, -0.000006, 0.014352, 0.999897 },
  { 'LeftHandMiddle3', -0.000040, -0.000029, -0.043229, 0.999065 },
  { 'LeftHandRing1', 0.018492, -0.074769, -0.025790, 0.996696 },
  { 'LeftHandRing2', -0.000030, -0.000008, -0.013808, 0.999905 },
  { 'LeftHandRing3', 0.000001, 0.000000, 0.024850, 0.999691 },
  { 'LeftHandPinky1', 0.018083, -0.089415, -0.024503, 0.995529 },
  { 'LeftHandPinky2', -0.000028, -0.000009, -0.057720, 0.998333 },
  { 'LeftHandPinky3', 0.000000, 0.000000, -0.041839, 0.999124 },
}
