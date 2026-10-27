#pragma once

// THE NATIVE RELOAD'S ARMS. See src/Anim/ReloadHandOff.cpp for the whole mechanism.
//
// Signatures are copied from the definitions, never retyped.

namespace cvr::anim {

// The reload state machine (ReloadEvents, redscript): entered with the seconds until its state can end,
// or a negative value when that is not known in advance (a shell of a looped reload that does not fill
// the magazine); left with active = false.
void ReloadHandOffSet(bool active, float finishInSec);

// Called from the pose hook once per FRESH solve, in this order:
//
//   ReloadHandOffCapture   right after the first VRIK_ComputeFK, beside WheelCaptureAnim: the buffer still
//                          holds the pure ANIMATED pose, and each hand is recorded relative to the chest.
//   ReloadHandOffUpdate    the blend toward or away from the animation.
//   ReloadHandOffBlendTarget  per arm, beside WheelBlendTarget: moves the IK target toward the animated
//                          hand, re-placed on the solved chest. A no-op at weight 0. hand 0 = right, 1 = left.
void ReloadHandOffCapture();
void ReloadHandOffUpdate();
void ReloadHandOffBlendTarget(int hand, float* target, float* handRot);

// 0 = the controllers own the hands, 1 = the reload animation does (eased). Also what the finger layers
// hand back to the animation.
float ReloadHandOffWeight();

}  // namespace cvr::anim
