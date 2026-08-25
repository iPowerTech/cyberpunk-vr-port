// Recoil that reaches the HANDS -- the kick the weapon gives the shooter, not the camera.
//
// The game already kicks the camera (an additive spine animation, base\animations\weapon\firearms\*,
// `add_camera_recoil_single_shot_*`). It never reaches the arms in VR for a structural reason: VRIK
// writes the wrist and the hand rotation from the controller every solve, so whatever the animation
// system does to those bones is overwritten in the same frame. The weapon, being parented to the
// hand, does not move either. So the kick has to be added where the arm is decided -- to the IK
// TARGET, before the solve -- or it does not exist at all.
//
// THE IMPULSE LIVES IN THE HAND'S OWN FRAME, not the world's and not the weapon's. Back is -Y of the
// hand and rise is a rotation about its right axis, so the same numbers work for a pistol, a rifle,
// either hand, any grip, and no part of this needs to know what is being held or where the muzzle is.
//
// The motion is a damped spring given a velocity impulse, which is what a shoulder actually does: the
// hand leaves fast, comes back, and settles. A pure decay (x *= k each frame) reads as a soft push
// because it has no overshoot and no return time of its own -- the whole character of recoil is in
// how it comes BACK.
//
//     x'' = -w^2 x - 2*zeta*w x'          w = 2*pi / returnTime, zeta = 0.55 (a little overshoot)
//
// Everything is integrated per solve with the real elapsed time, so the feel does not change with
// frame rate -- a rate-dependent decay was the first version and it made recoil weaker the faster the
// machine ran.

#include "Anim/CharacterRig.hpp"
#include "Camera/CameraState.hpp"

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>

// ---- controls -----------------------------------------------------------------------------------
//
// Four numbers, and each one is a thing you can feel: how far the hand is thrown back, how far the
// muzzle rises, how long it takes to come home, and how much of it the second hand takes.
extern "C" __declspec(dllexport) int   CyberpunkVR_HandRecoil          = 1;
// ZERO BY DEFAULT, and that is the correction that matters.
//
// Throwing the wrist backwards is what a free-floating gun does, not what a held one does: the
// player's hand is on a controller and does not move, so pulling the IK target off it breaks the one
// rule this whole rig is built on -- the hand IS the controller. Reported from the headset as "it
// jerks the arm somehow, and that is wrong, my hand is steady", which is exactly what a translated
// wrist looks like from the inside.
//
// What a held weapon actually does with the impulse is ROTATE: the muzzle flips up about the wrist
// while the hand stays where it is being held. That is the whole of it below, and the weapon follows
// because it is parented to the hand bone. The travel stays as a slider for anyone who wants a looser
// grip, and it is the first thing to try if the rotation alone reads as too light.
extern "C" __declspec(dllexport) float CyberpunkVR_HandRecoilBackCm    = 0.0f;   // along -hand forward
// THE PEAK ANGLE OF ONE SHOT, and it is computed rather than chosen -- 22 deg is what a 9 mm pistol
// held at the wrist actually produces:
//
//     J  = m_b*v_b + m_powder*v_gas = 0.008*360 + 0.00033*540      = 3.06 N s
//     L  = J * h,  h = 0.07 m from the bore axis down to the wrist = 0.214 N m s
//     I  = 1.0*0.08^2 + 0.004 + 0.4*0.05^2                         = 0.0114 kg m^2
//     w0 = L / I                                                   = 18.8 rad/s
//     peak = 0.523 * w0 / w_spring,  w_spring = 2*pi/0.25 s        = 0.39 rad = 22 deg
//
// AN EARLIER VERSION OF THIS COMMENT SAID 7.8 deg AND WAS WRONG: it used h = 0.03 m, which is the
// bore-to-top-of-hand distance, not the distance to the joint the flip actually turns about. The
// error made the recoil invisible in the headset, and the fix for that was briefly a taste number
// (15 deg) -- which is exactly the kind of thing this file is not supposed to contain. The moment arm
// is the physical quantity; the angle follows from it.
//
// This is also the number the per-weapon table will write: J and I are the only things that change
// between a Kenshin and an Overture, so each weapon gets its own peak from the same formula.
// 15.4 deg: the computed 22 taken down by 30% on the user's call after firing every weapon in the
// set. The physics above still holds -- it says what a 9 mm does to a free wrist -- and a hand that
// is braced for the shot, which a player's is, gives less than a free one.
extern "C" __declspec(dllexport) float CyberpunkVR_HandRecoilRiseDeg   = 15.4f;  // muzzle rise, 9 mm reference
extern "C" __declspec(dllexport) float CyberpunkVR_HandRecoilReturnMs  = 250.0f; // full settle
// THE OFF HAND'S SHARE IS GONE, and its absence is the answer rather than an oversight. It said how much
// of the kick a second hand takes when it is also on the weapon -- but a hand ON the weapon now rides the
// weapon: the support point is built from the already-kicked right hand, so the left one inherits exactly
// the motion the gun makes, about the gun's axis rather than its own. Sampling the spring for it again
// was the double kick that tore the hand off the grip. And a hand that is NOT on the weapon feels nothing
// at all. That leaves no case for a fraction, so there is no fraction.

// THE EQUIPPED WEAPON'S OWN KICK, in the game's degrees, published on each draw by the weapon module
// (SetVRWeaponKick). Zero means "unknown weapon" and the reference angle is used unscaled.
//
// The reference is the Lexington at 1.0, because 22 deg on it is the number that was tried in the
// headset and kept. Everything else follows its own ratio: an Overture at 4.0 flips four times as far,
// a Kappa at 0.24 barely moves -- which is the impulse ratio, not a taste ladder.
extern "C" __declspec(dllexport) float CyberpunkVR_WeaponKickDeg = 0.0f;
// The two-hand grip (src/Anim/TwoHandGrip.cpp): its state, and what it leaves of the kick.
extern "C" __declspec(dllexport) extern int   CyberpunkVR_TwoHandActive;
extern "C" __declspec(dllexport) extern float CyberpunkVR_TwoHandRecoil;
extern "C" __declspec(dllexport) float CyberpunkVR_HandRecoilRefKick   = 1.0f;   // the Lexington
extern "C" __declspec(dllexport) float CyberpunkVR_HandRecoilMaxDeg    = 28.0f;  // a hand has limits
// HOW LONG THE HEAVY ONES TAKE TO COME BACK. The settle time of a hand holding a weapon is the period
// of its own spring, 2*pi/sqrt(k/I) -- it grows with the inertia, so a magnum has to come home slower
// than a smart pistol. The weapon's kick stands in for that inertia, mildly (a power of 0.35), which
// spreads a 250 ms reference across roughly 180..400 ms.
//
// THE GAME'S OWN RecoilRecoveryTime WAS READ FIRST AND REJECTED, which is worth writing down: it is a
// gameplay pacing number, not a physical one, and it runs the wrong way -- Kenshin 0.08 s, Kappa 0.30,
// Overture 0.15, Liberty up to 0.80. Taking it would have made the heaviest revolver settle faster
// than the lightest smart pistol.
extern "C" __declspec(dllexport) float CyberpunkVR_HandRecoilReturnPow  = 0.35f;
extern "C" __declspec(dllexport) float CyberpunkVR_HandRecoilReturnMinMs = 180.0f;
extern "C" __declspec(dllexport) float CyberpunkVR_HandRecoilReturnMaxMs = 420.0f;
// 40, set from the headset: the ratio keeps a revolver clearly heavier than a 9 mm, and past this
// the wrist reads as broken rather than kicked. It bites on the top four -- Silverhand, Liberty,
// Unity, Omaha, Nue and the Overture all land here, so those six are told apart by their recovery
// rather than by their peak.
// Readable live: how many shots the pose path has answered, and the current displacement.
extern "C" __declspec(dllexport) uint64_t CyberpunkVR_DebugRecoilShots = 0;
extern "C" __declspec(dllexport) float    CyberpunkVR_DebugRecoilCm    = 0.0f;
// Advances every fresh solve. Zero here means the pose path never reached the spring at all.
extern "C" __declspec(dllexport) uint64_t CyberpunkVR_DebugRecoilTicks = 0;
// What the SOLVE actually applied to the right hand this frame, degrees of muzzle rise. The spring
// can be perfect and still reach nothing -- everything between the sample and the arm solve is
// downstream, and this is the only number that sees it.
extern "C" __declspec(dllexport) float CyberpunkVR_DebugRecoilAppliedDeg = 0.0f;
// Max-hold of the same number; write 0 to it to re-arm.
extern "C" __declspec(dllexport) float CyberpunkVR_DebugRecoilPeakDeg = 0.0f;

namespace {

double g_lastShotMs = 0.0;

// The shot is signalled from the weapon-aim detour, which runs on a game thread; the spring is
// integrated in the pose hook, which runs on another. One counter crossing between them is the whole
// handover: the pose path compares it against what it last saw and converts the difference into
// impulses. A missed shot is then impossible, and a double-counted one is impossible too -- which a
// bool flag could not promise, because two shots inside one animation frame would collapse into one.
std::atomic<uint64_t> g_shotSeq{0};

struct Spring {
    float x = 0.0f;   // displacement, 0..1 of the configured amplitude
    float v = 0.0f;   // and its rate, per second
};
Spring g_spring[2];
uint64_t g_seenSeq = 0;
uint64_t g_pendingShots = 0;
double g_lastMs = 0.0;

double NowMs() {
    static LARGE_INTEGER f = {};
    if (f.QuadPart == 0) QueryPerformanceFrequency(&f);
    if (f.QuadPart == 0) return 0.0;
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

}  // namespace

// Called from the shot bracket in src/Hooks/WeaponAim.cpp, once per round that actually leaves the
// barrel -- so burst and full-auto stack by construction, exactly as a real one does.
extern "C" void RecoilOnShot() {
    // ONE ROUND, ONE IMPULSE -- and the site cannot promise that on its own.
    //
    // The provider slot is called several times per round (measured: 42 calls for 8 rounds fired), and
    // the muzzle sequence only separates FRAMES, so a round whose calls straddle two frames still
    // counted twice -- 14 impulses for those same 8 rounds. A refractory window is what actually
    // matches a round: 40 ms is longer than the 2-3 frames one round's calls span at 72 fps, and
    // shorter than the gap between rounds even at 1500 rpm.
    static double s_lastMs = 0.0;
    const double now = NowMs();
    if (s_lastMs > 0.0 && (now - s_lastMs) < 40.0) return;
    s_lastMs = now;
    g_lastShotMs = now;
    g_shotSeq.fetch_add(1, std::memory_order_release);
}

// Advance both springs to now. Called once per fresh solve, before either arm is built.
extern "C" void RecoilTick() {
    ++CyberpunkVR_DebugRecoilTicks;
    if (!CyberpunkVR_HandRecoil) {
        g_spring[0] = Spring{};
        g_spring[1] = Spring{};
        CyberpunkVR_DebugRecoilCm = 0.0f;
        return;
    }
    const double now = NowMs();
    float dt = (g_lastMs > 0.0) ? static_cast<float>((now - g_lastMs) * 0.001) : 0.0f;
    g_lastMs = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;          // a hitch or a pause must not launch the spring

    const uint64_t seq = g_shotSeq.load(std::memory_order_acquire);
    if (seq != g_seenSeq) {
        const uint64_t shots = seq - g_seenSeq;
        g_seenSeq = seq;
        CyberpunkVR_DebugRecoilShots += shots;
        // Velocity, not position: a position step teleports the hand and reads as a glitch, while a
        // velocity impulse is a throw the spring then has to catch. Stacked shots add velocity, so a
        // held trigger climbs -- which is the behaviour, not a bug.
        //
        // THE SIZE IS DERIVED, NOT PICKED. With x(0)=0 and x'(0)=v0 this spring peaks at
        //     x_peak = 0.523 * v0 / w        (zeta = 0.55)
        // so one round reaching exactly the configured amplitude means v0 = 1.914*w, and the numbers
        // in the controls then mean what they say. A flat 6.0 sat here first and measured 0.34 cm of
        // travel against 3.5 asked for -- eleven times short, in a way no slider could have found,
        // because the error was in the units and not in the value.
        g_pendingShots += shots;
    }

    // Per-weapon settle: the reference return time scaled by the weapon's own kick, so the six that
    // share the 40 deg ceiling are still told apart -- by how long they take to come home rather than
    // by how far they go.
    float ret = CyberpunkVR_HandRecoilReturnMs;
    if (CyberpunkVR_WeaponKickDeg > 0.0f && CyberpunkVR_HandRecoilRefKick > 0.01f) {
        const float r = CyberpunkVR_WeaponKickDeg / CyberpunkVR_HandRecoilRefKick;
        ret *= std::pow(r, CyberpunkVR_HandRecoilReturnPow);
        if (ret < CyberpunkVR_HandRecoilReturnMinMs) ret = CyberpunkVR_HandRecoilReturnMinMs;
        if (ret > CyberpunkVR_HandRecoilReturnMaxMs) ret = CyberpunkVR_HandRecoilReturnMaxMs;
    }
    if (ret < 20.0f) ret = 20.0f;
    const float w = 6.28318531f / (ret * 0.001f);
    const float zeta = 0.55f;
    // The impulse is issued HERE, with the same w the spring is about to be integrated with -- the peak
    // is 0.523*v0/w, so any other w would break the promise that the configured angle is the angle.
    if (g_pendingShots) {
        g_spring[0].v += 1.914f * w * static_cast<float>(g_pendingShots);
        g_spring[1].v += 1.914f * w * static_cast<float>(g_pendingShots);
        g_pendingShots = 0;
    }
    // FIXED 2 ms SUBSTEPS, because a frame is far too coarse for this spring. Integrated once per
    // frame at 72 Hz the scheme reaches only a fraction of the analytic peak, and the fraction depends
    // on the stiffness: 0.53 at a 180 ms return, 0.67 at 250, 0.79 at 406. That silently compressed the
    // per-weapon ladder -- every light weapon lost half its kick while a heavy one kept four fifths --
    // and it made "the configured angle is the angle" false. At 2 ms the same weapons land at
    // 0.93 / 0.95 / 0.97, and the ladder is the one the numbers describe.
    int steps = static_cast<int>(dt / 0.002f) + 1;
    if (steps > 64) steps = 64;
    const float sdt = dt / static_cast<float>(steps);
    for (int s = 0; s < 2; ++s) {
        Spring& sp = g_spring[s];
        for (int i = 0; i < steps; ++i) {
            const float a = -w * w * sp.x - 2.0f * zeta * w * sp.v;
            sp.v += a * sdt;
            sp.x += sp.v * sdt;
        }
        if (std::fabs(sp.x) < 1e-5f && std::fabs(sp.v) < 1e-4f) { sp.x = 0.0f; sp.v = 0.0f; }
    }
    CyberpunkVR_DebugRecoilCm = g_spring[0].x * CyberpunkVR_HandRecoilBackCm;
}

// The displacement for one hand, in that hand's own frame: metres back along its forward axis, and
// radians of rise about its right axis. Only the hand that HOLDS the weapon is ever given anything:
// see the note on the removed share above.
extern "C" void RecoilSample(int side, int weaponHand, float* outBackM, float* outRiseRad) {
    *outBackM = 0.0f;
    *outRiseRad = 0.0f;
    if (!CyberpunkVR_HandRecoil || side < 0 || side > 1) return;
    // A HAND THAT IS NOT ON THE WEAPON FEELS NOTHING. It was being kicked anyway, every shot, whether or
    // not it was holding anything -- reported from the headset exactly that way. The two-hand grip is what
    // decides, because it is the only thing that puts this hand on the gun; and when it does, the kick
    // arrives through the weapon rather than from here (see AnimPose's left branch).
    if (!weaponHand && !CyberpunkVR_TwoHandActive) return;
    float rise = CyberpunkVR_HandRecoilRiseDeg;
    if (CyberpunkVR_WeaponKickDeg > 0.0f && CyberpunkVR_HandRecoilRefKick > 0.01f) {
        rise *= CyberpunkVR_WeaponKickDeg / CyberpunkVR_HandRecoilRefKick;
        // A KNEE, NOT A CLIFF. A hard clamp at the cap makes every heavy weapon the same weapon: at 40
        // deg the Silverhand, Liberty, Unity, Omaha, Nue and Overture all land on the ceiling and the
        // ratio that made this per-weapon is thrown away. Below the reference angle nothing is touched
        // -- the value tried in the headset stays exactly itself -- and above it the excess is
        // compressed toward the cap, which is also what a wrist does: it stiffens as the load grows, so
        // the response is sublinear at the top rather than clipped.
        const float knee = CyberpunkVR_HandRecoilRiseDeg;
        const float cap  = CyberpunkVR_HandRecoilMaxDeg;
        if (rise > knee && cap > knee) {
            rise = knee + (cap - knee) * std::tanh((rise - knee) / (cap - knee));
        }
        if (rise > cap) rise = cap;
    }
    // A SECOND HAND ON THE WEAPON TAKES MOST OF THE KICK. Two hands roughly triple the mass resisting
    // the same impulse and add a second lever against the muzzle rise, so what is left is a fifth of the
    // one-handed flip. Applied to the angle rather than to the impulse so the settle time is unchanged:
    // it is the same spring, held better.
    if (CyberpunkVR_TwoHandActive) rise *= CyberpunkVR_TwoHandRecoil;
    const float x = g_spring[side].x;
    *outBackM = x * CyberpunkVR_HandRecoilBackCm * 0.01f;
    float deg = x * rise;
    // THE CEILING HAS TO HOLD FOR STACKED SHOTS TOO. Rounds add velocity to the spring, so a burst drives
    // the envelope past 1 and the angle past the cap -- measured at 49.9 deg against a 40 deg ceiling. The
    // cap is a statement about a wrist, and a wrist does not bend further because the trigger was held.
    const float capDeg = CyberpunkVR_HandRecoilMaxDeg;
    if (deg >  capDeg) deg =  capDeg;
    if (deg < -capDeg) deg = -capDeg;
    *outRiseRad = deg * 0.01745329252f;
    if (side == 1) {
        const float deg = *outRiseRad * 57.2957795f;
        CyberpunkVR_DebugRecoilAppliedDeg = deg;
        // PEAK HELD, because sampling a 180 ms spring from outside the process means catching it in
        // the act -- three measurement windows in a row missed every shot simply because the trigger
        // was not being pulled during them. A latch the reader clears makes the question "did the
        // hand ever get the kick" answerable without standing next to the trigger.
        const float a = deg < 0.0f ? -deg : deg;
        if (a > CyberpunkVR_DebugRecoilPeakDeg) CyberpunkVR_DebugRecoilPeakDeg = a;
    }
}

// When the last round left, on the same clock the rest of this file uses. The heading hook reads it to
// ask a question it cannot answer alone: is the sideways camera jerk on a shot the GAME's own recoil
// arriving through the heading delta, or something of ours.
extern "C" double RecoilLastShotMs() { return g_lastShotMs; }
