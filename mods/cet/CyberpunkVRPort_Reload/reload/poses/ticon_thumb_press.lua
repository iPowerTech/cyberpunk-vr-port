-- The Ticon's THUMB pressing the block's release, from `empty_reload` at t=2.467 -- the frame `end_slider`
-- leaves the catch (its channel runs -105.3 -> -41.8 between 2.400 and 2.467).
-- 
-- CONVERTED FROM THE GLB, and it is not optional: WolvenKit exports Y-up where the engine is Z-up, so every
-- quaternion needs (x, y, z, w) -> (x, -z, y, w). Written raw the first time, which put the thumb's rotation
-- on the wrong axis and folded it into the palm. The conversion is checkable here: the thumb tip comes out
-- (0, 0, -0.398, 0.918) against the recorder's own left-hand (0, 0, -0.407, 0.914) from another weapon --
-- 1.2 deg apart on the same axis, two independent routes to the same joint. It also settles that the two
-- hands share a sign convention, which is what lets one file serve either wrist.
-- 
-- THREE JOINTS ONLY, so this can be laid on the hand HOLDING the gun without disturbing its grip -- a pose
-- writes just the joints it names. Measured against the same hand half a second earlier the press is the
-- metacarpal swinging out 16.2 -> 37.9 deg while the phalanges STRAIGHTEN, 8.9 -> 1.8 and 30.1 -> 12.3.
-- Over those frames index and middle move 0.0 deg and ring and little 1.0 and 2.0: they go on holding the gun.
-- 
-- Named Left like every file in the set, and measured on the right hand; `forHand` renames it to whichever
-- wrist it lands on.
return {
  { 'LeftInHandThumb', -0.752401, 0.554476, 0.127731, 0.331864 },
  { 'LeftHandThumb1', -0.170789, 0.091314, -0.355718, 0.914307 },
  { 'LeftHandThumb2', 0.000000, 0.000000, -0.397687, 0.917521 },
}
