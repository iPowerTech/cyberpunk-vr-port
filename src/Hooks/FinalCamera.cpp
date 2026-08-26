// FinalCamera -- one hook, one file.
//
// Reads back the quaternion the engine is about to render with and finds it in the write
// ring, which identifies the frame's pose exactly -- no assumption about how far ahead the
// engine renders. It is a LEAF, not part of the cycle: nothing the other two read is
// written here. It sat in the knot only because the ring and the seqlock quaternion are not
// plain loads, and those now live behind Camera/CameraLink.hpp.
//
// INSTALL ORDER IS EXACTLY WHAT IT WAS: Locate 10, Patch 12, Final 14, all in Stage::Boot. An
// adversarial pass over the plan for this split proposed reordering Patch before Locate for a
// "single-meaning" startup signal, and the payoff does not exist: g_engineCamQuatValid is set only
// when camKind == 1, which needs ClassifyPatchCameraOwner to have self-calibrated its name offset,
// which cannot happen until MAIN's placed component passes the site. The fallback stays an ordinary
// startup transient in BOTH orders. The two windows being weighed are the gap between consecutive
// FindPattern calls inside one function -- microseconds. An unobservable change is not a safe
// change; it is an unfalsifiable one, so the order is preserved.

#include "Camera/CameraLink.hpp"
#include "Camera/CameraState.hpp"
#include "Utils/LogThrottle.hpp"
#include "Core/LiveControls.hpp"
#include "Core/Telemetry.hpp"
#include "Core/VrCoreShared.hpp"
#include "Hooks/Hook.hpp"
#include "Hooks/Trampoline.hpp"
#include "Runtimes/OpenXRManager.hpp"
#include "Utils/AobScanner.hpp"
#include "Utils/MemorySafe.hpp"

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace {

// THE VIEWPOINT THE EYE GOES BACK TO, AND WHY IT CANNOT BE THE CAMERA'S OWN.
//
// Pinning the taken-over view to MAIN's patched position was not enough. Activating a device drags
// MAIN in as well: measured with the fallback engaged at a terminal, the scene camera sat 31.5 cm
// BEYOND MAIN, and the zoom was still plainly there -- because the thing being pinned to had moved
// too. In VR that zoom parks the head centimetres from the screen, which is not a distance anything
// can be read from.
//
// So the anchor is taken off the camera chain entirely: the player ENTITY's world position (pushed
// from Lua, shared [96..98]) plus the camera-to-entity offset as it stood BEFORE the divergence
// began, plus the live head translation so leaning still works.
//
// The offset comes out of a short ring rather than the frame of the take-over. A device camera
// slides for several frames before it is far enough away to be noticed, and those frames are
// exactly the ones that must not be baked into the anchor; the oldest sample in the ring is ~0.4 s
// old, which predates the whole slide.
//
// Sampled on MAIN views only, which is one logical stream, so plain members are enough here -- the
// same reason the muzzle latch above is a plain static.
class SceneCamAnchor {
public:
    // `armed` is the fallback state for this frame. While it is false the ring keeps filling; on
    // the edge into true the anchor is latched and then held for as long as the take-over lasts.
    void Update(bool armed) {
        if (!armed) {
            m_latched = false;
            CyberpunkVR_SceneCamAnchorValid = 0;
            const float* sh = GetShotShared();
            if (!sh || sh[131] == 0.0f || !CyberpunkVR_PatchedCamPosValid) return;
            const uint32_t slot = m_next % kRing;
            for (int i = 0; i < 3; ++i) {
                m_ring[slot][i] = FixedToM(CyberpunkVR_PatchedCamPosFP[i]) - HeadDeltaM(i) - sh[96 + i];
            }
            ++m_next;
            if (m_filled < kRing) ++m_filled;
            return;
        }
        if (!m_latched) {
            if (m_filled == 0) return;
            const uint32_t oldest = (m_filled < kRing) ? 0u : (m_next % kRing);
            for (int i = 0; i < 3; ++i) m_anchor[i] = m_ring[oldest][i];
            m_latched = true;
        }
        // PUBLISHED FOR THE PLACE THAT CAN ACTUALLY MOVE THE EYE.
        //
        // This site fixes what the eye points at and cannot fix where it is: with the render camera
        // pinned here, both eyes still zoomed in, because the view producer had taken the origin
        // long before. LocateCamera is upstream of the producer, the components and the render
        // camera alike, so that is where the anchor is applied -- and this is where it is known.
        //
        // WITHOUT the head translation: that buffer carries the camera before PatchCamera adds the
        // delta, and handing it a position that already contains one would count it twice.
        const float* sh = GetShotShared();
        if (sh && sh[131] != 0.0f) {
            for (int i = 0; i < 3; ++i) {
                CyberpunkVR_SceneCamAnchorFP[i] =
                    static_cast<int32_t>((sh[96 + i] + m_anchor[i]) * 131072.0f);
            }
            CyberpunkVR_SceneCamAnchorValid = 1;
        }
    }

    // Fixed point, same units as the render camera's own position.
    bool Resolve(int32_t* outFP) const {
        if (!m_latched) return false;
        const float* sh = GetShotShared();
        if (!sh || sh[131] == 0.0f) return false;
        for (int i = 0; i < 3; ++i) {
            outFP[i] = static_cast<int32_t>((sh[96 + i] + m_anchor[i] + HeadDeltaM(i)) * 131072.0f);
        }
        return true;
    }

private:
    static float FixedToM(int32_t v) { return static_cast<float>(v) / 131072.0f; }
    static float HeadDeltaM(int i) {
        return static_cast<float>(g_headDeltaFP[i].load(std::memory_order_relaxed)) / 131072.0f;
    }

    static constexpr uint32_t kRing = 32;   // ~0.44 s at 72 Hz
    float    m_ring[kRing][3] = {};
    float    m_anchor[3] = {};
    uint32_t m_next = 0;
    uint32_t m_filled = 0;
    bool     m_latched = false;
};

SceneCamAnchor g_sceneCamAnchor;

// Angle between two unit quaternions in degrees, sign-insensitive: q and -q are the same rotation
// and the engine is free to hand back either.
float QuatSeparationDeg(const float* a, const float* b) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0.0f) dot = -dot;
    if (dot > 1.0f) dot = 1.0f;
    return 2.0f * acosf(dot) * 57.29577951f;
}

// IS THE ENGINE STILL RENDERING FROM THE CAMERA WE WRITE?
//
// Evaluated on MAIN alone. VRCAM's render camera IS the RTT component PatchCamera writes, so it
// cannot diverge -- that is exactly why that eye kept following the head at a terminal while this
// one did not -- and sampling it would only halve the rate at which the question gets asked.
//
// Hysteresis in FRAMES, not in time: a camera blend crosses the threshold on its way in and on its
// way out, and a run counter costs nothing while a timer would need a clock this callback does not
// otherwise read.
bool FinalCameraFallbackArmed(const float* rsiPtr, const float* hq, bool haveHead, bool isMain) {
    static std::atomic<uint32_t> s_on{ 0 };
    static std::atomic<uint32_t> s_run{ 0 };

    // THE MEASUREMENT IS NOT THE ACTION, and separating them is what lets the answer be used by a
    // site that is nowhere near here. What this decides -- "a camera the port does not own is
    // holding the view" -- is the condition the LOCATE write needs to know about, and that write is
    // the one that works. The take-over below it is a separate, disabled, experiment.
    if (!CyberpunkVR_SceneCamDetect) {
        s_on.store(0, std::memory_order_relaxed);
        s_run.store(0, std::memory_order_relaxed);
        CyberpunkVR_DebugCamFallbackActive = 0;
        g_sceneCamObj.store(0, std::memory_order_relaxed);
        return false;
    }

    if (isMain && haveHead) {
        float camq[4] = {};
        if (ReadFloatArraySafe(rsiPtr + 4, camq, 4) && IsPlausibleUnitQuaternion(camq)) {
            const float deg = QuatSeparationDeg(camq, hq);
            CyberpunkVR_DebugCamFallbackDeg = deg;

            // PUBLISHED SO THE COMPONENT CAN BE RECOGNISED UPSTREAM.
            //
            // This is the pose the engine is rendering with, whichever camera it came from. At
            // PatchCamera every camera component passes by and none of them says which one is
            // active; matching against this is what names it, and naming it is what lets the
            // correction be applied early enough for the lighting to be built with it.
            const int32_t* camPos = reinterpret_cast<const int32_t*>(rsiPtr);
            for (int i = 0; i < 4; ++i) CyberpunkVR_RenderCamQuat[i] = camq[i];
            for (int i = 0; i < 3; ++i)
                CyberpunkVR_RenderCamPos[i] = static_cast<float>(camPos[i]) / 131072.0f;
            CyberpunkVR_RenderCamValid = 1;

            // HOW FAR THE VIEW HAS DRIFTED FROM THE PLAYER, which is the half that arrives first.
            // A device camera slides in before it turns, so the angle is still small while the
            // whole zoom is happening -- and it is the zoom that has to be caught, not the sit.
            float cm = 0.0f;
            if (CyberpunkVR_PatchedCamPosValid) {
                const int32_t* camPos = reinterpret_cast<const int32_t*>(rsiPtr);
                double sum = 0.0;
                for (int i = 0; i < 3; ++i) {
                    const double d = (static_cast<double>(camPos[i]) -
                                      static_cast<double>(CyberpunkVR_PatchedCamPosFP[i])) / 131072.0;
                    sum += d * d;
                }
                cm = static_cast<float>(sqrt(sum) * 100.0);
                CyberpunkVR_DebugCamFallbackCm = cm;
            }

            const bool on = s_on.load(std::memory_order_relaxed) != 0;
            const float degLimit = static_cast<float>(on ? CyberpunkVR_CamFinalFallbackExitDeg
                                                         : CyberpunkVR_CamFinalFallbackEnterDeg);
            const float cmLimit  = static_cast<float>(on ? CyberpunkVR_CamFinalFallbackExitCm
                                                        : CyberpunkVR_CamFinalFallbackEnterCm);
            // EITHER is enough to engage, and BOTH have to fall back before it lets go: a camera
            // that has turned away but not moved, and one that has moved but not turned, are the
            // same failure seen from two sides.
            const bool want = (deg > degLimit) ||
                              (CyberpunkVR_PatchedCamPosValid && cm > cmLimit);
            if (want == on) {
                s_run.store(0, std::memory_order_relaxed);
            } else {
                int need = CyberpunkVR_CamFinalFallbackFrames;
                if (need < 1) need = 1;
                if (static_cast<int>(s_run.fetch_add(1, std::memory_order_relaxed) + 1) >= need) {
                    s_run.store(0, std::memory_order_relaxed);
                    s_on.store(want ? 1u : 0u, std::memory_order_relaxed);
                    if (want) ++CyberpunkVR_DebugCamFallbackEnters;
                }
            }
        }
    }

    const bool armed = s_on.load(std::memory_order_relaxed) != 0;
    CyberpunkVR_DebugCamFallbackActive = armed ? 1 : 0;
    // The adoption lasts exactly as long as the reason for it. Letting a scene camera stay adopted
    // after the interaction ends would keep writing the head into a component the game has moved on
    // from, which is the same class of mistake as never having noticed it in the first place.
    if (!armed) g_sceneCamObj.store(0, std::memory_order_relaxed);
    if (armed) ++CyberpunkVR_DebugCamFallbackViews;
    return armed;
}

}   // namespace

extern "C" void __fastcall OnFinalCameraCallback(float* rsiPtr) {
    g_finalCameraHits++;
    if (g_telemetry) {
        g_telemetry->finalHits = static_cast<uint32_t>(g_finalCameraHits);
        g_telemetry->finalRsi = reinterpret_cast<uintptr_t>(rsiPtr);
    }

    const uint32_t locateSeq = g_lastLocateSeq;
    if (locateSeq != 0) {
        g_renderedSeq = locateSeq;
    }

    if (!rsiPtr || reinterpret_cast<uintptr_t>(rsiPtr) < 0x10000) return;

    // ---- READ BACK THE POSE THIS FRAME IS ACTUALLY BEING BUILT WITH ---------------------------
    //
    // Read-only, and it runs whatever the write path is set to -- it is a measurement of the
    // engine, not a modification of it. rsiPtr is the render camera + 0x70, so rsiPtr + 4 floats
    // is the camera quaternion at object+0x80 (verified live: for
    // q = (-0.112416, 0.204406, -0.710105, 0.664354) the basis rows at +0xC0 matched R(q) with
    // the Y/Z columns exchanged, to 1e-5). That quaternion is the one we composed and wrote into
    // the placed component, carried through the view producer, so finding it in the write ring
    // tells us exactly which XR sample is in this frame.
    //
    // MAIN only: both views carry the same orientation, so taking both would push two entries
    // for one presented frame and the queue would run at double rate. For the barrel packet the
    // same restriction is what keeps VRCAM from overwriting MAIN's render camera -- the overlay is
    // drawn into MAIN's backbuffer and the second eye is handed the finished NDC separately.
    //
    // THE GATE MOVED INWARD (dabinn, TofuExpress 821e8a4e): the quaternion read is now shared by two
    // consumers, the pose read-back and the barrel packet, and only the first of them is what
    // CyberpunkVR_PoseReadBack is about.
    if (CyberpunkVR_StereoModuleLoaded) {
        const bool isVrcam = CyberpunkVR_IsVrcamViewActive() != 0;
        const bool isMain  = !isVrcam && CyberpunkVR_IsMainViewActive() != 0;
        if (isMain) {
            float camq[4] = {};
            if (ReadFloatArraySafe(rsiPtr + 4, camq, 4) && IsPlausibleUnitQuaternion(camq)) {
                // LATCHED, and for the reason the overlay already latched it on its own side: with
                // no weapon the muzzle quaternion is identity and the publisher then sends its +Y as
                // exactly (0,1,0) -- not a barrel direction, and it used to drag the aim point
                // behind the camera. Anything that is exactly that default is not an answer, so the
                // last real direction is kept. The latch lives here now so the direction and the
                // quaternion leave as ONE packet.
                static float s_muzzleFwd[3] = {};
                if (float* sh = GetShotShared(); sh && sh[27] >= 0.5f) {
                    const float x = sh[24], y = sh[25], z = sh[26];
                    const bool identityDefault = (x == 0.0f && y == 1.0f && z == 0.0f);
                    if (!identityDefault && x * x + y * y + z * z > 0.25f) {
                        s_muzzleFwd[0] = x; s_muzzleFwd[1] = y; s_muzzleFwd[2] = z;
                    }
                }
                if (s_muzzleFwd[0] * s_muzzleFwd[0] +
                    s_muzzleFwd[1] * s_muzzleFwd[1] +
                    s_muzzleFwd[2] * s_muzzleFwd[2] > 0.25f) {
                    cvr::camera::BarrelFrame bf{};
                    bf.camQuat[0] = camq[0]; bf.camQuat[1] = camq[1];
                    bf.camQuat[2] = camq[2]; bf.camQuat[3] = camq[3];
                    bf.muzzleFwd[0] = s_muzzleFwd[0];
                    bf.muzzleFwd[1] = s_muzzleFwd[1];
                    bf.muzzleFwd[2] = s_muzzleFwd[2];
                    cvr::camera::BarrelFramePublish(bf);
                }

                if (CyberpunkVR_PoseReadBack) {
                    OpenXRHeadPose matched{};
                    uint32_t age = 0, ties = 0;
                    if (cvr::camera::CamWriteRecordFind(camq, &matched, &age, &ties)) {
                        CyberpunkVR_DebugFinalAge  = age;
                        CyberpunkVR_DebugFinalTies = ties;
                        if (ties > 1) ++CyberpunkVR_DebugFinalTieHits;
                        ++CyberpunkVR_DebugFinalMatch;
                        OpenXRManager::Get().PushRenderedFramePose(matched);
                    } else {
                        ++CyberpunkVR_DebugFinalNoMatch;
                    }
                }
            }
        }
    }

    // ---- MONO: THE PER-VIEW WRITE SITE -------------------------------------------------------
    //
    // This callback sits inside CRenderNode_PrepareSceneRendering (sub_140784ABC ->
    // sub_1407854C0), the third node of the FIRST pipeline stage, and it runs ONCE PER VIEW.
    // Measured live: consecutive hits alternate between exactly two camera objects, A -> B -> A,
    // sharing one vtable, positions 23 micrometres apart (i.e. the same viewpoint -- there is no
    // eye separation in these objects at all, so writing here owns both eyes outright).
    //
    // And it is the same object the view-matrix bake reads: a breakpoint on
    // `mov rsi,[r14+18h]` inside sub_140788A9C (CRenderNode_SetStreamlineConstants, stage 8)
    // returned one of those two pointers. So a write here is seen by the matrix bake AND by
    // everything between -- which is why frame-open is the right place and stage 8 is not.
    //
    // `rsiPtr` is the component + 0x70: int32 fixed-point position at [0..2], and the rotation
    // rows the bake consumes at component +0xC0 == rsiPtr + 20 floats, which is exactly what
    // ApplyFinalCameraOrientationFromQuat already writes. The machinery was here all along; it
    // was simply gated behind AER.
    //
    // DEFAULT OFF as a blanket setting -- the pose-binding work in this same build has to be
    // measurable on its own first, and two changes at once tell you nothing. Flip
    // CyberpunkVR_CamWriteInFinal live to compare paths.
    //
    // What IS on by default is the fallback below, which uses this same machinery for the one case
    // where the component path provably cannot work: the engine rendering from a camera of its own.
    {
        // ASK THE DISPATCHER, DO NOT HASH A NAME.
        //
        // The first attempt compared the view key against CyberpunkVR_VrcamCamNameHash(), and
        // VRCAM matched exactly zero times out of 8395 MAIN hits. That hash is the COMPONENT name
        // ("vrcam_2560x2560") used to classify owners at PatchCamera; the view key is the hash of
        // the CAMERA name ("vrcam_feed_2560x2560"). Different strings, so the test could never
        // fire -- MAIN moved to this path while VRCAM stayed on the old one, and two views driven
        // by two different mechanisms is what made the right eye judder.
        //
        // These two accessors are the ones the whole VRCAM capture pipeline already runs on, so
        // they are proven against the live dispatcher rather than reconstructed from a name.
        const bool isVrcam = CyberpunkVR_IsVrcamViewActive() != 0;
        const bool isMain  = !isVrcam && CyberpunkVR_IsMainViewActive() != 0;
        const bool isEye   = isMain || isVrcam;

        float hq[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const bool haveHead = g_headQuatValid != 0 &&
                              cvr::camera::CamWriteQuatRead(hq) &&
                              IsPlausibleUnitQuaternion(hq);

        // THE TWO WAYS IN, AND THEY ARE NOT THE SAME THING.
        //
        // CyberpunkVR_CamWriteInFinal takes every view on every frame -- the experiment this block
        // was written for, still off by default and still the thing to flip when comparing paths.
        // The fallback takes only the frames where the component path has DEMONSTRABLY stopped
        // working: the render camera's own quaternion has parted company with the head. It engages
        // at a terminal, a device, a scene camera; it reads 0.11 deg and stays out of the way for
        // all of ordinary play.
        const bool detected = FinalCameraFallbackArmed(rsiPtr, hq, haveHead, isMain);
        if (isMain) g_sceneCamAnchor.Update(detected);
        // Detected says what is true; the flag says whether to act on it here. The locate write acts
        // on the same fact somewhere else, and better.
        const bool armed = detected && CyberpunkVR_CamFinalFallback;
        const bool fallbackOnly = armed && !CyberpunkVR_CamWriteInFinal;
        if (!CyberpunkVR_CamWriteInFinal && !armed) return;

        // VRCAM NEEDS NOTHING FROM HERE, and giving it something is what broke it before.
        //
        // Its render camera IS the component PatchCamera writes -- proven at the terminal, where
        // that eye alone kept following the head, and again by forcing its fov: the change came out
        // in that eye and nowhere else. Writing it a second time from this site is the two-mechanism
        // arrangement whose symptom is judder on the turn, plus an IPD applied twice.
        if (fallbackOnly && isVrcam) return;

        // AND NEITHER DOES THE LIGHTING.
        //
        // The blanket mode below hands the head orientation to EVERY view in the image on purpose --
        // shadow cascades, reflections, the distant/imposter pass -- because there it drives both
        // eyes and those views have to agree with them. The fallback is not that: it corrects ONE
        // eye that a scene camera has taken, and the shadow views were never wrong. Giving them the
        // head anyway turns them with it, which is what "the lighting rotates when I turn my head"
        // was -- reported in the taken-over eye, at a terminal, and nowhere else.
        if (fallbackOnly && !isEye && !CyberpunkVR_CamFinalFallbackAllViews) return;

        // EVERY VIEW IN THE IMAGE, not just the two eye views.
        //
        // Restricting the write to MAIN/VRCAM produced a very specific symptom: near geometry
        // stayed world-locked while distant geometry and shadows dragged with the head. That is
        // what it looks like when one part of the frame is rendered from the VR camera and the
        // rest from the engine's own -- the untouched views are composited into an image whose
        // main view has already turned, so their content appears to counter-rotate.
        //
        // These other views (distant/imposter, reflection, shadow) each carry their own key at
        // ctx+0x28 and sail straight through a two-key gate. They belong to the same eye and the
        // same instant, so they need the same orientation. Only the lateral IPD term is withheld
        // from them -- that one is per-eye, and a view whose eye we cannot name must not get it.
        if (isEye) {
            if (isMain) ++CyberpunkVR_DebugViewCamMain; else ++CyberpunkVR_DebugViewCamVrcam;
        } else {
            ++CyberpunkVR_DebugViewCamOther;
            if (CyberpunkVR_CamFinalViewScope == 0) return;
        }

        if (!haveHead) return;

        // One orientation for every view -- it is one head. The eyes differ by the lateral IPD
        // term and nothing else, the same rule all three reference mods keep.
        WriteRenderCameraBasis(rsiPtr, hq);

        // AND PUT THE EYE BACK ON THE HEAD.
        //
        // Orientation alone is half a fix: the scene camera this view was taken over from sits
        // wherever the game put it -- 0.45 m from the other eye, measured at a terminal -- and two
        // eyes half a metre apart are not a head, whichever way they both point. The other eye is
        // already at the player's viewpoint (VRCAM takes MAIN's base, see PatchCamera), so this one
        // goes to the same place and the IPD below separates the pair about it.
        //
        // Only in the fallback, and only for MAIN: the full-on path above is an unrelated
        // experiment and must keep behaving exactly as it did.
        if (fallbackOnly && isMain && CyberpunkVR_CamFinalFallbackPos) {
            int32_t want[3] = {};
            bool have = g_sceneCamAnchor.Resolve(want);
            if (!have && CyberpunkVR_PatchedCamPosValid) {
                // No entity feed (no CET, or Lua has not pushed yet): the patched camera is a worse
                // anchor -- a device drags it too -- but it still holds the two eyes together, which
                // is the part that cannot be left undone.
                for (int i = 0; i < 3; ++i) want[i] = CyberpunkVR_PatchedCamPosFP[i];
                have = true;
            }
            if (have) {
                // A DEVICE CAMERA IS NEAR, A CINEMATIC IS ANYWHERE.
                //
                // A terminal parks the view within arm's reach of the player; a scene that flies the
                // camera across the room is not a fault to be corrected, it is the shot. Past the
                // limit, keep the orientation -- so the head still looks around, which is what VR
                // wants from a cutscene -- and leave the viewpoint where the scene put it.
                const int32_t* camPos = reinterpret_cast<const int32_t*>(rsiPtr);
                double sum = 0.0;
                for (int i = 0; i < 3; ++i) {
                    const double d = (static_cast<double>(camPos[i]) -
                                      static_cast<double>(want[i])) / 131072.0;
                    sum += d * d;
                }
                const float cm = static_cast<float>(sqrt(sum) * 100.0);
                CyberpunkVR_DebugCamAnchorCm = cm;
                if (CyberpunkVR_CamFinalFallbackMaxCm <= 0 ||
                    cm < static_cast<float>(CyberpunkVR_CamFinalFallbackMaxCm)) {
                    int32_t* posFP = reinterpret_cast<int32_t*>(rsiPtr);
                    for (int i = 0; i < 3; ++i) posFP[i] = want[i];
                }
            }
        }

        const float half = isEye ? GetDesiredHalfIpd() : 0.0f;
        if (half != 0.0f) {
            float right[3] = {};
            ComputeRightVectorFromQuaternion(hq, right);
            if (IsPlausibleUnitVector3(right)) {
                const float sign = isVrcam ? +1.0f : -1.0f;   // MAIN = left, VRCAM = right
                int32_t* posFP = reinterpret_cast<int32_t*>(rsiPtr);
                for (int i = 0; i < 3; ++i) {
                    posFP[i] += static_cast<int32_t>(right[i] * half * sign * 131072.0f);
                }
            }
        }
        return;
    }

    // THE LATE IPD SHIFT IS GONE, AND IT WAS UNREACHABLE BEFORE IT WAS DELETED.
    //
    // It lived here: finalPos = g_lastLocatePosFP + g_lastIpdShiftFP, with the shift computed in
    // LocateCamera and the eye chosen by the PARITY of the locate counter. That is the AER shape --
    // one camera alternating eyes, so the offset had to flip per frame and had to be applied below
    // the view producer or consecutive frames would cull against different viewpoints.
    //
    // Two separate reasons it is not the arrangement any more:
    //
    //   * REACHABILITY. Every path through the block above returns: the gate on the way in
    //     (`if (!CamWriteInFinal && !armed) return;`), the per-view filters, and the `return;` at
    //     the end of the write path. So this code could not execute at any setting of those flags
    //     -- it was dead in every direction, and the eye parity it used would in any case be wrong
    //     now that two cameras exist at once.
    //   * CORRECTNESS. With two real cameras the per-view offset is CONSTANT, so it belongs ABOVE
    //     the producer, in the component world position (CyberpunkVR_IpdInWorldPos, PatchCamera):
    //     there the distant/imposter pass, the shadow cascades, the reflections and the motion
    //     vectors are all built from the eye they are drawn for. Applied here they would not be.
    //
    // What remains of this hook is the read-back above, which is the one thing only this stage can
    // do: identify which written pose the frame is actually being drawn with.

    if (!g_verboseLog || (g_finalCameraHits % 600) != 1) return;

    float values[24] = {};
    float cameraMtx[16] = {};
    float cameraViewA[12] = {};
    float cameraViewB[16] = {};
    if (!ReadFloatArraySafe(rsiPtr, values, 24)) return;
    ReadFloatArraySafe(rsiPtr + 20, cameraMtx, 16);   // +0x50
    ReadFloatArraySafe(rsiPtr + 68, cameraViewA, 12); // +0x110
    ReadFloatArraySafe(rsiPtr + 204, cameraViewB, 16); // +0x330

    const int32_t* finalPosFP = reinterpret_cast<const int32_t*>(rsiPtr);
    const float posScale = 1.0f / 131072.0f;   // WorldPosition = 17 fractional bits

    Log("FinalCamera probe: hit=%llu rsi=%p eye=%d f40=%.6f f44=%.6f pos=(%.3f, %.3f, %.3f) locateSeq=%u locQ=(%.3f, %.3f, %.3f, %.3f)\n",
        static_cast<unsigned long long>(g_finalCameraHits),
        rsiPtr,
        OpenXRManager::Get().GetCurrentRenderEyeIndex(),
        values[16],
        values[17],
        static_cast<float>(finalPosFP[0]) * posScale,
        static_cast<float>(finalPosFP[1]) * posScale,
        static_cast<float>(finalPosFP[2]) * posScale,
        locateSeq,
        g_lastLocateQuat[0], g_lastLocateQuat[1], g_lastLocateQuat[2], g_lastLocateQuat[3]);

    if (LooksProjectionLike(values, 16)) {
        LogMatrix4x4("FinalCamera matrix candidate:", values);
    } else {
        Log("FinalCamera raw[0..15]: %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f\n",
            values[0], values[1], values[2], values[3],
            values[4], values[5], values[6], values[7],
            values[8], values[9], values[10], values[11],
            values[12], values[13], values[14], values[15]);
    }

    Log("FinalCamera mtx+0x50: %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f\n",
        cameraMtx[0], cameraMtx[1], cameraMtx[2], cameraMtx[3],
        cameraMtx[4], cameraMtx[5], cameraMtx[6], cameraMtx[7],
        cameraMtx[8], cameraMtx[9], cameraMtx[10], cameraMtx[11],
        cameraMtx[12], cameraMtx[13], cameraMtx[14], cameraMtx[15]);
    Log("FinalCamera view+0x110: %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f\n",
        cameraViewA[0], cameraViewA[1], cameraViewA[2], cameraViewA[3],
        cameraViewA[4], cameraViewA[5], cameraViewA[6], cameraViewA[7],
        cameraViewA[8], cameraViewA[9], cameraViewA[10], cameraViewA[11]);
    Log("FinalCamera view+0x330: %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f  %.6f %.6f %.6f %.6f\n",
        cameraViewB[0], cameraViewB[1], cameraViewB[2], cameraViewB[3],
        cameraViewB[4], cameraViewB[5], cameraViewB[6], cameraViewB[7],
        cameraViewB[8], cameraViewB[9], cameraViewB[10], cameraViewB[11],
        cameraViewB[12], cameraViewB[13], cameraViewB[14], cameraViewB[15]);
}

bool InstallFinalCameraHook() {
    const char* pattern = "\xF3\x44\x0F\x5E\x4E\x40\x49\x81\xC0\x20\x0F\x00\x00";
    const char* mask = "xxxxxxxxxxxxx";
    uint8_t* found = static_cast<uint8_t*>(FindPattern("Cyberpunk2077.exe", pattern, mask));
    if (!found) return false;

    constexpr int replaceLen = 13; 
    void* tramp = AllocateTrampoline(found, 512);
    if (!tramp) return false;

    uint8_t* code = static_cast<uint8_t*>(tramp);
    int pos = 0;

    // --- CALL C++ CALLBACK ---
    code[pos++] = 0x9C; // pushfq
    code[pos++] = 0x50; // push rax
    code[pos++] = 0x51; // push rcx
    code[pos++] = 0x52; // push rdx
    code[pos++] = 0x41; code[pos++] = 0x50; // push r8
    code[pos++] = 0x41; code[pos++] = 0x51; // push r9
    code[pos++] = 0x41; code[pos++] = 0x52; // push r10
    code[pos++] = 0x41; code[pos++] = 0x53; // push r11
    code[pos++] = 0x55; // push rbp

    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xEC; code[pos++] = 0x40; // sub rsp, 40h
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x04; code[pos++] = 0x24; // movups [rsp+00h], xmm0
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x4C; code[pos++] = 0x24; code[pos++] = 0x10; // movups [rsp+10h], xmm1
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x54; code[pos++] = 0x24; code[pos++] = 0x20; // movups [rsp+20h], xmm2
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x5C; code[pos++] = 0x24; code[pos++] = 0x30; // movups [rsp+30h], xmm3

    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xE5; // mov rbp, rsp
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xE4; code[pos++] = 0xF0; // and rsp, -16
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xEC; code[pos++] = 0x20; // sub rsp, 20h

    // Set arg1 (rcx) = rsi
    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xF1; // mov rcx, rsi

    WriteMovRaxImm64(code, pos, reinterpret_cast<uintptr_t>(OnFinalCameraCallback));
    code[pos++] = 0xFF; code[pos++] = 0xD0; // call rax

    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xEC; // mov rsp, rbp

    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x04; code[pos++] = 0x24; // movups xmm0, [rsp+00h]
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x4C; code[pos++] = 0x24; code[pos++] = 0x10; // movups xmm1, [rsp+10h]
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x54; code[pos++] = 0x24; code[pos++] = 0x20; // movups xmm2, [rsp+20h]
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x5C; code[pos++] = 0x24; code[pos++] = 0x30; // movups xmm3, [rsp+30h]
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xC4; code[pos++] = 0x40; // add rsp, 40h

    code[pos++] = 0x5D; // pop rbp
    code[pos++] = 0x41; code[pos++] = 0x5B; // pop r11
    code[pos++] = 0x41; code[pos++] = 0x5A; // pop r10
    code[pos++] = 0x41; code[pos++] = 0x59; // pop r9
    code[pos++] = 0x41; code[pos++] = 0x58; // pop r8
    code[pos++] = 0x5A; // pop rdx
    code[pos++] = 0x59; // pop rcx
    code[pos++] = 0x58; // pop rax
    code[pos++] = 0x9D; // popfq

    // Original instructions:
    // divss xmm9, [rsi+40h]
    code[pos++] = 0xF3; code[pos++] = 0x44; code[pos++] = 0x0F; code[pos++] = 0x5E; code[pos++] = 0x4E; code[pos++] = 0x40;
    // add r8, 0F20h
    code[pos++] = 0x49; code[pos++] = 0x81; code[pos++] = 0xC0; code[pos++] = 0x20; code[pos++] = 0x0F; code[pos++] = 0x00; code[pos++] = 0x00;

    // jmp back
    code[pos++] = 0xE9;
    *reinterpret_cast<int32_t*>(code + pos) = static_cast<int32_t>((found + replaceLen) - (code + pos + 4));
    pos += 4;

    DWORD oldProtect;
    VirtualProtect(found, replaceLen, PAGE_EXECUTE_READWRITE, &oldProtect);
    found[0] = 0xE9;
    *reinterpret_cast<int32_t*>(found + 1) = static_cast<int32_t>(code - (found + 5));
    for (int i = 5; i < replaceLen; ++i) found[i] = 0x90;
    VirtualProtect(found, replaceLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), found, replaceLen);
    return true;
}

CVR_HOOK("FinalCamera", ::cvr::hooks::Stage::Boot, 14, InstallFinalCameraHook);
