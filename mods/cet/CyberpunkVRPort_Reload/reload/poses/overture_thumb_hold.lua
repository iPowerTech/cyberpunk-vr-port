-- The Overture's thumb ON the hammer, from `empty_reload` at t=0.600. The game holds this for 1.33 s of every
-- reload -- it is the shape the user asked to keep.
-- 
-- THREE JOINTS ONLY -- the thumb -- so it lays over whatever the hand is doing with the gun without disturbing the
-- grip: a pose writes just the joints it names. Converted from the GLB, (x, y, z, w) -> (x, -z, y, w).
-- 
-- Measured against the same hand at rest: over the reach the metacarpal goes 0 -> 65.1 deg while the tip arcs out
-- to 76.5 and settles back to 26.0, and index and middle move 16.2 and 0.0 -- they never leave the gun.
return {
  { 'LeftInHandThumb', -0.745526, 0.652040, -0.041448, -0.131596 },
  { 'LeftHandThumb1', -0.011278, -0.002244, -0.314340, 0.949241 },
  { 'LeftHandThumb2', 0.000000, 0.000000, -0.390773, 0.920487 },
}
