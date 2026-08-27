// SceneEyeView -- giving the second eye its rotation at the moment it is read, not before.
//
// THE PROBLEM THIS SOLVES, and it took four wrong sites to state it properly. While a scene plays
// somewhere the player is not -- a braindance -- the two eyes have to be given one rotation or they
// look two ways at once. The first eye is easy: the located camera is ours to write. The second is a
// component the engine re-derives from the player's body, and every attempt to put a value in it
// lost in its own way:
//
//   * PatchCamera. Writes land, and the engine's own store lands too: about twice a second the
//     reader took ours, which in a braindance is tens of degrees off the scene. Seen as the LEFT eye
//     snapping and recovering, with the right eye and the flat monitor clean -- and when the eyes
//     were paired, the same jump appeared in both, because the pairing copied it.
//   * The RTT view-create. No effect at all.
//   * Reading the component back to compose the head onto it. The composition ate its own output:
//     one multiplication per frame, the view spinning up continuously.
//   * The fov write site. Untested in the end -- overtaken by this.
//
// WHAT IS DIFFERENT HERE. sub_140AC2BA4 is the function that BUILDS that view, and it reads the
// component's own transform to do it: `movsd xmm2,[rbx+0xE0]`, `movups xmm0,[rbx+0xF0]`, then the fov
// through the vtable. Its first argument is the component itself -- verified live, rcx equal to
// g_camObjVrcam on entry. A value written here cannot lose a race, because there is no one left to
// race: the reader is the next instruction.
//
// The value is composed once, in LocateCamera, from the rotation the ENGINE hands over for the
// scene, with the head on top. One composition for two eyes -- two compositions, however close, are
// a skew between them.

#include "Camera/CameraState.hpp"
#include "Core/VrCoreShared.hpp"
#include "Stereo/DetourRegistry.hpp"
#include "Stereo/EngineRvas.hpp"

#include <windows.h>
#include <atomic>
#include <cstdint>

namespace cvr {
namespace detail {

using RttViewBuildFn = __int64 (__fastcall*)(void*, void*, void*);
static RttViewBuildFn g_orig_rtt_view_build = nullptr;

static __int64 __fastcall Detour_RttViewBuild(void* comp, void* a2, void* a3) {
    if (comp && CyberpunkVR_SceneCamFarRotFromRtt && CyberpunkVR_DebugCamFallbackActive &&
        !CyberpunkVR_SceneCamNear && CyberpunkVR_SceneEyeQuatValid) {
        // Only the component this port owns as the second eye. This builder serves every RTT camera
        // in the world -- a security feed pointed at a corridor is not this eye, and writing the
        // player's rotation into it would aim it at nothing.
        const uintptr_t want = g_camObjVrcam.load(std::memory_order_relaxed);
        if (want && reinterpret_cast<uintptr_t>(comp) == want) {
            __try {
                float* q = reinterpret_cast<float*>(static_cast<uint8_t*>(comp) + 0xF0);
                for (int i = 0; i < 4; ++i) q[i] = CyberpunkVR_SceneEyeQuat[i];
                ++CyberpunkVR_DebugSceneEyeQuatWrites;
                // THE POSITION, FOR THE SAME REASON AS THE ROTATION. The engine shoves this component
                // clear of anything standing where the viewpoint is -- measured as 0.14 m, two frames
                // out and two frames back, with an NPC behind the braindance camera. The steady value
                // is decided upstream; delivering it here means the view is built from it, and there is
                // no writer left after this point.
                if (CyberpunkVR_SceneCamFarPosAtViewBuild && CyberpunkVR_SceneEyePosValid) {
                    int32_t* p = reinterpret_cast<int32_t*>(static_cast<uint8_t*>(comp) + 0xE0);
                    for (int i = 0; i < 3; ++i) p[i] = CyberpunkVR_SceneEyePosFP[i];
                    ++CyberpunkVR_DebugSceneEyePosWrites;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    return g_orig_rtt_view_build(comp, a2, a3);
}

CVR_DETOUR("[cam] rtt view build sub_140AC2BA4", RTT_VIEW_BUILD_RVA, Detour_RttViewBuild,
           g_orig_rtt_view_build)

}  // namespace detail
}  // namespace cvr
