-- The Overture's thumb PRESSING the hammer down. Synthesised, and it says so: no clip anywhere puts a thumb on
-- this hammer to move it -- the game only ever rests one there -- so the press is the HOLD pose with the two
-- phalanges pushed 35 % further and the metacarpal 10 % out, which is a thumb bearing down rather than resting.
-- Scaled by ANGLE the way the overlay does it.
-- 
-- THREE JOINTS ONLY -- the thumb -- so it lays over whatever the hand is doing with the gun without disturbing the
-- grip: a pose writes just the joints it names. Converted from the GLB, (x, y, z, w) -> (x, -z, y, w).
-- 
-- Measured against the same hand at rest: over the reach the metacarpal goes 0 -> 65.1 deg while the tip arcs out
-- to 76.5 and settles back to 26.0, and index and middle move 16.2 and 0.0 -- they never leave the gun.
return {
  { 'LeftInHandThumb', -0.717973, 0.627942, -0.039916, -0.297675 },
  { 'LeftHandThumb1', -0.015011, -0.002987, -0.418387, 0.908140 },
  { 'LeftHandThumb2', 0.000000, 0.000000, -0.515839, 0.856686 },
}
