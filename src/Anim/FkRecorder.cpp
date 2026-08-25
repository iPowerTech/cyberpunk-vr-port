// FkRecorder -- publishing the whole ANIMATED skeleton, for authoring reload poses.
//
// This is a TOOL, not part of the solve, and it is here rather than deleted because it is how the
// shipped reload poses were authored in the first place: with VRIK off, the pose hook publishes the
// engine's own animated bones so the recorder mod can capture a real animation frame by frame. Delete
// it and the next weapon's reload cannot be recorded.
//
// It ran inline in the pose detour, one branch deep in the hot path, next to the solve. That is the
// only thing that was wrong with it.
//
// Gated by g_VRRecordFK, which the CyberpunkVRPort_ReloadRecorder mod sets through the VRRecordFK
// native. The guard stays in src/Hooks/AnimPose.cpp; this function is entered only when recording is
// actually on, and it needs no __try -- the detour's __except covers it.

#include "Anim/VrikHook.hpp"
#include "Anim/CharacterRig.hpp"
#include "Anim/WeaponRig.hpp"
#include "Anim/SmokingPose.hpp"
#include "Anim/ReloadPose.hpp"
#include "Hooks/Hook.hpp"
#include <MinHook.h>
#include "Anim/FkRecorder.hpp"

namespace cvr {
namespace anim {

void VrikPublishAnimatedFk(uint8_t* boneBuf) {
                    VRIK_ComputeFK(boneBuf, VRIK_FKCount());
                    const int fkN  = VRIK_FKCount();
                    const int lim = (fkN < VRIK_MAX_BONES) ? fkN : VRIK_MAX_BONES;
                    for (int i = 0; i < lim; ++i) {
                        g_VRFKSnapPos[i][0] = g_fkPos[i][0];
                        g_VRFKSnapPos[i][1] = g_fkPos[i][1];
                        g_VRFKSnapPos[i][2] = g_fkPos[i][2];
                        g_VRFKSnapRot[i][0] = g_fkRot[i][0];
                        g_VRFKSnapRot[i][1] = g_fkRot[i][1];
                        g_VRFKSnapRot[i][2] = g_fkRot[i][2];
                        g_VRFKSnapRot[i][3] = g_fkRot[i][3];
                    }
                    g_VRFKSnapCount = lim;
}

}  // namespace anim
}  // namespace cvr
