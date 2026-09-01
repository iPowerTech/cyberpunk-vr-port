// LookAtRayFromHand -- the ray the game looks along leaves the PALM instead of the camera.
//
// ================================================================================================
// WHAT THIS REPLACES, AND WHY IT IS ONE PATCH RATHER THAN A FEATURE
//
// Everything the game decides by "what is the player looking at" -- the interaction prompt and its
// choice hub, the scanner target, aim assist, the look-at object scripts read -- is decided from a
// single ray, and that ray is built in exactly one place. The static trail, recorded so nobody has
// to walk it again:
//
//   TargetingSystem::Tick_UsersLookAt          RVA 0x4B99F0   (profiler scope only)
//     job body                                 RVA 0xCC6B2C
//       per user                               RVA 0x3F8E8C   TargetingUser_LookAtTick
//         recompute                            RVA 0x3F8F30   TargetingUser_UpdateLookAtRay
//
// and inside that last one:
//
//     mov  rax,[r9]                  ; r9 = the object at user+0x30 -- the ray's SOURCE
//     mov  r10,[rax+0x228]           ; its virtual "give me your world transform"
//     lea  r8,[rbp+0x20]
//     mov  rcx,r9
//     lea  rdx,[rbp-0x50]            ; -> Transform { Vector4 position; Quaternion orientation; }
//     call r10                       ; <-- the camera pose enters the engine HERE
//     mov  rax,[rbp-0x50]            ; <-- WE GO IN HERE
//     lea  rdx,[rsp+0x50]
//
// Downstream that transform becomes the query origin and, through a quaternion-to-forward, its
// direction; both are cached in the user state (+0x58 origin, +0x64 end point) and consumed by the
// later jobs -- broadphase, frustum tests, line of sight -- which produce the scored candidate list
// that `IsInteractionLookAtTarget` reads and the interaction visualiser's predicates classify.
//
// So there is exactly one place where the head's authority begins, and this is it. Patching the
// 32 bytes the virtual call just wrote moves the SOURCE; every consumer follows on its own. The
// alternative that was considered and dropped was chasing the consumers: a redscript TargetFilter,
// AddIgnoredLookAtEntity, a hook on the score writer. Each of those argues with one result of the
// query while leaving the query itself pointed at the head.
//
// WHY AFTER THE CALL AND NOT AT IT. `call r10` is three bytes -- too short for a jmp rel32. The two
// instructions that follow are nine bytes together, they are the first readers of what the call
// wrote, and rbp is intact across both. Rewriting the transform before `mov rax,[rbp-0x50]` runs
// means even that first read sees the hand.
//
// WHAT IS SAFE TO CLOBBER HERE, established by reading the site rather than assuming: after the
// call the function only uses rax, rcx and rdx, and sets each from memory or a lea before use. No
// volatile register is live across the injection point, so the trampoline needs shadow space and
// nothing else -- no register saves. The displaced `lea rdx,[rsp+0x50]` is rsp-relative, which is
// why the trampoline restores rsp exactly before re-executing it.
//
// THE FILTER, and it is not optional. Tick_UsersLookAt walks EVERY targeting user, not just the
// player's -- NPCs have look-at rays too, and rewriting theirs would point the whole street's
// attention at the player's hand. There is no cheap way to name the player's user from inside the
// hook, so the ray is judged by its own origin: the player's look-at starts at the player's eyes,
// which the port already knows as g_VRViewWorldPos. An origin further than ORIGIN_MATCH_M from the
// view is somebody else's and is left alone.
// ================================================================================================

#include "Anim/VrikHook.hpp"
#include "Anim/VrikState.hpp"
#include "Camera/CameraState.hpp"
#include "Core/LiveControls.hpp"
#include "Core/VrCoreShared.hpp"
#include "Hooks/Hook.hpp"
#include "Hooks/LookAtRayFromHand.hpp"
#include "Hooks/Trampoline.hpp"
#include "Utils/AobScanner.hpp"
#include "Utils/SharedSlots.hpp"

#include <windows.h>
#include <cstdint>
#include <cmath>

// The off hand is on the foregrip. Defined in src/Anim/TwoHandGrip.cpp; declared here rather than
// pulled in through a header because that is the only thing this file needs from it.
extern "C" __declspec(dllexport) extern int CyberpunkVR_TwoHandActive;

volatile int g_VRLookAtPatched  = 0;
volatile int g_VRLookAtSkipped  = 0;

// How near the player's own view a ray's origin has to start before we claim it. The player's
// look-at is built from the camera, so in practice this is centimetres; a metre of slack costs
// nothing and covers a camera that leads or lags the view by a frame. An NPC standing right next
// to the player is still metres away in origin terms, because their ray starts at THEIR eyes.
static constexpr float kOriginMatchM = 1.5f;

// ---- WHICH HAND POINTS: ALTERNATE, THEN LOCK -----------------------------------------------------
//
// THE ENGINE HAS ONE LOOK-AT RAY, so one hand casts it -- but which hand needs no threshold, no
// distance, and no setting. The ray ALTERNATES between the two hands until one of them finds
// something; then it stays with that hand for as long as that hand keeps finding something, and goes
// back to alternating the moment it does not.
//
// IT ALTERNATES ON A DWELL, NOT PER FRAME, and that is a correction the field found. The answer to
// "did this hand find anything" cannot arrive in the frame the question was asked, so swapping every
// frame meant every answer described the hand that had just been dropped, and the lock could never
// close. Worse, the object was then only hit on every other frame, so with a single hand pointing
// nothing was ever continuous and NO PROMPT APPEARED AT ALL -- while two hands pointing worked
// perfectly, because then every frame hit whichever hand was up. See kDwellMs.
//
// Both hands are therefore always live. The first one to acquire wins and holds, and it is the one
// that will do the action. Move it off the object and the alternation restarts, so the OTHER hand
// takes over as soon as it points at anything -- however far away it is, and whatever the other arm
// happens to be doing. There is no condition here that can be half-true.
//
// THIS REPLACED A REACH TEST, and the reach test was the wrong shape. It picked the hand further
// from the head, with 8 cm of hysteresis to stop it flapping -- so holding one hand out and touching
// a panel with the other did nothing until the touching hand became the more extended one. A
// heuristic with a threshold has to be tuned, and it is wrong on both sides of the threshold.
// Acquisition is not a heuristic: the engine either found an object or it did not.
//
// HOW ACQUISITION IS READ: from the script API, published once a frame and TAGGED WITH THE HAND it
// is about -- see LastRayAcquiredFor, and the two engine offsets that were tried first.
//
// TWO CASES WHERE NO HAND POINTS, and they are not defaults but answers:
//   * BOTH HANDS ON THE GUN (CyberpunkVR_TwoHandActive). The off hand is holding the weapon as
//     surely as the trigger hand; a ray from it points where the barrel does, which is aiming.
//   * DRIVING. The CET side already stood the gesture down in a vehicle; the RAY did not, so the
//     look-at still tracked a hand across the dashboard. Half a guard is worse than none: nothing
//     looks broken while prompts and the scanner quietly follow the wrong thing.
//
// -1 = nobody, 0 = left, 1 = right. Written here, on the job thread that owns the decision, and
// published to the CET side by the input poll so Lua never works it out a second way.
static volatile int g_VRPointingHandState = 1;

int  VRPointingHand()        { return g_VRPointingHandState; }
bool VRPointingHandIsRight() { return g_VRPointingHandState == 1; }

// Kept so the input poll has something to call; the decision itself now happens per look-at update,
// where the acquisition result is available.
void VRUpdatePointingHand() {}

// DID THE PREVIOUS RAY FIND ANYTHING?
//
// TWO WRONG ANSWERS CAME OUT OF THE ENGINE'S OWN MEMORY BEFORE THIS ONE, and both are worth keeping
// written down because both looked right:
//
//   * userState+0xb8 -- assumed to be the look-at object, null when nothing was found. It is a
//     CONTAINER that always exists, so "acquired" was always true and the ray never alternated.
//   * the byte at container+0x51 -- taken from FUN_1403f9a20, which skips the resolve when it is -1.
//     In the field it alternates 0 and 1 every frame: it is a slot index, and -1 never appeared.
//
// Both were caught by their own log lines inside one session, which is the only reason this did not
// become three wrong answers. The bit now comes from the script API instead, published once a frame
// by the Interact module: GetLookAtObject either returns an object or it does not, and that cannot
// be misread the way an offset can.
//
// STALENESS IS THE GUARD. If the producer stops -- module reloaded, errored, uninstalled -- the last
// value would otherwise keep steering the alternation forever. Older than kAcquiredStaleMs and the
// ray simply stops alternating and stays where it is: degraded, stable, and not a hand that swaps
// every frame under a player who is trying to point at something.
static constexpr uint64_t kAcquiredStaleMs = 500;

// HOW LONG A HAND KEEPS THE RAY BEFORE THE OTHER IS TRIED. Not a tuning knob: it is the round trip.
// The hook casts from a hand, the engine resolves the look-at, the script reads it and publishes the
// answer -- and that answer cannot come back in the same frame. Alternating every frame therefore
// asked a question and swapped hands before the reply arrived, so the reply always described the
// hand that had just been dropped and the lock could never close.
//
// The field report is exactly that shape: one hand pointing showed NO prompt, because the object was
// only hit on every other frame and nothing was ever continuous; two hands pointing always showed
// one, because then every frame hit whichever hand was up. 50 ms is a few frames -- enough for the
// reply, short enough that finding the target with the second hand is not a wait anybody notices.
static constexpr uint64_t kDwellMs = 50;

static bool LastRayAcquiredFor(int hand) {
    const uint64_t at = g_vrLookAtAcquiredAtMs.load(std::memory_order_relaxed);
    if (at == 0) return false;                                  // nothing published yet
    if (GetTickCount64() - at > kAcquiredStaleMs) return false;  // producer stopped: do not trust it
    if (g_vrLookAtAcquiredHand.load(std::memory_order_relaxed) != hand) return false;
    return g_vrLookAtAcquired.load(std::memory_order_relaxed) != 0;
}

static int ChoosePointingHand() {
    if (g_isInVehicle || CyberpunkVR_TwoHandActive != 0) return -1;
    // A weapon in the right hand leaves exactly one free hand, so there is nothing to alternate.
    if ((g_pSharedHands != nullptr) && (g_pSharedHands[vrshared::kWeaponFlag] > 0.5f)) return 0;

    static int      s_hand = 1;
    static uint64_t s_sinceMs = 0;    // last time s_hand was seen holding a target (or the last swap)
    static bool     s_locked = false; // s_hand has acquired at least once and not yet released
    const uint64_t now = GetTickCount64();
    if (s_sinceMs == 0) s_sinceMs = now;

    // THIS hand found something: it keeps the ray for as long as that stays true, and the moment it
    // does is when the lock is taken.
    if (LastRayAcquiredFor(s_hand)) {
        s_sinceMs = now;
        s_locked  = true;
        return s_hand;
    }

    // Not acquired this instant. HOW LONG THAT IS TOLERATED depends on whether a lock was ever taken:
    //
    //   * NOT LOCKED -- still alternating to find a target. Only the round-trip dwell (kDwellMs)
    //     applies: swap slower than that and the alternation outruns its own feedback and no prompt
    //     ever settles; see kDwellMs.
    //   * LOCKED -- this hand HAD the target and the ray has fallen through a hole in it (the gap
    //     between two buttons, a thin panel edge). Hold the lock for the configurable target-hold
    //     time so a momentary miss does not hand the target to the other hand -- which is exactly the
    //     bounce the player sees when pointing the second hand at a panel steals it from the first.
    uint64_t hold = kDwellMs;
    if (s_locked) {
        int cfg = g_liveControls.xrHandTargetHoldMs;
        if (cfg < 0) cfg = 0;
        hold = static_cast<uint64_t>(cfg);
    }
    if (now - s_sinceMs < hold) return s_hand;

    s_hand = (s_hand == 1) ? 0 : 1;
    s_sinceMs = now;
    s_locked  = false;   // the new hand has to earn its own lock
    return s_hand;
}

// The transform the virtual call wrote: a Vector4 position followed by a Quaternion, contiguous.
struct LookAtTransform {
    float px, py, pz, pw;
    float qi, qj, qk, qr;
};

// Called from the trampoline with a pointer to that transform and the targeting user state (rbx).
extern "C" void CyberpunkVR_LookAtRayFromHand(LookAtTransform* xf, void* /*userState*/) {
    if (!g_liveControls.xrHandInteract) return;      // the Controls-tab checkbox, read every call
    if (!xf) return;

    if (!g_VRPalmModelValid || !g_VRViewWorldValid) { ++g_VRLookAtSkipped; return; }

    // IS THIS RAY OURS? Every targeting user passes through here, NPCs included, and the alternation
    // is a state machine with a memory -- so this check has to come FIRST. Advancing it on somebody
    // else's ray would step the hand on frames the player's ray never even ran.
    const float dx = xf->px - g_VRViewWorldPos[0];
    const float dy = xf->py - g_VRViewWorldPos[1];
    const float dz = xf->pz - g_VRViewWorldPos[2];
    if (dx * dx + dy * dy + dz * dz > kOriginMatchM * kOriginMatchM) { ++g_VRLookAtSkipped; return; }

    // WHICH HAND POINTS: alternate until one acquires, then hold. See the block above the function.
    const int hand = ChoosePointingHand();
    g_VRPointingHandState = hand;
    if (hand < 0) { ++g_VRLookAtSkipped; return; }   // driving, or both hands on the gun
    const bool right = (hand == 1);

    const int idx = right ? g_VRPalmRIdx : g_VRPalmLIdx;
    if (idx < 0) { ++g_VRLookAtSkipped; return; }

    // LOGGED ON THE ACQUISITION EDGE, not on the change of hand -- and that distinction cost a
    // session. While nothing is acquired the hand swaps EVERY FRAME by design, so a budget of lines
    // spent on hand changes is gone in a third of a second, all of it before the player has pointed
    // at anything. What matters is the moment the answer flips: that is when the ray locks, and it
    // is the only line that can say whether the lock works.
    {
        static int s_logged = 0;
        static int s_last = -1;
        const int acq = LastRayAcquiredFor(hand) ? 1 : 0;
        if (acq != s_last && s_logged < 20) {
            s_last = acq;
            ++s_logged;
            Log("LookAtRayFromHand: acquired=%d -> ray %s on the %s hand\n",
                acq, acq ? "LOCKS" : "alternates", right ? "right" : "left");
        }
    }

    // MODEL SPACE TO WORLD, the same bridge VRPalmWorldPos/VRPalmWorldRot use, and deliberately the
    // same arithmetic rather than a call into them: those take a script frame, and this runs on a
    // job thread with no frame in sight. Keeping the formula identical is what stops the hand ray
    // and the hand the player sees from drifting apart.
    const float camRot[4]  = { g_VRCamModelRot[0], g_VRCamModelRot[1], g_VRCamModelRot[2], g_VRCamModelRot[3] };
    const float viewRot[4] = { g_VRViewWorldRot[0], g_VRViewWorldRot[1], g_VRViewWorldRot[2], g_VRViewWorldRot[3] };
    float camInv[4]; VRIK_QuatConj(camRot, camInv);
    float m2w[4];    VRIK_QuatMul(viewRot, camInv, m2w); VRIK_QuatNorm(m2w);

    const volatile float* pm = right ? g_VRPalmModelR : g_VRPalmModelL;
    const float rel[3] = { pm[0] - g_VRCamModelPos[0],
                           pm[1] - g_VRCamModelPos[1],
                           pm[2] - g_VRCamModelPos[2] };
    float wpos[3]; VRIK_QuatRotateVec(m2w, rel, wpos);

    const volatile float* pr = right ? g_VRPalmModelRotR : g_VRPalmModelRotL;
    const float palm[4] = { pr[0], pr[1], pr[2], pr[3] };
    if (palm[0] == 0.0f && palm[1] == 0.0f && palm[2] == 0.0f && palm[3] == 0.0f) {
        ++g_VRLookAtSkipped;
        return;
    }
    // WHICH WAY IS "POINTING" ON THIS BONE. The engine reads a quaternion's forward as its +Y, and
    // the first field test came back with the ray leaving along the THUMB: on the palm bone, +Y is
    // not the finger axis. So the bone's rotation is corrected in its OWN frame -- post-multiplied,
    // not pre -- by the rotation that brings +Y onto the axis the fingers actually run along.
    //
    // The correction is a table rather than a derivation because the answer is a fact about CDPR's
    // rig, not about geometry: the five candidates are the five other axes, and one look in the
    // headset settles it in a way no amount of reasoning here can.
    static const float kAxisFix[6][4] = {
        { 0.0f, 0.0f, -0.70710678f, 0.70710678f },  // 0: +X  -- -90 deg about Z maps +Y onto +X
        { 0.0f, 0.0f,  0.70710678f, 0.70710678f },  // 1: -X  -- +90 about Z
        { 0.0f, 0.0f,  0.0f,        1.0f        },  // 2: +Y  -- identity, the raw bone forward
        { 1.0f, 0.0f,  0.0f,        0.0f        },  // 3: -Y  -- 180 about X
        { 0.70710678f, 0.0f, 0.0f,  0.70710678f },  // 4: +Z  -- +90 about X maps +Y onto +Z
        {-0.70710678f, 0.0f, 0.0f,  0.70710678f },  // 5: -Z  -- -90 about X
    };
    int axis = g_liveControls.xrHandInteractAxis;
    if (axis < 0 || axis > 5) axis = 0;
    // THE LEFT HAND IS MIRRORED, and the correction has to be mirrored with it. The axis above was
    // calibrated in the headset on the RIGHT palm; with a weapon drawn the ray moves to the left one
    // and came out pointing BACKWARDS -- the same axis, the opposite way, which is what a mirrored
    // skeleton does to a bone's local frame. The table is laid out in signed pairs (+X,-X,+Y,-Y,
    // +Z,-Z), so flipping the low bit is exactly "the same axis, other direction" and needs no
    // second calibration. It is derived from the right-hand answer rather than stored separately on
    // purpose: two settings that must always disagree by a sign are two settings that will one day
    // be set to agree.
    if (!right) axis ^= 1;

    float aimed[4]; VRIK_QuatMul(palm, kAxisFix[axis], aimed); VRIK_QuatNorm(aimed);
    float wrot[4]; VRIK_QuatMul(m2w, aimed, wrot); VRIK_QuatNorm(wrot);

    xf->px = g_VRViewWorldPos[0] + wpos[0];
    xf->py = g_VRViewWorldPos[1] + wpos[1];
    xf->pz = g_VRViewWorldPos[2] + wpos[2];
    // pw is left as the engine set it: it is the Vector4 w of a position, and the engine's own
    // value is the one the rest of the pipeline expects.
    xf->qi = wrot[0];
    xf->qj = wrot[1];
    xf->qk = wrot[2];
    xf->qr = wrot[3];

    // ONCE, and only once: the first ray that is actually moved, with the numbers in it. The
    // counters say whether the hook runs; this says whether what it wrote is a pose or nonsense, and
    // a wrong quaternion looks exactly like a working hook until something is aimed at.
    static bool logged = false;
    if (!logged) {
        logged = true;
        Log("LookAtRayFromHand first patch: hand=%s pos=(%.3f,%.3f,%.3f) quat=(%.3f,%.3f,%.3f,%.3f)\n",
            right ? "right" : "left", xf->px, xf->py, xf->pz, xf->qi, xf->qj, xf->qk, xf->qr);
    }

    ++g_VRLookAtPatched;
}

bool InstallLookAtRayFromHandHook() {
    // The site, anchored eight bytes before the injection point so the pattern includes the virtual
    // call itself and cannot drift onto a different `mov rax,[rbp-0x50]`. Verified unique across the
    // whole 2.31 image -- one hit, at RVA 0x3F90E9. No wildcards are needed: every byte is either an
    // opcode or a frame-relative displacement, and there is not a single rip-relative or call-rel32
    // operand in the run.
    //
    //   4C 8D 45 20              lea  r8,[rbp+0x20]
    //   49 8B C9                 mov  rcx,r9
    //   48 8D 55 B0              lea  rdx,[rbp-0x50]
    //   41 FF D2                 call r10                  <- vtbl[0x228], writes the transform
    //   48 8B 45 B0              mov  rax,[rbp-0x50]       <- injection point, found + 14
    //   48 8D 54 24 50           lea  rdx,[rsp+0x50]
    const char* pattern =
        "\x4C\x8D\x45\x20\x49\x8B\xC9\x48\x8D\x55\xB0\x41\xFF\xD2\x48\x8B\x45\xB0\x48\x8D\x54\x24\x50";
    const char* mask = "xxxxxxxxxxxxxxxxxxxxxxx";

    uint8_t* found = static_cast<uint8_t*>(FindPattern("Cyberpunk2077.exe", pattern, mask));
    if (!found) return false;

    uint8_t* site = found + 14;          // the `mov rax,[rbp-0x50]` right after the call returns
    constexpr int replaceLen = 9;        // that mov (4) + the lea that follows it (5)

    void* tramp = AllocateTrampoline(site, 128);
    if (!tramp) return false;

    uint8_t* code = static_cast<uint8_t*>(tramp);
    int pos = 0;

    // lea rcx,[rbp-0x50]   -- the transform the call just wrote
    code[pos++] = 0x48; code[pos++] = 0x8D; code[pos++] = 0x4D; code[pos++] = 0xB0;
    // mov rdx,rbx          -- the targeting user state, held in rbx across the whole function
    code[pos++] = 0x48; code[pos++] = 0x8B; code[pos++] = 0xD3;
    // sub rsp,0x20 -- shadow space, AND NOT 0x28, which is the version of this line that crashed.
    //
    // 0x28 is the right number when a trampoline is entered by a CALL: the return address is already
    // on the stack, rsp is 8 past alignment, and 0x28 puts it back. This one is entered by a JMP, so
    // rsp arrives exactly as the engine left it -- 16-byte aligned, because the instruction we
    // displaced sits immediately after the engine's own `call r10` and a call site is aligned by the
    // ABI. Subtracting 0x28 from an aligned rsp misaligns OUR call by 8, and the callback then faults
    // on the first movaps MSVC emits against a 16-byte-aligned local. 0x20 is a multiple of 16, so
    // alignment is preserved and the shadow space is still there.
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xEC; code[pos++] = 0x20;
    WriteMovRaxImm64(code, pos, reinterpret_cast<uintptr_t>(&CyberpunkVR_LookAtRayFromHand));
    code[pos++] = 0xFF; code[pos++] = 0xD0;                       // call rax
    // add rsp,0x20 -- BEFORE the displaced lea, which is rsp-relative
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xC4; code[pos++] = 0x20;

    // the two displaced instructions, now reading a transform that holds the hand
    code[pos++] = 0x48; code[pos++] = 0x8B; code[pos++] = 0x45; code[pos++] = 0xB0;  // mov rax,[rbp-0x50]
    code[pos++] = 0x48; code[pos++] = 0x8D; code[pos++] = 0x54; code[pos++] = 0x24; code[pos++] = 0x50; // lea rdx,[rsp+0x50]

    code[pos++] = 0xE9;
    *reinterpret_cast<int32_t*>(code + pos) = static_cast<int32_t>((site + replaceLen) - (code + pos + 4));
    pos += 4;

    DWORD oldProtect;
    VirtualProtect(site, replaceLen, PAGE_EXECUTE_READWRITE, &oldProtect);
    site[0] = 0xE9;
    *reinterpret_cast<int32_t*>(site + 1) = static_cast<int32_t>(code - (site + 5));
    for (int i = 5; i < replaceLen; ++i) site[i] = 0x90;
    VirtualProtect(site, replaceLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, replaceLen);

    Log("LookAtRayFromHand hook installed at %p (pattern %p, trampoline %p)\n",
        site, found, tramp);
    return true;
}

// Installed always, idle until the Controls-tab checkbox is ticked. Installing an idle hook is the
// deliberate choice: a hook installed only when a setting turns on would run its pattern scan in the
// middle of a session, and a scan that fails there fails with nobody reading the log.
CVR_HOOK("LookAtRayFromHand", ::cvr::hooks::Stage::Boot, 80, InstallLookAtRayFromHandHook);
