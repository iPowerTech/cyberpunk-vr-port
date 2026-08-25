-- The Overture's trigger finger IN the trigger, RECORDED WHILE ACTUALLY FIRING (reload_record_SHOOT) -- no
-- conversion, no synthesis, no borrowed amount.
-- 
-- Two earlier attempts came out of the player's GLB and both read as the finger LIFTING rather than bending, and the
-- numbers said why: a runtime finger curls about its own -Z -- the Liberty's palm has Index2 at exactly
-- (0, 0, -0.737, 0.676) -- while the GLB's absolute pose for the same joint came out with its largest component on X,
-- which is a SPREAD. An absolute pose replaces a joint's rotation outright, so it brings the animation hand's whole
-- rest orientation with it, and that hand does not hold this gun the way a tracked one does. A third attempt kept a
-- recorded rest and composed the animation's bend onto it, which fixed the axis but was still half-invented.
-- 
-- This is the thing itself: the parent-local rotations of the three knuckles at the most curled frame of a take made
-- while shooting, in the same rig they are written back into. The trigger finger's own travel, measured on the hand
-- that owns it. PARENT-LOCAL matters -- the recorder stores model-space quaternions, and read raw the bend comes out
-- as hundreds of degrees.
-- 
-- THREE JOINTS, the knuckles only. Not the metacarpal: that is the finger's spread, and driving it swings the whole
-- finger away from the hand. Named Left like every file in the set and measured on the right hand; `forHand` renames
-- it to whichever wrist it lands on.
return {
  { 'LeftHandIndex1', -0.080208, 0.059494, -0.230596, 0.967911 },
  { 'LeftHandIndex2', -0.000022, -0.000021, -0.396964, 0.917834 },
  { 'LeftHandIndex3', 0.016565, 0.026618, -0.574678, 0.817779 },
}
