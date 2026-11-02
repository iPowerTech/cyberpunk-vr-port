// HAND INTERACTION, ported from the fork (main: src/Hooks/LookAtRayFromHand.cpp). The engine has one look-at ray
// per targeting user, built in TargetingUser_UpdateLookAtRay from a virtual call that writes a Vector4 position and
// a Quaternion; the fork rewrote that transform from a palm, so the hand that points at something is what selects
// it. Upstream 0.1.9 hooks the very same instruction for its story attention (Hooks/StoryAttention.cpp, 2.5
// +2F262D), so the two cannot both patch it: the story hook calls AimFromHand for the player's own ray whenever no
// story mode owns it, and the story mode wins when one does.
#include "Hooks/HandInteract.hpp"
#include "Anim/VrikHook.hpp"
#include "Anim/VrikState.hpp"
#include "Core/LiveControls.hpp"
#include "Core/VrCoreShared.hpp"
#include "Utils/SharedSlots.hpp"

#include <windows.h>
#include <atomic>
#include <cstdint>

extern void Log(const char* fmt, ...);
extern "C" __declspec(dllexport) extern int CyberpunkVR_TwoHandActive;   // Anim/TwoHandGrip.cpp
extern "C" volatile uint64_t CyberpunkVR_DualWieldUntil;                  // Hooks/XInput.cpp

extern "C" {
// Rays aimed from a hand / left as the engine built them (no hand points, or no palm pose yet).
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_HandInteractRays[2]{};
}

namespace cvr::interact {
namespace {

// -1 = nobody, 0 = left, 1 = right. Written on the job thread that owns the decision, published to the CET side
// by the input poll (vrshared::kPointingHand) so Lua never works it out a second way.
std::atomic<int> g_pointingHand{1};

// DID THE PREVIOUS RAY FIND ANYTHING? From the script API, once a frame and tagged with the hand it is about: two
// engine offsets read for it in the fork were wrong (a container that always exists, a slot index), GetLookAtObject
// cannot be misread. STALENESS IS THE GUARD: a producer that stops (module reloaded, errored) must not keep steering
// the alternation, so an answer older than this is no answer.
std::atomic<int> g_acquired{0}, g_acquiredHand{-1};
std::atomic<uint64_t> g_acquiredAtMs{0};
constexpr uint64_t kAcquiredStaleMs = 500;
// HOW LONG A HAND KEEPS THE RAY BEFORE THE OTHER IS TRIED: the round trip. The hook casts from a hand, the engine
// resolves the look-at, the script reads it and publishes the answer, which cannot come back in the same frame;
// swapping every frame asked a question and dropped the hand before the reply arrived, and the lock never closed.
constexpr uint64_t kDwellMs = 50;

bool LastRayAcquiredFor(int hand) {
    const uint64_t at = g_acquiredAtMs.load(std::memory_order_relaxed);
    if (at == 0 || GetTickCount64() - at > kAcquiredStaleMs) return false;
    return g_acquiredHand.load(std::memory_order_relaxed) == hand && g_acquired.load(std::memory_order_relaxed) != 0;
}

// WHICH HAND POINTS: the ray alternates between the two hands until one finds something, then stays with it while it
// keeps finding something (and, once locked, through a miss for the Controls-tab target hold), and alternates again
// when it does not. No hand points while driving, with both hands on the gun, or with a gun in each hand (upstream's
// dual wield: neither hand is free); a weapon in the right hand leaves the left.
int ChoosePointingHand() {
    if (g_isInVehicle || CyberpunkVR_TwoHandActive != 0 || GetTickCount64() < CyberpunkVR_DualWieldUntil) return -1;
    if (g_pSharedHands && g_pSharedHands[vrshared::kWeaponFlag] > 0.5f) return 0;

    static int s_hand = 1;
    static uint64_t s_sinceMs = 0;   // last time s_hand was seen holding a target (or the last swap)
    static bool s_locked = false;    // s_hand has acquired at least once and not yet released
    const uint64_t now = GetTickCount64();
    if (s_sinceMs == 0) s_sinceMs = now;
    if (LastRayAcquiredFor(s_hand)) {
        s_sinceMs = now;
        s_locked = true;
        return s_hand;
    }
    uint64_t hold = kDwellMs;
    if (s_locked) hold = static_cast<uint64_t>(g_liveControls.xrHandTargetHoldMs > 0 ? g_liveControls.xrHandTargetHoldMs : 0);
    if (now - s_sinceMs < hold) return s_hand;
    s_hand = s_hand == 1 ? 0 : 1;
    s_sinceMs = now;
    s_locked = false;   // the new hand has to earn its own lock
    return s_hand;
}

}  // namespace

int PointingHand() { return g_pointingHand.load(std::memory_order_relaxed); }

void NoteAcquired(bool found, int hand) {
    g_acquired.store(found ? 1 : 0, std::memory_order_relaxed);
    g_acquiredHand.store(hand, std::memory_order_relaxed);
    g_acquiredAtMs.store(GetTickCount64(), std::memory_order_relaxed);
}

bool AimFromHand(float* transform) {
    if (!transform || !g_liveControls.xrHandInteract) return false;
    if (!g_VRPalmModelValid || !g_VRViewWorldValid) { ++CyberpunkVR_HandInteractRays[1]; return false; }
    const int hand = ChoosePointingHand();
    g_pointingHand.store(hand, std::memory_order_relaxed);
    if (hand < 0) { ++CyberpunkVR_HandInteractRays[1]; return false; }
    const bool right = hand == 1;
    if ((right ? g_VRPalmRIdx : g_VRPalmLIdx) < 0) { ++CyberpunkVR_HandInteractRays[1]; return false; }

    // Logged on the acquisition edge, not on the change of hand: unacquired, the hand swaps every 50 ms by design.
    {
        static int s_logged = 0, s_last = -1;
        const int acq = LastRayAcquiredFor(hand) ? 1 : 0;
        if (g_verboseLog && acq != s_last && s_logged < 20) {
            s_last = acq;
            ++s_logged;
            Log("HandInteract: acquired=%d -> ray %s on the %s hand\n", acq, acq ? "LOCKS" : "alternates",
                right ? "right" : "left");
        }
    }

    // MODEL SPACE TO WORLD, the bridge VRPalmWorldPos / VRPalmWorldRot use, as the same arithmetic rather than a
    // call (those take a script frame; this runs on a job thread): the ray and the hand the player sees stay one.
    const float camRot[4] = {g_VRCamModelRot[0], g_VRCamModelRot[1], g_VRCamModelRot[2], g_VRCamModelRot[3]};
    const float viewRot[4] = {g_VRViewWorldRot[0], g_VRViewWorldRot[1], g_VRViewWorldRot[2], g_VRViewWorldRot[3]};
    float camInv[4]; VRIK_QuatConj(camRot, camInv);
    float m2w[4]; VRIK_QuatMul(viewRot, camInv, m2w); VRIK_QuatNorm(m2w);

    const volatile float* pm = right ? g_VRPalmModelR : g_VRPalmModelL;
    const float rel[3] = {pm[0] - g_VRCamModelPos[0], pm[1] - g_VRCamModelPos[1], pm[2] - g_VRCamModelPos[2]};
    float wpos[3]; VRIK_QuatRotateVec(m2w, rel, wpos);

    const volatile float* pr = right ? g_VRPalmModelRotR : g_VRPalmModelRotL;
    const float palm[4] = {pr[0], pr[1], pr[2], pr[3]};
    if (palm[0] == 0.0f && palm[1] == 0.0f && palm[2] == 0.0f && palm[3] == 0.0f) {
        ++CyberpunkVR_HandInteractRays[1];
        return false;
    }
    // WHICH WAY IS "POINTING" ON THIS BONE. The engine reads the quaternion's +Y as forward, and on the palm bone +Y
    // runs along the thumb: the bone's rotation is corrected in its own frame by the rotation that brings +Y onto the
    // axis the fingers run along -- a fact about the rig, settled in the headset (default -X). The table is signed
    // pairs, and the LEFT hand is mirrored, so flipping the low bit is "the same axis, the other way".
    static const float kAxisFix[6][4] = {
        {0.0f, 0.0f, -0.70710678f, 0.70710678f},   // 0: +X
        {0.0f, 0.0f, 0.70710678f, 0.70710678f},    // 1: -X
        {0.0f, 0.0f, 0.0f, 1.0f},                  // 2: +Y, the raw bone forward
        {1.0f, 0.0f, 0.0f, 0.0f},                  // 3: -Y
        {0.70710678f, 0.0f, 0.0f, 0.70710678f},    // 4: +Z
        {-0.70710678f, 0.0f, 0.0f, 0.70710678f},   // 5: -Z
    };
    int axis = g_liveControls.xrHandInteractAxis;
    if (axis < 0 || axis > 5) axis = 1;
    if (!right) axis ^= 1;
    float aimed[4]; VRIK_QuatMul(palm, kAxisFix[axis], aimed); VRIK_QuatNorm(aimed);
    float wrot[4]; VRIK_QuatMul(m2w, aimed, wrot); VRIK_QuatNorm(wrot);

    transform[0] = g_VRViewWorldPos[0] + wpos[0];
    transform[1] = g_VRViewWorldPos[1] + wpos[1];
    transform[2] = g_VRViewWorldPos[2] + wpos[2];
    // [3] is the position's w: the engine's own value is the one the pipeline expects.
    transform[4] = wrot[0]; transform[5] = wrot[1]; transform[6] = wrot[2]; transform[7] = wrot[3];

    static bool s_first = true;
    if (s_first) {
        s_first = false;
        Log("HandInteract: first ray from the %s hand pos=(%.3f,%.3f,%.3f) quat=(%.3f,%.3f,%.3f,%.3f)\n",
            right ? "right" : "left", transform[0], transform[1], transform[2], transform[4], transform[5],
            transform[6], transform[7]);
    }
    ++CyberpunkVR_HandInteractRays[0];
    return true;
}

}  // namespace cvr::interact
