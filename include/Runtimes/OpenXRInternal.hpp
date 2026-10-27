#pragma once
// Private implementation header for the OpenXR module: openxr_manager.cpp and any
// method-group translation units split out of it. Holds shared file-scope state and
// helpers as `inline` so there is exactly ONE instance/definition across all those
// TUs (no silent state divergence, no linkage clash).
//
// NOT a public API header. Include it only from the openxr_*.cpp implementation
// files, and only AFTER "openxr_manager.h" (some helpers use manager-level free
// functions declared there).
#include <atomic>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <d3d12.h>
#include <vector>
#include "Utils/XrMath.hpp"              // XR types + quaternion math (used by the cant helpers)
#include "Runtimes/RuntimeFovCorrection.hpp"   // ComputeRuntimeFovCorrection for cant/FOV helpers

// The central logger is defined in vr_core.cpp; every TU forward-declares it.
extern void Log(const char* fmt, ...);

#include "Hooks/Ngx.hpp"
#include "Utils/LogThrottle.hpp"

// Shared free-function / global decls used across the split OpenXR TUs.
// Definitions live in openxr_manager.cpp or vr_core.cpp; these mirror the
// forward-decls that the monolith kept at its top.
extern volatile int g_verboseLog;   // gate per-frame spam

// Config getters (definitions in vr_core.cpp / openxr_manager.cpp). Mirrors the
// forward-decl block the monolith kept at its top so every split TU can call them.
extern "C" int GetDisableRoll();
extern "C" float GetForcedFov();
extern "C" float GetGameRenderFovDeg();
extern "C" float GetGameRenderVerticalFovDeg();
extern "C" float GetTargetRenderVfovDegC();
extern "C" float GetMenuFov();
extern "C" float GetMenuFollowDeg();
extern "C" int GetMenuRectMode();
extern "C" int GetMenuMode();
extern "C" int GetSyncSequential();
extern "C" int Get3DofMovement();
extern "C" float GetVrSharpness();
extern "C" float GetVrSharpmix();
extern "C" int GetReuseLastFrameOutput();
extern "C" float GetMotionPredictMs();
extern "C" int GetRenderPoseSubmit();
extern "C" int GetDepthSubmit();
extern "C" int GetPoseLag();
extern "C" uint32_t GetRenderedCameraSeq();
extern "C" int GetXrRuntimeMode();
// Tunable accessors (defined in openxr_manager.cpp; read the g_* atomics above)
extern "C" float GetPoseBlend();
extern "C" float GetFlowSmooth();
extern "C" float GetHmdTrackingSmooth();
extern "C" int GetInputActionsEnabled();
extern "C" int GetMonoXQueueWait();
extern "C" int GetMonoDepthCapture();
// GPU cross-queue barrier helper (defined in swapchain_hooks.cpp).
extern "C" void CyberpunkVRPort_WaitOnAllGameSignals(ID3D12CommandQueue* consumerQueue);

// Small shared helper (was a file-scope static in openxr_manager.cpp).
inline bool ReuseLastFrameOutputEnabled() { return GetReuseLastFrameOutput() != 0; }

// ---- runtime tunables -----------------------------------------------------------
// Lightweight atomics (g_verboseLog pattern). PERSISTED to vrport.ini: the ini reader
// pushes parsed values in via the Set* accessors, Save reads them back out via Get*.
inline std::atomic<float> g_hmdTrackingSmooth{0.35f};

// ---- tiny D3D helpers ---------------------------------------------------------

inline void SetD3DName(ID3D12Object* object, const wchar_t* name) {
    if (object && name) {
        object->SetName(name);
    }
}

inline void SetD3DNamef(ID3D12Object* object, const wchar_t* format, ...) {
    if (!object || !format) {
        return;
    }
    wchar_t name[192]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(name, sizeof(name) / sizeof(name[0]), _TRUNCATE, format, args);
    va_end(args);
    object->SetName(name);
}

// ---- FOV / display-cant helpers (were file-scope static in openxr_manager.cpp) ----
inline XrFovf ApplyForcedProjectionFov(const XrFovf& sourceFov, const XrFovf* pairFovs, int eyeIndex, float width, float height) {
    float forceFov = GetForcedFov();
    if (forceFov <= 1.0f || forceFov >= 170.0f) {
        if (pairFovs && eyeIndex >= 0 && eyeIndex <= 1) {
            const RuntimeFovCorrection corr = ComputeRuntimeFovCorrection(pairFovs[0], pairFovs[1]);
            XrFovf fov = corr.eye[eyeIndex];
            
            const float gameFovDeg = GetGameRenderFovDeg();
            if (gameFovDeg > 1.0f) {
                const float wantHalfH = (gameFovDeg * 3.1415926535f / 180.0f) * 0.5f;
                const float hCenter = (fov.angleRight + fov.angleLeft) * 0.5f;

                // DESCRIBE WHAT WAS RENDERED, NOT WHAT THE PANEL IS.
                //
                // A projection view is a promise that this rectangle contains exactly this frustum.
                // The engine renders a SYMMETRIC frustum about the camera axis on both axes, so the
                // honest submit is symmetric on both. Passing the runtime's raw asymmetric vertical
                // through instead was a 1.8% lie: measured, the engine rendered V 100.02 while the
                // submit claimed the runtime's 99.0, and the compositor squashes the difference.
                //
                // Submit the rendered frustum without cropping: the degree of
                // extra vertical simply falls outside the panel and is never shown.
                const float gameVFovDeg = GetGameRenderVerticalFovDeg();
                const float realHalfV = (fov.angleUp - fov.angleDown) * 0.5f;
                const float vCenter = (fov.angleUp + fov.angleDown) * 0.5f;
                const bool  haveRenderedV = (gameVFovDeg > 1.0f && gameVFovDeg < 179.0f);
                const float wantHalfV = haveRenderedV
                    ? (gameVFovDeg * 3.1415926535f / 180.0f) * 0.5f
                    : realHalfV;

                fov.angleLeft  = hCenter - wantHalfH;
                fov.angleRight = hCenter + wantHalfH;
                // Symmetric about the camera axis when the rendered vertical is known; the runtime's
                // own centre only as a fallback before the FOV hook has fired.
                fov.angleUp    = (haveRenderedV ? 0.0f : vCenter) + wantHalfV;
                fov.angleDown  = (haveRenderedV ? 0.0f : vCenter) - wantHalfV;
                
                // Was every 200th call, which still came to 169 lines a session saying the same
                // four numbers. The numbers only move when the FOV or the zoom does, so a 30 s
                // heartbeat loses nothing; DEBUG keeps the old cadence.
                static uint32_t s_sfLogN = 0;
                if (g_verboseLog ? ((s_sfLogN++ % 200) == 0) : true) {
                    const float hfovDeg = (fov.angleRight - fov.angleLeft) * (180.0f / 3.1415926535f);
                    const float vfovDeg = (fov.angleUp - fov.angleDown) * (180.0f / 3.1415926535f);
                    LOG_THROTTLED(30000, "OpenXRManager[SUBMITFOV]: eye=%d gameFov=%.2f -> submitHFov=%.2f submitVFov=%.2f (lensHFov=%.2f lensVFov=%.2f decantYaw=%d)\n",
                        eyeIndex, gameFovDeg, hfovDeg, vfovDeg,
                        (sourceFov.angleRight - sourceFov.angleLeft) * (180.0f / 3.1415926535f),
                        (sourceFov.angleUp - sourceFov.angleDown) * (180.0f / 3.1415926535f),
                        corr.yawEnabled ? 1 : 0);
                }
                return fov;
            }
            return fov;
        }
        return sourceFov;
    }

    // Custom override (xr_force_fov): keep the historical "forced symmetric
    // projection" behavior, but derive vertical FOV from the ACTUAL render aspect,
    // not a broken hardcoded 1.0. This matches the UI text: it changes only the
    // OpenXR projection layer FOV, not the CP2077 camera FOV.
    float aspect = (height > 1.0f) ? (width / height) : 0.0f;
    if (!(aspect > 0.01f && aspect < 10.0f)) {
        const float tanLeft = std::tanf(-sourceFov.angleLeft);
        const float tanRight = std::tanf(sourceFov.angleRight);
        const float tanDown = std::tanf(-sourceFov.angleDown);
        const float tanUp = std::tanf(sourceFov.angleUp);
        const float v = tanUp + tanDown;
        aspect = (v > 1.0e-5f) ? ((tanLeft + tanRight) / v) : 1.0f;
    }

    const float halfFovH = (forceFov * 3.1415926535f / 180.0f) * 0.5f;
    const float halfFovV = atanf(tanf(halfFovH) / aspect);
    XrFovf fov{};
    fov.angleLeft = -halfFovH;
    fov.angleRight = halfFovH;
    fov.angleDown = -halfFovV;
    fov.angleUp = halfFovV;
    return fov;
}

// ---- mono swapchain format pick (were file-scope static in openxr_manager.cpp) ----
inline bool ContainsSwapchainFormat(const std::vector<int64_t>& formats, int64_t candidate) {
    for (const int64_t format : formats) {
        if (format == candidate) {
            return true;
        }
    }
    return false;
}

// THE EYE SWAPCHAIN'S FORMAT, AND WHETHER THE SUBMIT MUST CONVERT INTO IT.
//
// OpenXR defines a non-sRGB swapchain as LINEAR: the compositor encodes it for the display itself. CP2077's
// backbuffer is typed R8G8B8A8_UNORM but holds tonemapped, display-ENCODED bytes, and so do both of our eye
// snapshots (the second eye is written through an _UNORM_SRGB view, src/Runtimes/OpenXRCapture.cpp). Copied
// into a UNORM swapchain those bytes are encoded a second time: the washed-out, over-bright picture first
// reported through Virtual Desktop, which honours the format strictly. The fix was VD-only until 2026-10-16,
// so every other runtime that lists R8G8B8A8_UNORM (the OpenXR simulator, Meta's) still got the UNORM
// swapchain; the TofuExpress fork fixed the same thing for every runtime.
//
// So: an 8-bit source takes its bit-compatible _UNORM_SRGB companion -- the same bits, now declared as
// encoded, and the submit stays a plain CopyResource. A runtime without that companion gets another format
// and convert = true: the submit draws through SrgbToLinearPass, which decodes to linear and lets the target
// view encode again if it is sRGB (src/Render/SrgbToLinearPass.cpp). A straight copy into an unrelated format
// is what once blanked the headset (FP16 on SteamVR); the pass is what makes any listed format usable.
struct MonoSwapchainFormat {
    int64_t format = 0;
    bool convert = false;   // the submit draws through SrgbToLinearPass instead of copying
};

// testConvert (TEST ONLY, cvr_swapchain_convert.txt next to the executable): pass over the companion, so a
// runtime that has it still exercises the decode.
inline MonoSwapchainFormat PickMonoSwapchainFormat(const std::vector<int64_t>& runtimeFormats, int64_t gameFormat,
                                                   bool testConvert = false) {
    int64_t companion = 0;
    switch (static_cast<DXGI_FORMAT>(gameFormat)) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            companion = static_cast<int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB); break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            companion = static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB); break;
        default: break;
    }
    if (companion) {
        if (!testConvert && ContainsSwapchainFormat(runtimeFormats, companion)) return {companion, false};
        const int64_t converted[] = {
            static_cast<int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
            static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),
            static_cast<int64_t>(DXGI_FORMAT_R16G16B16A16_FLOAT),
            static_cast<int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM),
            static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM),
        };
        for (const int64_t candidate : converted) {
            if ((!testConvert || candidate != companion) && ContainsSwapchainFormat(runtimeFormats, candidate))
                return {candidate, true};
        }
    }

    // Anything else (an HDR backbuffer, say) is copied as it was before: the game's own format if listed.
    if (ContainsSwapchainFormat(runtimeFormats, gameFormat)) {
        return {gameFormat, false};
    }

    const int64_t preferredFormats[] = {
        static_cast<int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM),
        static_cast<int64_t>(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
        static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM),
        static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),
        static_cast<int64_t>(DXGI_FORMAT_R16G16B16A16_FLOAT)
    };
    for (const int64_t preferred : preferredFormats) {
        if (ContainsSwapchainFormat(runtimeFormats, preferred)) {
            return {preferred, false};
        }
    }

    return {runtimeFormats.empty() ? gameFormat : runtimeFormats[0], false};
}
