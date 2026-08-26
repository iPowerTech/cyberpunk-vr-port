// SceneCamFov -- the field of view a device camera brings with it, refused for the view it holds.
//
// THE SYMPTOM. A terminal narrows the whole image. It reads as the camera being pushed towards the
// screen and was chased as one for a while; it is not, and the numbers say so -- measured live at
// the same terminal:
//
//     gameplay   view fov 98.06   proj m00 0.868   (= the headset's, forced by the port)
//     terminal   view fov 60.00   proj m00 1.732   (= cot(30 deg))
//
// 98 degrees of world squeezed into the same panel at 60 is a 1.6x magnification, which in VR is
// the difference between reading a screen and being unable to focus on it.
//
// WHERE THE NUMBER LIVES. sub_1404E4030 is a field-by-field copy of a camera setup, and the FOV is
// one of the fields: `mov eax,[rdx+0x38]` -> `mov [rcx+0x20],eax`. The destination is the setup the
// view is then built from: further down the same frame, `movss xmm3,[rbx+0x20]` hands exactly that
// float to the projection builder, alongside the aspect at +0x24. Verified by reading it in a
// debugger in ordinary play -- 98.06, our own value, sitting where the view will pick it up.
//
// WHY IT IS NOT WRITTEN ANYWHERE ELSE. Every later site was tried and each one failed with its own
// evidence: the view context's fov (ctx+0x90) accepts the value and changes nothing; the context's
// projection terms belong to the RTT view and only broke it; the zoom multiply the pattern list
// calls `Zoom` runs, counts, and leaves the terminal exactly as zoomed as before. This copy is the
// last point at which the number is still a number rather than a matrix.
//
// THE FILTER IS THE ASPECT, and getting that wrong is what made the first attempt at this only half
// work. The copy is generic: shadow cascades, reflections and imposter passes come through it too,
// and rewriting their FOV lights the scene wrong. The first version filtered by POSITION, which does
// not separate them at all -- a shadow camera stands at the player's viewpoint -- so it skipped some
// of the view's own setups and rewrote some of theirs, and the zoom came out halved instead of
// gone. The main view renders 1:1 (2160x2160); the other passes carry their own aspect, measured at
// 2.25 for one of them in the same session. So the aspect names the view, and nothing else has to.
//
// SCOPED TO THE MEASURED CONDITION. Only while a camera the port does not own is holding the view --
// see CyberpunkVR_SceneCamDetect. In ordinary play the field already holds our value and this hook
// has nothing to say.

#include "Camera/CameraState.hpp"
#include "Core/VrCoreShared.hpp"
#include "Stereo/DetourRegistry.hpp"
#include "Stereo/EngineRvas.hpp"

#include <windows.h>
#include <cmath>
#include <cstdint>

namespace cvr {
namespace detail {

// (dst, src, ...) -- we touch neither the copy nor its result, only the one field in the source it
// is about to read.
using CamSetupCopyFn = void* (__fastcall*)(void*, void*, void*);
static CamSetupCopyFn g_orig_cam_setup_copy = nullptr;

static void* __fastcall Detour_CamSetupCopy(void* dst, void* src, void* a3) {
    if (src && CyberpunkVR_SceneCamHoldFov && CyberpunkVR_DebugCamFallbackActive) {
        const float want = g_normalFovOverrideValue;
        if (want > 1.0f && want < 179.0f) {
            __try {
                float* fov    = reinterpret_cast<float*>(static_cast<uint8_t*>(src) + 0x38);
                const float* aspect = reinterpret_cast<const float*>(static_cast<uint8_t*>(src) + 0x3C);
                const float had = *fov;
                const float asp = *aspect;
                CyberpunkVR_DebugSceneFovWas = had;

                // The render aspect, from the launcher's own picture size -- the same source the FOV
                // hook derives its vertical from, so the two cannot drift apart.
                const float want_asp = (g_launcherWidth > 1 && g_launcherHeight > 1)
                    ? (static_cast<float>(g_launcherWidth) / static_cast<float>(g_launcherHeight))
                    : 1.0f;
                const float da = asp > want_asp ? asp - want_asp : want_asp - asp;

                if (had > 1.0f && had < 179.0f && da < 0.02f && had != want) {
                    *fov = want;
                    ++CyberpunkVR_DebugSceneFovForced;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    return g_orig_cam_setup_copy(dst, src, a3);
}

CVR_DETOUR("[cam] setup copy sub_1404E4030", CAM_SETUP_COPY_RVA, Detour_CamSetupCopy,
           g_orig_cam_setup_copy)

}  // namespace detail
}  // namespace cvr
