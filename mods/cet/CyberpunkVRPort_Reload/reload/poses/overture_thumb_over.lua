-- The Overture's thumb MID-REACH, from `empty_reload` at t=0.367 -- the top of the arc it makes coming off the
-- grip and onto the hammer. It is a waypoint, not a rest: passing straight from the grip to the hammer reads as
-- a finger sliding through the frame, and the game's own hand goes over the top.
-- 
-- THREE JOINTS ONLY -- the thumb -- so it lays over whatever the hand is doing with the gun without disturbing the
-- grip: a pose writes just the joints it names. Converted from the GLB, (x, y, z, w) -> (x, -z, y, w).
-- 
-- Measured against the same hand at rest: over the reach the metacarpal goes 0 -> 65.1 deg while the tip arcs out
-- to 76.5 and settles back to 26.0, and index and middle move 16.2 and 0.0 -- they never leave the gun.
return {
  { 'LeftInHandThumb', -0.824691, 0.554359, 0.080000, 0.078559 },
  { 'LeftHandThumb1', 0.023639, -0.068113, -0.312042, 0.947329 },
  { 'LeftHandThumb2', 0.000000, 0.000000, 0.038703, 0.999251 },
}
