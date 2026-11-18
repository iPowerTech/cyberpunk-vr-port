// Haptics -- see Runtimes/Haptics.hpp for the shape of it.
//
// WHY THE FRAME THREAD PLAYS EVERYTHING. A round is reported from whichever worker ran the shot, a
// block from the script thread, a belt touch from CET. xrApplyHapticFeedback is not documented as
// needing external synchronisation, but it is an action call, and this port's runtimes (Virtual
// Desktop, SteamVR, Link) only ever see action calls from the thread that syncs them. One frame of
// latency on a tap is below what a hand can tell.
//
// WHY ENVELOPES AND NOT FREQUENCIES. The controllers this port runs on answer frequency very
// differently -- Index takes it, most Touch controllers resonate in one band and go weak outside it --
// so the frequency stays XR_FREQUENCY_UNSPECIFIED and the weapons differ in strength, length and
// decay instead, which every motor can render.
//
// ONE ENVELOPE PER HAND. A new event replaces what is playing unless that has a higher priority and
// is still running: full-auto restarts the rifle tap every round, and a belt tick never cuts a
// shot's tail. The engine's beats (Runtimes/EngineHaptics.hpp) never interrupt a pulse or an envelope.

#include "Runtimes/Haptics.hpp"
#include "Anim/LeftHanded.hpp"
#include "Anim/WheelGrab.hpp"
#include "Runtimes/HapticRouting.hpp"
#include "Runtimes/Psvr2HapticChannel.hpp"
#include "Runtimes/LegacyHapticPolicy.hpp"
#include "Runtimes/EngineHaptics.hpp"
#include "Core/VrCoreShared.hpp"

#include "Runtimes/OpenXRManager.hpp"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <mutex>

extern "C" {
// Master switch (ini xr_haptics) and the strength of everything (ini xr_haptic_scale, 0..2).
__declspec(dllexport) int   CyberpunkVR_Haptics = 1;
__declspec(dllexport) float CyberpunkVR_HapticScale = 1.0f;
// THE SECOND HAND IN A TWO-HANDED GRIP feels the round as the trigger hand does: in 2H the weapon is
// in both hands, held in one it is in one (2026-10-17). This is its share; 1 = the same.
__declspec(dllexport) float CyberpunkVR_HapticSupportMul = 1.0f;
// THE PROFILES, live-tunable: per profile three segments of (amplitude 0..1, milliseconds); an
// amplitude of 0 is a gap. Rows: 0 handgun, 1 rifle/SMG/LMG, 2 shotgun, 3 sniper/precision,
// 4 belt touch, 5 melee block, 6 melee parry, 7 bullet block, 8 bullet deflect, 9 grenade pin (the
// pulling hand), 10 grenade pin (the holding hand), 11 VR menu click, 12 VR menu hover, 13 VR menu
// slider step, 14 melee hit, 15 melee swing, 16 hand interaction armed, 17 hand interaction press, 18 wheel horn,
// 19 stick click threshold, 20 magazine seated, 21 round chambered (the slide or the pump going home), 22 the hand's
// physics body stopped by something it struck.
// A segment under ~12 ms can fall between two XR frames and never play, so none is shorter.
__declspec(dllexport) float CyberpunkVR_HapticProfiles[23][6] = {
    { 0.85f, 28.0f,  0.25f, 30.0f,  0.00f,  0.0f },   // handgun: one crisp snap
    { 0.60f, 22.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // rifle: short, so full-auto stays a rattle of rounds
    { 1.00f, 55.0f,  0.55f, 70.0f,  0.20f, 60.0f },   // shotgun: a heavy thump that rolls off
    { 1.00f, 45.0f,  0.60f, 60.0f,  0.25f, 90.0f },   // sniper: the hardest kick and the longest tail
    { 0.30f, 14.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // belt: a light tick
    { 0.90f, 40.0f,  0.40f, 50.0f,  0.00f,  0.0f },   // melee block: a dull thud
    { 1.00f, 20.0f,  0.00f, 35.0f,  0.75f, 25.0f },   // melee parry: a double "clang"
    { 0.65f, 30.0f,  0.20f, 30.0f,  0.00f,  0.0f },   // bullet block: a short knock
    { 1.00f, 15.0f,  0.45f, 25.0f,  0.00f,  0.0f },   // bullet deflect: a sharp ping
    { 1.00f, 18.0f,  0.35f, 30.0f,  0.00f,  0.0f },   // grenade pin: the snap of the ring coming free
    { 0.45f, 25.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // grenade pin, felt through the grenade
    { 0.80f, 16.0f,  0.00f, 12.0f,  0.35f, 12.0f },   // menu click: a press and its release
    { 0.15f, 12.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // menu hover: a faint tick on entering a control
    { 0.22f, 12.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // menu slider: one tick per step crossed
    { 1.00f, 35.0f,  0.50f, 55.0f,  0.00f,  0.0f },   // melee hit: the blow landing, then its weight
    { 0.50f, 45.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // melee swing: the weapon cutting the air
    { 0.35f, 15.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // interact armed: light, says which hand would act
    { 0.75f, 45.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // interact press: firmer, the panel took it
    { 0.55f, 30.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // horn: the gesture was read before the sound comes
    { 0.70f, 40.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // stick threshold: one firm pulse, three in a row past the limit
    { 0.80f, 18.0f,  0.25f, 22.0f,  0.00f,  0.0f },   // magazine seated: the latch clicking in
    { 0.90f, 16.0f,  0.30f, 24.0f,  0.00f,  0.0f },   // round chambered: the slide or pump slamming home
    { 0.50f, 18.0f,  0.00f,  0.0f,  0.00f,  0.0f },   // hand contact: one tap, the stop the eye sees is felt
};
// The off hand is on the weapon (src/Anim/TwoHandGrip.cpp).
__declspec(dllexport) extern int CyberpunkVR_TwoHandActive;
extern float CyberpunkVR_WeaponKickDeg;
// [0] events accepted, [1] segments applied left, [2] right, [3] last xrApplyHapticFeedback result,
// [4] last profile row, [5] dropped (switched off, no action, no session), [6] shots, [7] events.
__declspec(dllexport) int   CyberpunkVR_HapticStats[8] = {};
}

namespace cvr::haptics {
namespace {

constexpr int kSegs = 3;
constexpr int kProfileCount = 23;

struct Envelope {
    float   amp[kSegs] = {};
    int64_t start[kSegs] = {};   // QPC
    int64_t end[kSegs] = {};
    int     next = kSegs;        // first segment not yet started; kSegs = idle
    int     priority = 0;
    int64_t done = 0;            // QPC when the last segment ends
    bool    lazy = false;        // fork: timed from the Tick that first plays it, not from when it was asked
};

std::mutex g_lock;
Envelope   g_hand[2];            // 0 left, 1 right
PulseRequest g_pulse[2];
float g_engineLoad = 0;          // the revs as a fraction of the limit, as last published
float g_engineSmooth = 0;        // ...smoothed on the frame thread
uint64_t g_vehicleMs = 0;        // when the revs were last published (GetTickCount64)
int64_t g_engineNextBeat = 0;    // QPC of the next engine beat; 0 = on the next frame a hand is on the wheel
int64_t g_engineLastTick = 0;
std::atomic<float> g_legacyGain{1.25f};
std::atomic<float> g_vehicleGain{0.0f};   // engine haptics are off until switched on in F10

int64_t Qpc() { LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return c.QuadPart; }
int64_t QpcFreq() {
    static const int64_t f = [] { LARGE_INTEGER q{}; QueryPerformanceFrequency(&q); return q.QuadPart; }();
    return f;
}
int64_t MsToQpc(float ms) { return static_cast<int64_t>(static_cast<double>(ms) * 0.001 * static_cast<double>(QpcFreq())); }

int PriorityOf(int row) {
    switch (row) {
        case 4: case 11: case 12: case 13: case 15: case 16: case 17: case 18: case 19: case 22:
            return 1;   // belt, menu, a swing, hand, horn, stick, a hand's contact
        case 0: case 1: case 2: case 3: return 2;   // shots
        default: return 3;           // block / parry / deflect / pin / magazine / chamber
    }
}

void Queue(int row, int handMask, float mul) {
    if (row < 0 || row >= kProfileCount) return;
    if (!CyberpunkVR_Haptics) { ++CyberpunkVR_HapticStats[5]; return; }
    const float scale = SafeGain(CyberpunkVR_HapticScale) * mul;
    if (scale <= 0.0f) return;
    const float* p = CyberpunkVR_HapticProfiles[row];
    const int64_t now = Qpc();
    const int pri = PriorityOf(row);
    std::lock_guard lock(g_lock);
    for (int h = 0; h < 2; ++h) {
        if (!(handMask & (h == 0 ? kLeft : kRight))) continue;
        Envelope& e = g_hand[h];
        if (now < e.done && e.priority > pri) continue;
        Envelope n{};
        int64_t t = now;
        for (int s = 0; s < kSegs; ++s) {
            const float ms = std::clamp(p[s * 2 + 1], 0.0f, 500.0f);
            n.amp[s] = std::clamp(p[s * 2] * scale, 0.0f, 1.0f);
            n.start[s] = t;
            n.end[s] = t + MsToQpc(ms);
            t = n.end[s];
        }
        n.next = 0;
        n.priority = pri;
        n.done = t;
        // Fork: a hand's contact is asked for by the physics thread at any moment between two XR frames, and Tick plays a
        // segment only for what is left of it (none under 3 ms): an 18 ms tap asked for just after a Tick could be cut to
        // a few ms or skipped. Timed from the Tick that plays it, it is always whole.
        n.lazy = row == 22;
        e = n;
    }
    ++CyberpunkVR_HapticStats[0];
    CyberpunkVR_HapticStats[4] = row;
}

}  // namespace

void SetLegacyGain(float gain) { g_legacyGain.store(SafeGain(gain, 1.25f)); }
float LegacyGain() { return g_legacyGain.load(); }
void SetVehicleGain(float gain) { g_vehicleGain.store(SafeGain(gain, 0.0f)); }
float VehicleGain() { return g_vehicleGain.load(); }

void Pulse(int hand, float amplitude, int durationMs) {
    if (hand < 0 || hand > 1 || !CyberpunkVR_Haptics) return;
    std::lock_guard lock(g_lock);
    g_pulse[hand].Merge(amplitude, durationMs, GetTickCount64());
}

void SetVehicleEngine(float rpm, float rpmMax) {
    std::lock_guard lock(g_lock);
    g_engineLoad = EngineLoad(rpm, rpmMax);
    g_vehicleMs = GetTickCount64();
}

void Clear() {
    std::lock_guard lock(g_lock);
    for (int hand = 0; hand < 2; ++hand) { g_hand[hand] = {}; g_pulse[hand] = {}; }
    g_engineLoad = g_engineSmooth = 0;
    g_vehicleMs = 0;
    g_engineNextBeat = g_engineLastTick = 0;
}

void Shot(int weaponClass, bool supportHandOnGun, int hand, float kick, bool dual) {
    if (weaponClass < 1 || weaponClass > 4 || hand < 0 || hand > 1) return;
    if (!(kick > 0.0f)) kick = CyberpunkVR_WeaponKickDeg;
    if (HapticRoute().Toolkit()) {
        // EDS owns single-gun recoil. Actual per-hand shots own dual-wield grip
        // impacts, independently of adaptive-trigger profile selection/timers.
        if (dual && CyberpunkVR_Haptics && OpenXRManager::Get().IsSessionRunning()) {
            const auto pulse = DualToolkitShot(kick);
            const float amp = std::min(1.0f, pulse.amplitude * SafeGain(CyberpunkVR_HapticScale));
            if (amp > 0) cvr::psvr2::Haptics().Pulse(hand, amp, pulse.durationMs);
        }
        return;
    }
    // OPENXR KEEPS THE PROFILE PER WEAPON CLASS (fork): the pistol snaps, the shotgun thumps and rolls off.
    // Upstream's single pulse sized from the recoil kick (LegacyShot) was tried and judged less accurate.
    // While dual wielding each gun is felt only in the hand that fires it.
    ++CyberpunkVR_HapticStats[6];
    const int row = weaponClass - 1;
    const int trigger = (hand == 0) ? kLeft : kRight;
    Queue(row, trigger, 1.0f);
    if (!dual && supportHandOnGun) {
        Queue(row, (trigger == kLeft) ? kRight : kLeft, std::clamp(CyberpunkVR_HapticSupportMul, 0.0f, 1.0f));
    }
}

void Play(int event, int handMask) {
    int row = -1;
    switch (event) {
        case kBeltTouch:     row = 4; break;
        case kGrenadePin:    row = 9; break;
        case kGrenadePinHold: row = 10; break;
        case kMeleeBlock:    row = 5; break;
        case kMeleeParry:    row = 6; break;
        case kBulletBlock:   row = 7; break;
        case kBulletDeflect: row = 8; break;
        case kUiClick:       row = 11; break;
        case kUiHover:       row = 12; break;
        case kUiStep:        row = 13; break;
        case kMeleeHit:      row = 14; break;
        case kMeleeSwing:    row = 15; break;
        case kInteractArm:   row = 16; break;
        case kInteractPress: row = 17; break;
        case kWheelHorn:     row = 18; break;
        case kStickThreshold: row = 19; break;
        case kMagSeat:       row = 20; break;
        case kChamber:       row = 21; break;
        case kHandContact:   row = 22; break;
        default: return;
    }
    // A BLOCK IS TAKEN BY BOTH HANDS when both are on the weapon -- the same test the two-handed melee
    // damage uses (CyberpunkVR_TwoHandActive) -- so the support hand feels it too: the left one, or the right
    // one with the weapon in the left hand.
    if (((row >= 5 && row <= 8) || row == 14 || row == 15) && CyberpunkVR_TwoHandActive != 0)
        handMask |= cvr::anim::WeaponInLeftHand() ? kRight : kLeft;
    ++CyberpunkVR_HapticStats[7];
    Queue(row, handMask, 1.0f);
}

void Tick() {
    if (!CyberpunkVR_Haptics || SafeGain(CyberpunkVR_HapticScale) <= 0) { Clear(); return; }
    struct Due { float amp; float sec; };
    Due due[2] = {};
    {
        std::lock_guard lock(g_lock);
        const int64_t now = Qpc();
        const uint64_t nowMs = GetTickCount64();
        const float gain = LegacyGain() * SafeGain(CyberpunkVR_HapticScale);
        // THE ENGINE, felt only in a hand that is on the wheel or the bars (the wheel grab's blend), and only
        // while the revs are fresh. One beat schedule for both hands; a hand that lands on the wheel starts it.
        const bool engineOn = VehicleGain() > 0 &&
            VehicleFresh(g_isDriving.load(std::memory_order_relaxed), nowMs, g_vehicleMs);
        const bool onWheel[2] = {cvr::anim::g_wheelBlendLeft.load() > .01f, cvr::anim::g_wheelBlendRight.load() > .01f};
        bool engineBeat = false;
        EngineBeat beat;
        if (engineOn && (onWheel[0] || onWheel[1])) {
            // ~80 ms to follow the revs: a stepped rev counter must not jump the rhythm from one beat to the next
            const double dt = g_engineLastTick ? static_cast<double>(now - g_engineLastTick) / static_cast<double>(QpcFreq()) : 0.0;
            const float a = dt > 0 ? static_cast<float>(1.0 - std::exp(-std::min(dt, 0.25) / 0.08)) : 1.0f;
            g_engineSmooth += a * (g_engineLoad - g_engineSmooth);
            beat = EngineBeatFor(g_engineSmooth);
            if (now >= g_engineNextBeat) {
                engineBeat = true;
                g_engineNextBeat = now + MsToQpc(beat.periodSec * 1000.0f);
            }
        } else {
            g_engineNextBeat = 0;
            if (!engineOn) g_engineSmooth = 0;
        }
        g_engineLastTick = now;
        const float engineAmp = std::min(1.0f, beat.amplitude * VehicleGain() * SafeGain(CyberpunkVR_HapticScale));
        for (int h = 0; h < 2; ++h) {
            Envelope& e = g_hand[h];
            const auto pulse = g_pulse[h];
            g_pulse[h] = {};
            // A block/parry takes precedence. Merge simultaneous swings/impacts,
            // and keep their lifetime intact instead of overwriting with engine rumble.
            if (pulse.Fresh(nowMs) && gain > 0 && !(now < e.done && e.priority > 2)) {
                e = {};
                e.amp[0] = std::min(1.0f, pulse.amplitude * gain);
                e.start[0] = now;
                e.end[0] = now + MsToQpc(static_cast<float>(pulse.durationMs));
                e.start[1] = e.end[1] = e.start[2] = e.end[2] = e.end[0];
                e.next = 0; e.priority = 2; e.done = e.end[0];
                ++CyberpunkVR_HapticStats[0];
            }
            if (e.lazy && e.next == 0) {
                const int64_t late = now - e.start[0];
                if (late > 0) {
                    for (int s = 0; s < kSegs; ++s) { e.start[s] += late; e.end[s] += late; }
                    e.done += late;
                }
                e.lazy = false;
            }
            int started = -1;
            while (e.next < kSegs && e.start[e.next] <= now) started = e.next++;
            if (started >= 0 && e.amp[started] > 0) {
                const double left = static_cast<double>(e.end[started] - now) / static_cast<double>(QpcFreq());
                if (left > 0.003) due[h] = { e.amp[started], static_cast<float>(left) };
            }
            if (engineBeat && onWheel[h] && now >= e.done && engineAmp > .01f)
                due[h] = {engineAmp, beat.pulseSec};
        }
    }
    // Use the selected actuator owner, including OpenXR fallback on PSVR2.
    const bool psvr2 = HapticRoute().Toolkit();
    for (int h = 0; h < 2; ++h) {
        if (due[h].amp <= 0.0f) continue;
        if (psvr2) {
            cvr::psvr2::Haptics().Pulse(h, due[h].amp, std::clamp(static_cast<int>(due[h].sec * 1000.0f + 0.5f), 1, 1000));
            ++CyberpunkVR_HapticStats[h == 0 ? 1 : 2];
            continue;
        }
        const int r = OpenXRManager::Get().ApplyHaptic(h, due[h].amp, due[h].sec);
        CyberpunkVR_HapticStats[3] = r;
        if (r == 0) ++CyberpunkVR_HapticStats[h == 0 ? 1 : 2];
        else ++CyberpunkVR_HapticStats[5];
    }
}

}  // namespace cvr::haptics
