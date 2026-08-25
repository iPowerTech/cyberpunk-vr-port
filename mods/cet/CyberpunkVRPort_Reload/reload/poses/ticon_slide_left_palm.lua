-- The Ticon's palm on its rear block: `liberty_slide_left_palm` with the RING and LITTLE fingers opened out by
-- 5 % -- the three curl joints of each, scaled by ANGLE the way the overlay does it. The metacarpals are left
-- alone: they set how the fingers are spread, not how far they are closed.
-- 
-- A COPY rather than an edit, because the original is the Liberty's and it is committed and right there.
-- 
-- (This filename previously held a pose pulled straight out of `first_equip` at t=2.767. That one had the
-- game's exact fingers but no wrist to go with them, and read as too spread in the headset; it is one command
-- away if it is ever wanted -- tools glb_hand.py against the pwa GLB, clip first_equip, t=2.767, Left.)
-- 
-- opened: Ring1 48.6->46.2 deg, Ring2 86.4->82.1 deg, Ring3 20.6->19.6 deg, Pinky1 35.8->34.0 deg, Pinky2 86.4->82.1 deg, Pinky3 28.0->26.6 deg
return {
  { 'LeftInHandThumb', -0.928970, 0.297879, 0.126223, -0.179861 },
  { 'LeftInHandIndex', -0.749738, 0.033630, 0.012643, -0.660759 },
  { 'LeftInHandMiddle', 0.688001, 0.023759, 0.037362, 0.724358 },
  { 'LeftInHandRing', 0.652147, 0.097011, 0.049668, 0.750218 },
  { 'LeftInHandPinky', 0.558311, 0.166934, 0.091037, 0.807548 },
  { 'LeftHandThumb1', 0.025185, -0.010976, -0.284379, 0.958318 },
  { 'LeftHandThumb2', -0.000001, 0.000004, -0.406703, 0.913561 },
  { 'LeftHandIndex1', -0.125787, 0.079426, -0.403951, 0.902603 },
  { 'LeftHandIndex2', 0.000000, 0.000003, -0.737014, 0.675878 },
  { 'LeftHandIndex3', 0.113144, -0.038925, -0.265652, 0.956615 },
  { 'LeftHandMiddle1', -0.027028, 0.008519, -0.460275, 0.887324 },
  { 'LeftHandMiddle2', 0.000004, -0.000001, -0.690458, 0.723373 },
  { 'LeftHandMiddle3', 0.000002, -0.000002, -0.355835, 0.934549 },
  { 'LeftHandRing1', -0.006094, -0.035073, -0.390571, 0.919884 },
  { 'LeftHandRing2', -0.000001, 0.000003, -0.656794, 0.754070 },
  { 'LeftHandRing3', 0.004652, 0.004322, -0.169978, 0.985427 },
  { 'LeftHandPinky1', 0.116603, -0.114406, -0.242950, 0.956185 },
  { 'LeftHandPinky2', 0.000002, 0.000000, -0.656695, 0.754157 },
  { 'LeftHandPinky3', -0.000002, -0.000002, -0.229740, 0.973252 },
}
