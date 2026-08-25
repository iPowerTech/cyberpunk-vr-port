// VisionLayer -- the scanner outline, in the second eye.
//
// SOLVED, and the finding is why this file can be short: the outline is not part of the view's colour
// image. RenderVisionElements writes it by COMPUTE into a surface of its own, so the second eye needs
// that surface identified, fitted pixel-exactly, and blended with STRAIGHT alpha -- not the
// premultiplied alpha the HUD wants. Getting the blend mode wrong is a visible halo, not a crash.
//
// IDENTIFICATION IS THE HARD PART, and the comment the moved block opens with says why a resource
// descriptor alone will not do it: several surfaces in a frame share the outline's dimensions and
// format. So the match is a conjunction -- descriptor signature, the dispatch that just ran, and
// full-size-for-this-view -- and the node->target map exists to make the third term answerable.
//
// The reports are kept because the failure mode here is SILENCE: an outline that never appears looks
// exactly like a feature that is switched off, and rtmap_report/vision_report are what tell those
// two apart.

#include "Stereo/SyncStereo.hpp"
#include "Utils/StereoLog.hpp"
#include "Stereo/VrcamConfig.hpp"   // vrcam.json access + CName hashing, shared with the launcher
#include "Render/ColorBlit.hpp"   // HUD debug overlay on the mirror image
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <dxgi1_4.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "MinHook.h"
#include "Utils/LogThrottle.hpp"
#include "Stereo/StereoInternal.hpp"
#include "Stereo/EngineRvas.hpp"
#include "Stereo/DetourRegistry.hpp"
#include "Stereo/StereoInternal.hpp"
#include "Stereo/EngineRvas.hpp"

namespace cvr {
namespace detail {

// Naming the outline surface by DESCRIPTOR does not work: at the second view's resolution there
// are two 1-mip RGBA8_SRGB render targets with identical descs (83220 and 83328), and picking by
// desc alone is exactly the kind of guess that has cost this project several rounds. Name it by
// WHO BINDS IT instead -- the target bound while CRenderNode_RenderVisionElements is on the
// stack is the outline surface by construction, per view. That node is the sole writer, and its
// draws are attributed correctly (the draw census sees it firing for both views).
constexpr uint32_t VISION_ELEMENTS_RVA = 0x61FDE4;
// The outline layer is produced by a COMPUTE dispatch, not a draw -- which is why hanging the
// detection off OMSetRenderTargets found nothing at all. In the capture:
//     VRCAM  PipelineState_1213  Dispatch(306,320,1) -> UAV barrier on Resource_85137 (2444x2560)
//     MAIN   PipelineState_1213  Dispatch(320,320,1) -> UAV barrier on Resource_85164 (2560x2560)
// so both eyes DO produce their own, fully symmetric; it is only never composited into the
// second one. (Resource_83328, which looked like VRCAM's, is the DiscardResource target on the
// very next line -- a transient whose memory happened to hold someone else's picture.)
//
// The dispatch-shape rule alone is NOT unique -- measured, not assumed. Live it fired 30845 times
// in one session across five distinct shapes per view, and the capture says exactly why: eleven
// dispatches in the frame write a full-size RGBA8 UAV at 8x8, six different pipeline states.
// Per view, ordered:
//
//     AsyncComputeDuringShadowmaps  PS494  -> half-size    PS619 -> half-size   PS1143 -> FULL
//     PostFX                        PS1040 -> FULL                              PS1213 -> FULL
//
// PS1213 is the outline layer (VRCAM Resource_85137, MAIN Resource_85164). So two more conditions
// pin it, and both are things we can check honestly at the barrier:
//   * the texture is EXACTLY the view's own render size -- kills the half-size passes outright;
//   * within one frame-graph NODE it is the Nth full-size match, N from the capture (PS1040 is 0,
//     PS1213 is 1). The ordinal resets when the node changes, so the async-compute node's own
//     full-size write cannot bleed into the count.
// The [vismap] report below prints node/size/ordinal so the value of N is read off measurement
// rather than trusted.
thread_local UINT t_last_disp[3] = {0, 0, 0};

// MEASURED, and it turns out the ordinal was the wrong axis. The [vismap] table for a live scan:
//
//   VRCAM  61EE78#0 1222x1280   61EE78#1 1222x1280   <- CRenderNode_DrawConeAO, half-res
//          EFC110#0 2444x2560                        <- CRenderNode_GenerateTonemappingLUT
//          77120C#0 2444x2560                        <- CRenderNode_GameplayPostFX
//          61FDE4#0 2444x2560                        <- CRenderNode_RenderVisionElements
//
// Three nodes produce a full-size match and every one of them at ordinal 0, so VisionPick=1 chose
// nothing at all -- which is why no snapshot was taken. But the table also names the writer
// outright: CRenderNode_RenderVisionElements, the node the outline belongs to by its own name,
// firing 1190 times for VRCAM against MAIN's 1191. It never appeared in the DRAW census because
// it does not draw; it dispatches. Select by node, and the identification stops being a heuristic.
extern "C" __declspec(dllexport) uint32_t CyberpunkVR_VisionNode = 0x61FDE4;
// The tally that FOUND that node. Report-only; the snapshot below does not need it.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_VisionMap = 0;
// Ordinal of the full-size match inside that node. 0 -- measured, not assumed.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_VisionPick = 0;
// Blend the snapshotted layer into the second eye (openxr_capture does the pass).
extern "C" __declspec(dllexport) int CyberpunkVR_VisionToSecondEye = 1;
// Blend for the outline layer: 3 = straight alpha, which is what PipelineState_1216 does and the
// default; 0 = premultiplied, 1 = opaque replace, 2 = additive -- kept for A/B without a rebuild.
extern "C" __declspec(dllexport) int CyberpunkVR_VisionDebug = 3;
// The outline layer is the size of the view's RENDER RECT (2444x2560), while the eye image is the
// texture the engine copies it into (2444x2444) -- the top 2444 rows of it. Stretching the layer
// over the eye therefore lifts it by 116 rows at the bottom, which is the "outline sits higher
// than MAIN's" symptom exactly. 1 = pixel-exact (correct), 0 = old stretch, for A/B.
extern "C" __declspec(dllexport) int   CyberpunkVR_VisionFit  = 1;
extern "C" __declspec(dllexport) float CyberpunkVR_VisionOffX = 0.0f;
extern "C" __declspec(dllexport) float CyberpunkVR_VisionOffY = 0.0f;
extern "C" __declspec(dllexport) uint64_t CyberpunkVR_DebugVisionOverlays = 0;

bool vision_layer_signature(const D3D12_RESOURCE_DESC& d) {
    return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
           d.MipLevels == 1 && d.DepthOrArraySize == 1 && d.SampleDesc.Count == 1 &&
           d.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
           (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
           (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) &&
           d.Width >= 1000 && d.Height >= 1000;
}

bool vision_matches_last_dispatch(const D3D12_RESOURCE_DESC& d) {
    if (t_last_disp[2] != 1 || !t_last_disp[0] || !t_last_disp[1]) return false;
    const UINT gx = static_cast<UINT>((d.Width + 7) / 8);
    const UINT gy = (d.Height + 7) / 8;
    return t_last_disp[0] == gx && t_last_disp[1] == gy;
}

// Is this the second view's OWN full-size surface, rather than one of its half-res intermediates?
bool vision_is_vrcam_full_size(const D3D12_RESOURCE_DESC& d) {
    const uint32_t w = g_vrcam_view_w.load(std::memory_order_acquire);
    const uint32_t h = g_vrcam_view_h.load(std::memory_order_acquire);
    return w && h && d.Width == w && d.Height == h;
}

// Per-node ordinal of full-size matches, on the recording thread.
thread_local uintptr_t t_vision_node = 0;
thread_local int32_t   t_vision_ord = 0;

static thread_local ID3D12Resource* t_vision_bound = nullptr;

// ---- node -> render-target map ---------------------------------------------------------------
// Built because two inferences in a row went wrong here. "RenderVisionElements draws for both
// views" came from its ABSENCE in the exclusive draw census -- but absence there also means
// "never draws at all", which is what the missing [vision] lines then showed. Rather than guess
// again, record what every node actually binds, per view: node RVA + target size/format/mips.
// The scanner's outline surface is known from a capture (MAIN 2560x2560 1-mip RGBA8_UNORM,
// VRCAM 2444x2560 1-mip RGBA8_UNORM_SRGB), so whichever node binds that shape is its writer.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_RtMapProbe = 1;   // OFF: [rtmap] node -> render-target map
struct RtMapEntry {
    uint32_t node_rva, w, h, fmt, mips;
    uint64_t hits[2];
};
static std::array<RtMapEntry, 128> g_rtmap{};
static uint32_t g_rtmap_n = 0;
static std::mutex g_rtmap_mtx;

static void rtmap_report() {
    static uint64_t s_last = 0;
    const uint64_t now = GetTickCount64();
    if (s_last && now - s_last < 20000) return;
    RtMapEntry e[128];
    uint32_t n;
    {
        std::lock_guard<std::mutex> lk(g_rtmap_mtx);
        n = g_rtmap_n;
        for (uint32_t i = 0; i < n; ++i) e[i] = g_rtmap[i];
    }
    if (!n) return;
    s_last = now;
    // Only 1-mip colour targets at view size or larger: that is the shape of an overlay layer,
    // and printing every shadow atlas and froxel grid would drown the line.
    for (int pass = 0; pass < 2; ++pass) {
        char line[1400];
        int u = 0, c = 0;
        line[0] = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (!e[i].hits[pass]) continue;
            // RGBA8 flavours only (TYPELESS 27 / UNORM 28 / UNORM_SRGB 29). The first report
            // listed every full-size 1-mip target and ran past the line budget, which is how a
            // 37-entry list arrived truncated. The outline surface is RGBA8, so this both
            // shortens the line and keeps exactly the candidates.
            if (e[i].mips != 1 || e[i].w < 1000) continue;
            if (e[i].fmt != 27 && e[i].fmt != 28 && e[i].fmt != 29) continue;
            ++c;
            if (u < static_cast<int>(sizeof(line)) - 56)
                u += snprintf(line + u, sizeof(line) - u, "%X:%ux%u/f%u(%llu) ",
                              e[i].node_rva, e[i].w, e[i].h, e[i].fmt,
                              (unsigned long long)e[i].hits[pass]);
        }
        log("[rtmap] %-5s node:RGBA8 1-mip full-size targets (%d): %s",
            pass ? "VRCAM" : "MAIN", c, c ? line : "(none)");
    }
}

void rtmap_note(ID3D12Resource* res, bool vrcam) {
    if (!g_exe_base || !res) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_exe_base);
    const uintptr_t work = t_current_node_work;
    const uint32_t rva = (work > base) ? static_cast<uint32_t>(work - base) : 0;
    D3D12_RESOURCE_DESC d{};
    if (!mirror_get_resource_desc(res, &d)) return;
    {
        std::lock_guard<std::mutex> lk(g_rtmap_mtx);
        uint32_t i = 0;
        for (; i < g_rtmap_n; ++i)
            if (g_rtmap[i].node_rva == rva && g_rtmap[i].w == (uint32_t)d.Width &&
                g_rtmap[i].h == d.Height && g_rtmap[i].fmt == (uint32_t)d.Format &&
                g_rtmap[i].mips == d.MipLevels) break;
        if (i == g_rtmap_n) {
            if (g_rtmap_n >= g_rtmap.size()) return;
            g_rtmap[g_rtmap_n++] = { rva, (uint32_t)d.Width, d.Height,
                                     (uint32_t)d.Format, d.MipLevels, {0, 0} };
        }
        ++g_rtmap[i].hits[vrcam ? 1 : 0];
    }
    rtmap_report();
}

// Aggregate, not one line per hit. The first version logged on every change of resource pointer,
// which the engine's transient ring makes near-continuous: 30845 lines and a 2.2 MB log for one
// scan. Same mistake shape as the truncated [rtmap] line -- report a table, not a stream.
struct VisMapEntry {
    uint32_t node_rva, w, h;
    int32_t  ord;
    uint64_t hits[2];
};
static std::array<VisMapEntry, 64> g_vismap{};
static uint32_t g_vismap_n = 0;
static std::mutex g_vismap_mtx;

static void vision_report() {
    static uint64_t s_last = 0;
    const uint64_t now = GetTickCount64();
    if (s_last && now - s_last < 20000) return;
    VisMapEntry e[64];
    uint32_t n;
    {
        std::lock_guard<std::mutex> lk(g_vismap_mtx);
        n = g_vismap_n;
        for (uint32_t i = 0; i < n; ++i) e[i] = g_vismap[i];
    }
    if (!n) return;
    s_last = now;
    for (int pass = 0; pass < 2; ++pass) {
        char line[1200];
        int u = 0, c = 0;
        line[0] = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (!e[i].hits[pass]) continue;
            ++c;
            if (u < static_cast<int>(sizeof(line)) - 48)
                u += snprintf(line + u, sizeof(line) - u, "%X#%d:%ux%u(%llu) ",
                              e[i].node_rva, e[i].ord, e[i].w, e[i].h,
                              (unsigned long long)e[i].hits[pass]);
        }
        log("[vismap] %-5s node#ordinal:size(hits) (%d): %s",
            pass ? "VRCAM" : "MAIN", c, c ? line : "(none)");
    }
}

void vision_note_surface(ID3D12Resource* res, bool vrcam,
                                uint32_t node_rva, int32_t ord) {
    D3D12_RESOURCE_DESC d{};
    if (!mirror_get_resource_desc(res, &d)) return;
    {
        std::lock_guard<std::mutex> lk(g_vismap_mtx);
        uint32_t i = 0;
        for (; i < g_vismap_n; ++i)
            if (g_vismap[i].node_rva == node_rva && g_vismap[i].ord == ord &&
                g_vismap[i].w == (uint32_t)d.Width && g_vismap[i].h == d.Height) break;
        if (i == g_vismap_n) {
            if (g_vismap_n >= g_vismap.size()) return;
            g_vismap[g_vismap_n++] = { node_rva, (uint32_t)d.Width, d.Height, ord, {0, 0} };
        }
        ++g_vismap[i].hits[vrcam ? 1 : 0];
    }
    vision_report();
}

}  // namespace detail
}  // namespace cvr
