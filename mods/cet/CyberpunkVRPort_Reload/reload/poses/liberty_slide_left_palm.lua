-- The Liberty's palm on the slide: `unity_slide_left_palm` with every FINGER joint curled 12 % further in
-- (the four fingers' base, middle and tip; thumb and metacarpals untouched), scaled by ANGLE the way the
-- overlay does it -- multiplying a quaternion's components is not a rotation. 15 % first, then bent back to 12.
--
-- A COPY rather than an edit: four weapons share the original (Unity, Tamayura, Nue, Liberty).
--
-- NOT a geometric correction, and worth saying so: the two slides measure the SAME across the gripped stretch
-- -- 27.8 mm wide on both, 32.7 against 33.1 mm tall, girth 121.2 against 121.8 -- so the fingers were not
-- sitting open because the Liberty is thinner. Whatever the cause, the bend was judged by eye and it is that.
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
  { 'LeftHandRing1', -0.006396, -0.036810, -0.409913, 0.911359 },
  { 'LeftHandRing2', -0.000001, 0.000003, -0.684759, 0.728770 },
  { 'LeftHandRing3', 0.004894, 0.004547, -0.178830, 0.983857 },
  { 'LeftHandPinky1', 0.122544, -0.120235, -0.255328, 0.951490 },
  { 'LeftHandPinky2', 0.000002, 0.000000, -0.684658, 0.728865 },
  { 'LeftHandPinky3', -0.000002, -0.000002, -0.241597, 0.970377 },
}
