#include "FsrFramegen/FrameGenFsr3.hpp"
#include "FsrFramegen/FrameGenDilate.hpp"
#include "FsrFramegen/FrameGenCapture.hpp"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

extern void Log(const char* fmt, ...);
// Set by Stereo/DeviceHooks.cpp the first time the game submits work -- the queue our own
// command list is executed on, so our submission lands in the same GPU timeline as the game's.
extern "C" ID3D12CommandQueue* CyberpunkVR_GetGameQueue();
// Runtimes/OpenXRManager.cpp -- see their own doc comments for why these are free functions
// instead of an OpenXRManager.hpp include.
extern "C" void CyberpunkVR_GetPresentedBackbufferDesc(uint32_t* w, uint32_t* h, uint32_t* fmt);
extern "C" uint64_t CyberpunkVR_PeekNextPresentSerial();
// MAIN's/VRCAM's true final composited per-eye frame (post post-process/HUD) -- see their own
// doc comments (OpenXRManager.hpp). Re-resolved fresh in RunPendingDispatchesUnguarded, AFTER
// OnPresent() has published THIS frame's own capture (Hooks/SwapChain.cpp reordered so
// FrameGen_RunPendingDispatches runs after OnPresent), instead of trusting the pointer
// NgxEvalPassthrough.cpp captured at eval time -- that pointer is unavoidably one real Present
// stale (eval runs mid-render, before this frame's own composite exists), which made every
// interpolated frame actually a midpoint of the PRIOR TWO real frames and show up as a visible
// "snap back" during head rotation.
extern "C" int CyberpunkVR_GetMainEyeFinalFrame(ID3D12Resource** outTex, unsigned long long* outSerial);
extern "C" int CyberpunkVR_GetVrcamEyeFinalFrame(ID3D12Resource** outTex, unsigned long long* outSerial);

// UI-visible status (Overlay/OverlayPanels.cpp, FSR3 tab). Same shape as the DLSS5-NR globals in
// NgxEvalPassthrough.cpp: a plain exported int the overlay reads directly and can clear.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenReady = 0;     // 1 once both eye contexts exist
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenDisabled = 0;  // 1 = a fault stopped it
extern "C" __declspec(dllexport) uint32_t CyberpunkVR_FrameGenDispatches = 0;
extern "C" __declspec(dllexport) uint32_t CyberpunkVR_FrameGenLastResult = 0;
// Debug capture: 0 = no request, 1 = capture eye 0's interpolated (discarded) output, 2 = eye 1.
// Set by the FSR3 panel's "Save interpolated frame" button, cleared once the file is written.
// The ONLY reason to ever look at the smoke test's output -- everywhere else it is discarded.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenCaptureRequest = 0;
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenCaptureLastOk = -1;  // -1 = never attempted, 0 = failed, 1 = ok
// The capture button lives INSIDE the F10 menu, and the menu itself has no game motion vectors --
// FFX has no idea what it is, so a frame captured while it is open shows the panel as a bright
// warped outline (confirmed live: exactly that, edge-lit magenta/cyan around the panel, dark scene
// behind it). Delay a few seconds so there is time to close the menu before the real capture runs.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenCaptureDelayFrames = 0;
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenCapturePendingChoice = 0;
// Diagnostic mode: 0 = normal FFX interpolated output, 1 = our own dilated motion vectors
// visualized as color (see FrameGenCapture_WriteMotionVectorBmp) -- bypasses FFX entirely, so a
// wildly-scaled or wrong-convention motion vector shows up directly instead of however FFX's
// internal passes happen to react to it.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenCaptureRawMv = 0;
extern "C" __declspec(dllexport) float CyberpunkVR_FrameGenCaptureMvRange = 0.1f;
// Diagnostic: capture the game's OWN Color resource -- FFX's currentBackBuffer input -- exactly as
// read, before FFX ever touches it. Splits the search space: if THIS already shows the same
// G-channel-always-0/R-equals-B corruption seen in the FFX output, the bug is in how we read the
// resource (wrong format/key), not in FFX's own passes or the flags/params we hand it.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenCaptureRawInput = 0;
// A/B toggle: user reported the interpolated output got WORSE once optical flow started actually
// contributing (3-frame ghosting/overlap on BOTH eyes, worse than the game-MV-only "long exposure"
// smear it replaced) -- default OFF until the scale/convention bug is found, so FrameGen is at
// least back to its previous (known, milder-artifact) behavior. Flip on to A/B test a fix.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenOpticalFlowEnable = 0;
// Real presentation, not just smoke-test/debug-capture: when on, every successful dispatch also
// resize+tonemaps the interpolated eye into a pool the XR submit thread can pick up on a "stale"
// cycle (see FrameGen_GetPresentableEye / OpenXRFrameLoop.cpp's own comment at the call site).
// Default OFF -- separate from CyberpunkVR_FrameGenEnable so existing debug-capture workflows are
// completely unaffected, and because this is materially higher-risk (touches what the headset
// actually shows, not a discardable smoke test).
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenPresentEnable = 0;

namespace {

void* g_scratchBuffer = nullptr;
FfxInterface g_backendInterface{};
FfxFrameInterpolationContext g_eyeContext[2]{};
bool g_eyeContextValid[2] = {false, false};
// Real optical flow, computed from consecutive Color snapshots by AMD's own SDK component (NOT
// reimplemented by us, unlike the dilate/reconstruct pass) -- fills in exactly what dilated game
// motion vectors can't cover (disocclusions, transparencies, particles, reflections, UI). Without
// this, frame interpolation falls back to game-MV-only reconstruction in those regions, which is
// what produced the "long exposure" ghosting/smearing the user saw once the color corruption
// (fixed separately) stopped masking it. One context per eye, same lifetime/sizing as the
// FfxFrameInterpolationContext it feeds.
FfxOpticalflowContext g_ofContext[2]{};
bool g_ofContextValid[2] = {false, false};
// Set true whenever CreateEyeContexts (re)creates both contexts for an eye -- forces reset=true on
// that eye's next dispatch regardless of the caller's own reset param, see CreateEyeContexts' own
// comment for why.
bool g_eyeContextFirstDispatch[2] = {false, false};
bool g_backendReady = false;
// The size the eye contexts were last (re)created at. CP2077's dynamic resolution scaling means
// the DLSS render size the caller reports can shrink and grow frame to frame -- a context created
// for one size and then dispatched at a LARGER one writes past its own internally-sized buffers
// (FI_ReconstructedDepthInterpolatedFrame and friends, sized off maxRenderSize at create time).
// That is a real GPU-corruption-then-hang bug this build shipped with (see the commit message).
// Grow-only, like a vector's capacity: recreate only when a request exceeds what is already
// allocated, never on every fluctuation.
unsigned g_contextRenderW = 0, g_contextRenderH = 0;
unsigned g_contextDisplayW = 0, g_contextDisplayH = 0;

void DestroyEyeContexts() {
    for (int eye = 0; eye < 2; ++eye) {
        if (g_eyeContextValid[eye]) {
            ffxFrameInterpolationContextDestroy(&g_eyeContext[eye]);
            g_eyeContextValid[eye] = false;
        }
        if (g_ofContextValid[eye]) {
            ffxOpticalflowContextDestroy(&g_ofContext[eye]);
            g_ofContextValid[eye] = false;
        }
    }
}

bool CreateEyeContexts(unsigned renderWidth, unsigned renderHeight, unsigned displayWidth,
                       unsigned displayHeight) {
    for (int eye = 0; eye < 2; ++eye) {
        FfxFrameInterpolationContextDescription desc{};
        desc.backendInterface = g_backendInterface;
        // Two context-create flags, both true for this pipeline: CP2077 (like this codebase's own
        // CyberpunkVR_NrDepthConversion default elsewhere) ships REVERSED depth, and reversed-Z
        // engines overwhelmingly pair it with an INFINITE far plane (that is the whole point of
        // reversed-Z: precision far from camera, which an infinite far plane needs). With
        // DEPTH_INFINITE unset, FFX's view-space depth reconstruction uses the FINITE-far-plane
        // matrix branch against a depth buffer that does not have one -- correct only near the
        // camera, degenerating with distance. Exactly the magenta/black pattern seen live (soffit
        // near the camera fine, floor/NPC/hands farther away broken).
        //
        // NOT HDR_COLOR_INPUT: currentBackBuffer is now MAIN's/VRCAM's real final composited frame
        // (m_monoPool/m_vrcamEyePool -- see Hooks/NgxEvalPassthrough.cpp's own comment for why),
        // which is the ACTUAL displayed SDR image -- sRGB gamma-encoded bytes, confirmed by
        // PickMonoSwapchainFormat's own doc comment (OpenXRInternal.hpp) -- not the scene-linear
        // HDR "Color"/"Output" DLSS buffers this used to read. backBufferFormat matches that same
        // reality below (queried at runtime, not hardcoded) instead of assuming FP16 HDR.
        desc.flags = FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED |
                    FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE;
        desc.maxRenderSize = {renderWidth, renderHeight};
        desc.displaySize = {displayWidth, displayHeight};
        {
            uint32_t bbW = 0, bbH = 0, bbFmt = 0;
            CyberpunkVR_GetPresentedBackbufferDesc(&bbW, &bbH, &bbFmt);
            desc.backBufferFormat = (bbFmt != 0)
                ? ffxGetSurfaceFormatDX12(static_cast<DXGI_FORMAT>(bbFmt))
                : FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;  // fallback before the first real Present
        }
        const FfxErrorCode err = ffxFrameInterpolationContextCreate(&g_eyeContext[eye], &desc);
        if (err != FFX_OK) {
            Log("[framegen] ffxFrameInterpolationContextCreate eye=%d failed 0x%08X\n", eye,
                static_cast<unsigned>(err));
            for (int j = 0; j < eye; ++j) {
                ffxFrameInterpolationContextDestroy(&g_eyeContext[j]);
                g_eyeContextValid[j] = false;
            }
            return false;
        }
        g_eyeContextValid[eye] = true;

        FfxOpticalflowContextDescription ofDesc{};
        ofDesc.backendInterface = g_backendInterface;
        ofDesc.resolution = {displayWidth, displayHeight};
        ofDesc.flags = 0;
        const FfxErrorCode ofErr = ffxOpticalflowContextCreate(&g_ofContext[eye], &ofDesc);
        if (ofErr != FFX_OK) {
            // Non-fatal: frame interpolation still works without optical flow, just with the
            // ghosting/smearing this whole feature exists to fix in disoccluded regions -- log and
            // keep going rather than failing the entire eye context.
            Log("[framegen] ffxOpticalflowContextCreate eye=%d failed 0x%08X -- interpolation will "
                "run without optical flow (game-MV-only, more ghosting on disocclusion)\n",
                eye, static_cast<unsigned>(ofErr));
            g_ofContextValid[eye] = false;
        } else {
            g_ofContextValid[eye] = true;
        }
        // Both contexts just got (re)created -- no internal temporal history yet. Force reset=true
        // on the very next dispatch for this eye regardless of what the caller passes (matches the
        // same fix already applied to DLSS5-NR's NrContext::justCreated, same underlying reason:
        // dispatching without reset accumulates optical flow/interpolation against uninitialised
        // history). Left unset, this produced exactly the "images overlapping/stretched, unreadable"
        // regression the user saw the moment optical flow started actually contributing.
        g_eyeContextFirstDispatch[eye] = true;
    }
    g_contextRenderW = renderWidth;
    g_contextRenderH = renderHeight;
    g_contextDisplayW = displayWidth;
    g_contextDisplayH = displayHeight;
    return true;
}

// Scratch interpolated-frame output, one per eye, allocated lazily by the smoke test. Never
// presented -- see FrameGen_DispatchSmokeTest's doc comment.
ID3D12Resource* g_scratchOutput[2] = {nullptr, nullptr};
int g_scratchOutputW[2] = {0, 0};
int g_scratchOutputH[2] = {0, 0};

ID3D12Resource* EnsureScratchOutput(ID3D12Device* dev, int eye, int w, int h) {
    // Grow-only, same reasoning as the eye contexts above: dynamic resolution must not thrash a
    // GPU resource create/destroy every frame just because render size shrank back down.
    if (g_scratchOutput[eye] && w <= g_scratchOutputW[eye] && h <= g_scratchOutputH[eye]) {
        return g_scratchOutput[eye];
    }
    w = (g_scratchOutput[eye] && g_scratchOutputW[eye] > w) ? g_scratchOutputW[eye] : w;
    h = (g_scratchOutput[eye] && g_scratchOutputH[eye] > h) ? g_scratchOutputH[eye] : h;
    if (g_scratchOutput[eye]) {
        g_scratchOutput[eye]->Release();
        g_scratchOutput[eye] = nullptr;
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = static_cast<UINT64>(w);
    rd.Height = static_cast<UINT>(h);
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* res = nullptr;
    if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(&res)))) {
        g_scratchOutput[eye] = res;
        g_scratchOutputW[eye] = w;
        g_scratchOutputH[eye] = h;
    }
    return g_scratchOutput[eye];
}

FfxResource WrapResource(ID3D12Resource* res, FfxResourceStates state, wchar_t* name = nullptr) {
    if (!res) return FfxResource{};
    return ffxGetResourceDX12(res, GetFfxResourceDescriptionDX12(res), name, state);
}

// Bridges FrameGen's linear-HDR, render-resolution output to what the XR submit path can
// CopyResource straight into an eye swapchain image: a resize (render-res -> the game's own flat
// backbuffer size, which is what the swapchain is created to match -- see
// CyberpunkVR_GetPresentedBackbufferDesc's own comment) and a tonemap (scene-linear -> the SAME
// Reinhard + manual sRGB encode this file's own debug-capture path already uses, since CP2077's own
// backbuffer bytes are gamma-encoded despite being typed plain UNORM -- confirmed by
// PickMonoSwapchainFormat's doc comment, OpenXRInternal.hpp). Nearest-neighbour upsample (Load, no
// sampler) -- good enough for a first cut; the game's own backbuffer is only 2x the render
// resolution here, and DLSS-grade upsampling is a separate, much bigger effort this doesn't attempt.
struct PresentBlit {
    ID3D12RootSignature* rootSig = nullptr;
    ID3D12PipelineState* pso = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT descSize = 0;
    UINT ring = 0;
    static constexpr UINT kRingSlots = 16;

    bool Init(ID3D12Device* dev) {
        if (pso) return true;
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;
        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 4;  // uint2 SrcSize + uint2 DstSize
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 2;
        params[1].DescriptorTable.pDescriptorRanges = ranges;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rs{};
        rs.NumParameters = 2;
        rs.pParameters = params;
        ID3DBlob* rsBlob = nullptr;
        ID3DBlob* rsErr = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr))) {
            if (rsErr) rsErr->Release();
            return false;
        }
        HRESULT hr = dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&rootSig));
        rsBlob->Release();
        if (FAILED(hr)) return false;

        // No tonemap here anymore: currentBackBuffer is now MAIN's/VRCAM's real final composited
        // SDR frame, and FFX's own dispatch runs in FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB mode
        // (DispatchSmokeTestUnguarded's own comment) -- meaning g_scratchOutput (this shader's Src)
        // already holds gamma-encoded, display-ready values, the SAME convention Src was captured
        // in. A Reinhard+manual-sRGB-encode pass here (the old formula, correct back when Src was
        // scene-linear HDR from "Color"/"Output") would double-encode it. Straight clamp-and-copy.
        static const char* kBlitHLSL = R"HLSL(
cbuffer K : register(b0) { uint2 SrcSize; uint2 DstSize; };
Texture2D<float4> Src : register(t0);
RWTexture2D<float4> Dst : register(u0);
[numthreads(8,8,1)]
void Blit(uint3 id : SV_DispatchThreadID) {
    if (id.x >= DstSize.x || id.y >= DstSize.y) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(DstSize);
    int2 srcCoord = int2(uv * float2(SrcSize));
    srcCoord = clamp(srcCoord, int2(0, 0), int2(SrcSize) - 1);
    float4 c = Src.Load(int3(srcCoord, 0));
    Dst[id.xy] = float4(saturate(c.rgb), 1.0);
}
)HLSL";
        ID3DBlob* cs = nullptr;
        ID3DBlob* csErr = nullptr;
        if (FAILED(D3DCompile(kBlitHLSL, strlen(kBlitHLSL), nullptr, nullptr, nullptr, "Blit",
                              "cs_5_0", 0, 0, &cs, &csErr))) {
            if (csErr) csErr->Release();
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rootSig;
        pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
        hr = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso));
        cs->Release();
        if (FAILED(hr)) return false;

        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kRingSlots * 2;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) return false;
        descSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }

    void Run(ID3D12Device* dev, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* src, int srcW,
            int srcH, ID3D12Resource* dst, int dstW, int dstH, DXGI_FORMAT dstFormat) {
        const UINT slot = (ring % kRingSlots) * 2;
        ++ring;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(slot) * descSize;
        gpu.ptr += static_cast<UINT64>(slot) * descSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(src, &srv, cpu);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu2 = cpu; cpu2.ptr += descSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = dstFormat;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        dev->CreateUnorderedAccessView(dst, nullptr, &uav, cpu2);

        struct { uint32_t srcW, srcH, dstW, dstH; } k{static_cast<uint32_t>(srcW),
            static_cast<uint32_t>(srcH), static_cast<uint32_t>(dstW), static_cast<uint32_t>(dstH)};
        ID3D12DescriptorHeap* heaps[] = {heap};
        cmdList->SetDescriptorHeaps(1, heaps);
        cmdList->SetComputeRootSignature(rootSig);
        cmdList->SetPipelineState(pso);
        cmdList->SetComputeRoot32BitConstants(0, 4, &k, 0);
        cmdList->SetComputeRootDescriptorTable(1, gpu);
        cmdList->Dispatch((static_cast<UINT>(dstW) + 7) / 8, (static_cast<UINT>(dstH) + 7) / 8, 1);
    }
};
PresentBlit g_presentBlit;

// A 3-slot rotating pool per eye, sized/formatted to exactly match the game's own flat backbuffer
// (CyberpunkVR_GetPresentedBackbufferDesc -- what the XR swapchain images are created to match, see
// PickMonoSwapchainFormat), tagged with the real-frame serial (CyberpunkVR_PeekNextPresentSerial)
// the interpolation was computed from. Deliberately NOT reusing m_monoPool/m_vrcamEyePool: those
// feed frame-pacing telemetry (FitAddDisplayTime) and pose-prediction regression that assume every
// serial change is a REAL game-produced frame -- publishing a synthetic frame through that same
// path would corrupt those without auditing every consumer. This pool is read ONLY by
// FrameGen_GetPresentableEye, matched by exact serial, on the XR thread's existing "stale, game
// hasn't produced a new frame yet" cycle -- the pose used is still that same real frame's own
// (OpenXRFrameLoop.cpp's stale-resubmit path already works this way), so no pacing/pose-prediction
// code needs to change at all.
constexpr int kPresentPoolSlots = 3;
ID3D12Resource* g_presentPool[2][kPresentPoolSlots] = {};
uint64_t g_presentPoolSerial[2][kPresentPoolSlots] = {};
// Each slot rests in COPY_SOURCE between our own writes -- what the XR thread's CopyResource
// needs, exactly like m_vrcamEyeTex already does for the same reason (see PresentBlit's own doc
// comment). Created in UNORDERED_ACCESS (false = "not yet in copy-source, no transition-from
// needed on the very first write to this slot"); flips to true right after every write+barrier.
bool g_presentPoolInCopySrc[2][kPresentPoolSlots] = {};
int g_presentPoolWriteIdx[2] = {0, 0};
int g_presentPoolW = 0, g_presentPoolH = 0;
DXGI_FORMAT g_presentPoolFormat = DXGI_FORMAT_UNKNOWN;
std::mutex g_presentPoolMutex;

// Grow-only is the wrong shape here (unlike every other texture in this file): this pool must
// match the backbuffer EXACTLY, not be at-least-as-large, because it is CopyResource'd (not
// blitted) into the swapchain image on the consuming side -- a size mismatch there is a D3D12
// validation error, not silently-fine oversized geometry. Recreated whenever the backbuffer size
// or format actually changes (a real but rare event -- display resolution, not the render-res
// dynamic scaling the rest of this file already tolerates via grow-only).
bool EnsurePresentPool(ID3D12Device* dev) {
    uint32_t w = 0, h = 0, fmtRaw = 0;
    CyberpunkVR_GetPresentedBackbufferDesc(&w, &h, &fmtRaw);
    if (w == 0 || h == 0 || fmtRaw == 0) return false;  // no real Present captured yet
    const DXGI_FORMAT fmt = static_cast<DXGI_FORMAT>(fmtRaw);
    if (g_presentPoolW == static_cast<int>(w) && g_presentPoolH == static_cast<int>(h) &&
        g_presentPoolFormat == fmt && g_presentPool[0][0]) {
        return true;
    }
    std::lock_guard<std::mutex> lock(g_presentPoolMutex);
    for (auto& eyeSlots : g_presentPool) {
        for (auto& res : eyeSlots) {
            if (res) { res->Release(); res = nullptr; }
        }
    }
    for (auto& eyeSerials : g_presentPoolSerial) {
        for (auto& s : eyeSerials) s = 0;
    }
    for (auto& eyeFlags : g_presentPoolInCopySrc) {
        for (auto& f : eyeFlags) f = false;
    }
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    bool allOk = true;
    for (int eye = 0; eye < 2; ++eye) {
        for (int slot = 0; slot < kPresentPoolSlots; ++slot) {
            ID3D12Resource* res = nullptr;
            if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                    IID_PPV_ARGS(&res)))) {
                allOk = false;
                continue;
            }
            g_presentPool[eye][slot] = res;
        }
    }
    g_presentPoolW = static_cast<int>(w);
    g_presentPoolH = static_cast<int>(h);
    g_presentPoolFormat = fmt;
    if (!allOk) {
        Log("[framegen] present pool create failed (%ux%u fmt=%u) -- presentation stays off this "
            "frame\n", w, h, fmtRaw);
    }
    return allOk;
}

// Resize+tonemaps g_scratchOutput[eye] (this dispatch's interpolated result) into the next slot of
// that eye's present pool, tagged with `serial` -- the real frame this interpolation was computed
// from (CyberpunkVR_PeekNextPresentSerial(), called by the caller AFTER this Present's own
// OpenXRManager::OnPresent() has already run and incremented the real counter -- so this returns
// the SAME serial OnPresent's own CaptureMonoPresentedFrame just used, not the next one; see its
// own doc comment for why that distinction matters). All on g_fgCmdList, our own dedicated
// list -- no shader work ever touches the XR submit thread, which is the fix a past GPU hang in
// this exact file already established (see FrameGen_CapturePendingEye's doc comment).
void PublishPresentableEye(ID3D12Device* dev, ID3D12GraphicsCommandList* cmdList, int eye,
                           ID3D12Resource* hdrSrc, int srcW, int srcH, uint64_t serial) {
    if (!EnsurePresentPool(dev) || !g_presentBlit.Init(dev)) return;
    const int slot = g_presentPoolWriteIdx[eye];
    ID3D12Resource* dst = g_presentPool[eye][slot];
    if (!dst) return;

    if (g_presentPoolInCopySrc[eye][slot]) {
        D3D12_RESOURCE_BARRIER toUav{};
        toUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav.Transition.pResource = dst;
        toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &toUav);
    }
    g_presentBlit.Run(dev, cmdList, hdrSrc, srcW, srcH, dst, g_presentPoolW, g_presentPoolH,
                      g_presentPoolFormat);
    D3D12_RESOURCE_BARRIER toCopySrc{};
    toCopySrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopySrc.Transition.pResource = dst;
    toCopySrc.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toCopySrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toCopySrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &toCopySrc);
    g_presentPoolInCopySrc[eye][slot] = true;

    {
        std::lock_guard<std::mutex> lock(g_presentPoolMutex);
        g_presentPoolSerial[eye][slot] = serial;
        g_presentPoolWriteIdx[eye] = (slot + 1) % kPresentPoolSlots;
    }
}

// Optical flow's own two output resources, sized/formatted exactly as
// ffxOpticalflowGetSharedResourceDescriptions documents (read straight from AMD's SDK source,
// src/components/opticalflow/ffx_opticalflow.cpp: GetOpticalFlowTextureSize divides display size by
// the fixed block size of 8, rounding up) -- created as our OWN raw D3D12 resources and wrapped
// per-dispatch like every other FFX input/output in this file, rather than going through FFX's own
// backend-managed resource allocator (which would need a second effectContextId/registration dance
// this file has no other use for). opticalFlowSCD is a fixed tiny 3x1 buffer regardless of
// resolution; opticalFlowVector scales with display size like everything else here.
ID3D12Resource* g_ofVector[2] = {nullptr, nullptr};
int g_ofVectorW[2] = {0, 0};
int g_ofVectorH[2] = {0, 0};
ID3D12Resource* g_ofScd[2] = {nullptr, nullptr};

ID3D12Resource* CreateOfTexture(ID3D12Device* dev, UINT64 w, UINT h, DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = format;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* res = nullptr;
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  nullptr, IID_PPV_ARGS(&res));
    return res;
}

bool EnsureOpticalFlowResources(ID3D12Device* dev, int eye, int displayW, int displayH, int* outOfW,
                                int* outOfH) {
    const int ofW = (displayW + 7) / 8;
    const int ofH = (displayH + 7) / 8;
    *outOfW = ofW;
    *outOfH = ofH;
    if (!g_ofVector[eye] || ofW > g_ofVectorW[eye] || ofH > g_ofVectorH[eye]) {
        const int newW = (g_ofVector[eye] && g_ofVectorW[eye] > ofW) ? g_ofVectorW[eye] : ofW;
        const int newH = (g_ofVector[eye] && g_ofVectorH[eye] > ofH) ? g_ofVectorH[eye] : ofH;
        if (g_ofVector[eye]) { g_ofVector[eye]->Release(); g_ofVector[eye] = nullptr; }
        g_ofVector[eye] = CreateOfTexture(dev, static_cast<UINT64>(newW), static_cast<UINT>(newH),
                                          DXGI_FORMAT_R16G16_SINT);
        if (!g_ofVector[eye]) return false;
        g_ofVectorW[eye] = newW;
        g_ofVectorH[eye] = newH;
    }
    if (!g_ofScd[eye]) {
        g_ofScd[eye] = CreateOfTexture(dev, 3, 1, DXGI_FORMAT_R32_UINT);
        if (!g_ofScd[eye]) return false;
    }
    return true;
}

// Diagnostic-only: copies the game's OWN Color resource into a texture WE own (g_scratchOutput),
// via an SRV read -- no barrier touched, same "already shader-readable, no transition" assumption
// this file and FrameGenDilate.cpp already make everywhere else for game-owned resources (NGX/FFX
// themselves read Color the same way). CopyTextureRegion would instead need the resource actually
// IN D3D12_RESOURCE_STATE_COPY_SOURCE, which would mean guessing its real current state and
// transitioning a resource we do not own on OUR OWN command list -- avoided entirely by reading it
// as an SRV instead, exactly like every other game resource this file touches.
struct PassthroughCopy {
    ID3D12RootSignature* rootSig = nullptr;
    ID3D12PipelineState* pso = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT descSize = 0;
    UINT ring = 0;
    static constexpr UINT kRingSlots = 16;

    bool Init(ID3D12Device* dev) {
        if (pso) return true;
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 2;
        param.DescriptorTable.pDescriptorRanges = ranges;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rs{};
        rs.NumParameters = 1;
        rs.pParameters = &param;
        ID3DBlob* rsBlob = nullptr;
        ID3DBlob* rsErr = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr))) {
            if (rsErr) rsErr->Release();
            return false;
        }
        HRESULT hr = dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&rootSig));
        rsBlob->Release();
        if (FAILED(hr)) return false;

        static const char* kCopyHLSL = R"HLSL(
Texture2D<float4> Src : register(t0);
RWTexture2D<float4> Dst : register(u0);
[numthreads(8,8,1)]
void Copy(uint3 id : SV_DispatchThreadID) { Dst[id.xy] = Src.Load(int3(id.xy, 0)); }
)HLSL";
        ID3DBlob* cs = nullptr;
        ID3DBlob* csErr = nullptr;
        if (FAILED(D3DCompile(kCopyHLSL, strlen(kCopyHLSL), nullptr, nullptr, nullptr, "Copy",
                              "cs_5_0", 0, 0, &cs, &csErr))) {
            if (csErr) csErr->Release();
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rootSig;
        pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
        hr = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso));
        cs->Release();
        if (FAILED(hr)) return false;

        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kRingSlots * 2;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) return false;
        descSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return true;
    }

    void Run(ID3D12Device* dev, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* src,
            ID3D12Resource* dst, int w, int h) {
        const UINT slot = (ring % kRingSlots) * 2;
        ++ring;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(slot) * descSize;
        gpu.ptr += static_cast<UINT64>(slot) * descSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(src, &srv, cpu);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu2 = cpu; cpu2.ptr += descSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        dev->CreateUnorderedAccessView(dst, nullptr, &uav, cpu2);

        ID3D12DescriptorHeap* heaps[] = {heap};
        cmdList->SetDescriptorHeaps(1, heaps);
        cmdList->SetComputeRootSignature(rootSig);
        cmdList->SetPipelineState(pso);
        cmdList->SetComputeRootDescriptorTable(0, gpu);
        cmdList->Dispatch((static_cast<UINT>(w) + 7) / 8, (static_cast<UINT>(h) + 7) / 8, 1);
    }
};
PassthroughCopy g_passthroughCopy;

// Color snapshot: taken at EVAL time (game's own command list, right after the game's real DLSS
// eval already consumed Color/MotionVectors/Depth as SRVs -- so their D3D12 state is exactly what
// the NGX contract guarantees), NOT deferred to Present time.
//
// CONFIRMED ROOT CAUSE of the persistent magenta (G=0, R=B) corruption: a Present-time SRV read of
// the game's live Color resource -- whether by our own diagnostic passthrough or by FFX's own real
// dispatch, same format, confirmed fmt=10=R16G16B16A16_FLOAT via cyberpunkvrport.log so it was never
// a format mismatch -- reads garbage. By Present time, whatever CP2077 does to that resource between
// its eval call and Present (reused/retransitioned/pooled -- exact mechanism not identified, doesn't
// need to be) has already broken it, and every consumer reading it that late inherits the same
// garbage, FFX included. Bisected live: the SAME PassthroughCopy read, moved to eval time, produced
// a perfectly clean image (user-confirmed).
//
// Fix: snapshot Color into OUR OWN texture right here, every eligible eval call (both eyes, every
// frame -- this is now load-bearing for the real dispatch, not diagnostic-only), and hand FFX this
// stable copy instead of the live game resource. One tiny compute pass (SetDescriptorHeaps/RootSig/
// PSO/Dispatch) on the game's own list -- the same shape of state-changing call DLSS5-NR's own NGX
// Evaluate() already makes on this list every eval, every frame, without incident, so this does not
// reintroduce the un-restored-heap/PSO class of GPU hang FFX's own multi-pass dispatch caused
// earlier (that fix -- deferring FFX's real work to a dedicated command list -- is untouched; only
// what we feed it as currentBackBuffer changes here).
ID3D12Resource* g_colorSnapshot[2] = {nullptr, nullptr};
int g_colorSnapshotW[2] = {0, 0};
int g_colorSnapshotH[2] = {0, 0};

ID3D12Resource* EnsureColorSnapshot(ID3D12Device* dev, int eye, int w, int h) {
    if (g_colorSnapshot[eye] && w <= g_colorSnapshotW[eye] && h <= g_colorSnapshotH[eye]) {
        return g_colorSnapshot[eye];
    }
    w = (g_colorSnapshot[eye] && g_colorSnapshotW[eye] > w) ? g_colorSnapshotW[eye] : w;
    h = (g_colorSnapshot[eye] && g_colorSnapshotH[eye] > h) ? g_colorSnapshotH[eye] : h;
    if (g_colorSnapshot[eye]) { g_colorSnapshot[eye]->Release(); g_colorSnapshot[eye] = nullptr; }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = static_cast<UINT64>(w);
    rd.Height = static_cast<UINT>(h);
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* res = nullptr;
    if (SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(&res)))) {
        g_colorSnapshot[eye] = res;
        g_colorSnapshotW[eye] = w;
        g_colorSnapshotH[eye] = h;
    }
    return g_colorSnapshot[eye];
}

// One eye's captured request, filled by FrameGen_CapturePendingEye (Hooks/NgxEvalPassthrough.cpp,
// inside the DLSS-eval hook -- cheap, no GPU work) and consumed by FrameGen_RunPendingDispatches
// (the Present hook, on our own command list). currentBackBuffer/rawDepth/rawMotionVectors are the
// game's own persistent per-eye resources (not per-frame transients), so holding the pointers from
// eval-time to present-time is the same assumption DLSS5-NR's own NrContext already relies on.
struct PendingEye {
    bool valid = false;
    ID3D12Resource* currentBackBuffer = nullptr;
    ID3D12Resource* rawDepth = nullptr;
    ID3D12Resource* rawMotionVectors = nullptr;
    int renderWidth = 0, renderHeight = 0;
    int displayWidth = 0, displayHeight = 0;
    float motionVectorScaleX = 1.0f, motionVectorScaleY = 1.0f;
    bool invertedDepth = true;
    float cameraNearMeters = 0.1f, cameraFarMeters = 2000.0f, cameraFovYRadians = 1.39626f;
    float frameTimeDeltaMs = 16.6f;
    bool reset = false;
};
PendingEye g_pending[2];

// Our OWN command list/allocator/fence, entirely separate from anything the game records into --
// the whole point of this file's fix. Lazily created once a device is known.
ID3D12CommandAllocator* g_fgAllocator = nullptr;
ID3D12GraphicsCommandList* g_fgCmdList = nullptr;
ID3D12Fence* g_fgFence = nullptr;
HANDLE g_fgFenceEvent = nullptr;
UINT64 g_fgFenceValue = 0;       // next value to signal
UINT64 g_fgFenceSubmitted = 0;   // last value actually signaled via ExecuteCommandLists

// Debug readback: one CPU-visible buffer, reused/grown on demand, plus which eye's capture is
// in flight and at which fence value -- Map()ing a buffer the GPU is still writing is undefined,
// so the file write waits for that same fence the allocator-reset wait already tracks.
ID3D12Resource* g_readback = nullptr;
UINT64 g_readbackCapacity = 0;    // actual allocated buffer size -- only grows
UINT64 g_readbackValidBytes = 0;  // bytes the LAST copy actually wrote (<= capacity) -- what to Map
UINT64 g_readbackRowPitch = 0;
int g_readbackW = 0, g_readbackH = 0;
int g_pendingCaptureEye = -1;
int g_pendingCaptureMode = 0;  // 0 = FFX interpolated output, 1 = our own dilated motion vectors
UINT64 g_pendingCaptureFence = 0;
// "Capture both eyes" (CaptureRequest==3) chains eye1 right after eye0's file is actually written,
// instead of requiring two separate button presses several seconds apart. Found live why this
// matters: comparing eye0 vs eye1 raw motion vectors from two independently-pressed captures showed
// wildly different magnitude/direction, which looked like a per-eye data bug -- but the two captures
// were ~5.75s apart (each needs its own 3s menu-close delay), and no human reproduces identical head
// motion twice with a several-second gap in between. A same-moment pair is the only valid comparison.
bool g_captureBothChain = false;

bool EnsureReadback(ID3D12Device* dev, int w, int h, DXGI_FORMAT format, UINT64* outRowPitch) {
    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = static_cast<UINT64>(w);
    texDesc.Height = static_cast<UINT>(h);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 totalBytes = 0;
    dev->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
    *outRowPitch = footprint.Footprint.RowPitch;

    // ALWAYS update, regardless of whether the underlying allocation below is reused or
    // recreated: switching capture mode (color RGBA16F <-> raw motion vectors RG16F) at the same
    // w/h needs a different stride for the SAME buffer, and the reuse path below does not touch
    // it otherwise. Real bug, found live: reusing a bigger buffer left the OLD (wrong) stride for
    // the new format, which reads as a clean top part (still in-bounds by chance) degrading into
    // garbage/short data below it -- exactly the "cropped in half" symptom reported.
    g_readbackRowPitch = footprint.Footprint.RowPitch;
    g_readbackW = w;
    g_readbackH = h;
    g_readbackValidBytes = totalBytes;

    if (g_readback && g_readbackCapacity >= totalBytes) {
        return true;
    }
    if (g_readback) { g_readback->Release(); g_readback = nullptr; }
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = totalBytes;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&g_readback)))) {
        return false;
    }
    g_readbackCapacity = totalBytes;
    return true;
}

// Records the copy on OUR command list -- the same one the dispatch itself just recorded on.
// Caller (RunPendingDispatchesUnguarded) barriers nothing else here; the scratch output is ours
// alone, so we own its state transitions completely.
void RecordCaptureCopy(ID3D12GraphicsCommandList* cmdList, ID3D12Device* dev,
                       ID3D12Resource* output, int w, int h, DXGI_FORMAT format) {
    UINT64 rowPitch = 0;
    if (!EnsureReadback(dev, w, h, format, &rowPitch)) return;

    D3D12_RESOURCE_BARRIER toSrc{};
    toSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrc.Transition.pResource = output;
    toSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toSrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &toSrc);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = g_readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = format;
    dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
    dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = output;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER toUav = toSrc;
    toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cmdList->ResourceBarrier(1, &toUav);
}

// Called once the fence covering the pending capture has signalled -- safe to Map now.
void WritePendingCaptureIfReady() {
    if (g_pendingCaptureEye < 0 || !g_fgFence) return;
    if (g_fgFence->GetCompletedValue() < g_pendingCaptureFence) return;
    const int justWroteEye = g_pendingCaptureEye;  // captured before any reset path below

    if (!g_readback) {
        Log("[framegen] capture eye=%d -> no readback buffer (EnsureReadback never succeeded)\n",
            g_pendingCaptureEye);
        CyberpunkVR_FrameGenCaptureLastOk = 0;
        g_pendingCaptureEye = -1;
        if (g_captureBothChain && justWroteEye == 0) {
            g_captureBothChain = false;
            CyberpunkVR_FrameGenCaptureRequest = 2;  // still try eye1 even if eye0's write failed
        }
        return;
    }
    void* mapped = nullptr;
    // NOT rowPitch*height: GetCopyableFootprints' totalBytes (what the buffer was actually sized
    // to) omits the trailing padding after the last row -- rowPitch*height overshoots it by
    // (rowPitch - unpadded row bytes), which Map() rejects with E_INVALIDARG (0x80070057, seen
    // live: 8448*1148=9698304 vs the real 9698112 -- exactly 192 bytes over, one row's padding).
    D3D12_RANGE readRange{0, static_cast<SIZE_T>(g_readbackValidBytes)};
    const HRESULT mapHr = g_readback->Map(0, &readRange, &mapped);
    if (SUCCEEDED(mapHr) && mapped) {
        char path[MAX_PATH];
        const char* modeTag = g_pendingCaptureMode == 1 ? "mv"
                             : g_pendingCaptureMode == 2 ? "input"
                                                          : "color";
        sprintf_s(path, "framegen_capture_eye%d_%s_%llu.bmp", g_pendingCaptureEye, modeTag,
                  static_cast<unsigned long long>(GetTickCount64()));
        const bool ok =
            (g_pendingCaptureMode == 1)
                ? FrameGenCapture_WriteMotionVectorBmp(
                      path, static_cast<const uint8_t*>(mapped),
                      static_cast<uint32_t>(g_readbackRowPitch), g_readbackW, g_readbackH,
                      CyberpunkVR_FrameGenCaptureMvRange)
                : FrameGenCapture_WriteBmp(path, static_cast<const uint8_t*>(mapped),
                                          static_cast<uint32_t>(g_readbackRowPitch), g_readbackW,
                                          g_readbackH);
        D3D12_RANGE writtenRange{0, 0};  // we only read
        g_readback->Unmap(0, &writtenRange);
        CyberpunkVR_FrameGenCaptureLastOk = ok ? 1 : 0;
        Log("[framegen] capture eye=%d -> %s (%s)\n", g_pendingCaptureEye, path,
            ok ? "ok" : "write failed");
    } else {
        CyberpunkVR_FrameGenCaptureLastOk = 0;
        Log("[framegen] capture eye=%d -> Map failed hr=0x%08X rowPitch=%llu w=%d h=%d "
            "readbackSize=%llu\n",
            g_pendingCaptureEye, static_cast<unsigned>(mapHr),
            static_cast<unsigned long long>(g_readbackRowPitch), g_readbackW, g_readbackH,
            static_cast<unsigned long long>(g_readbackValidBytes));
    }
    g_pendingCaptureEye = -1;
    if (g_captureBothChain && justWroteEye == 0) {
        g_captureBothChain = false;
        CyberpunkVR_FrameGenCaptureRequest = 2;  // chain eye1 right away -- same moment, no re-press
    }
}

bool EnsureOwnCommandList(ID3D12Device* dev) {
    if (g_fgCmdList) return true;
    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&g_fgAllocator)))) {
        Log("[framegen] own command allocator create failed\n");
        return false;
    }
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_fgAllocator, nullptr,
                                      IID_PPV_ARGS(&g_fgCmdList)))) {
        Log("[framegen] own command list create failed\n");
        return false;
    }
    g_fgCmdList->Close();  // created open; nothing recorded yet this "frame"
    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fgFence)))) {
        Log("[framegen] own fence create failed\n");
        return false;
    }
    g_fgFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return g_fgFenceEvent != nullptr;
}

bool DispatchSmokeTestUnguarded(ID3D12GraphicsCommandList* cmdList, int eyeSlot,
                                ID3D12Resource* currentBackBuffer, ID3D12Resource* rawDepth,
                                ID3D12Resource* rawMotionVectors, int renderWidth, int renderHeight,
                                int displayWidth, int displayHeight, float motionVectorScaleX,
                                float motionVectorScaleY, bool invertedDepth,
                                float cameraNearMeters, float cameraFarMeters,
                                float cameraFovYRadians, float frameTimeDeltaMs, bool reset) {
    if (!g_backendReady || !g_eyeContextValid[eyeSlot] || !cmdList || !currentBackBuffer ||
        !rawDepth || !rawMotionVectors) {
        return false;
    }
    ID3D12Device* dev = nullptr;
    if (FAILED(cmdList->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return false;

    if (!FrameGenDilate_Init(dev)) {
        dev->Release();
        return false;
    }
    const FrameGenDilateOutputs* dilated =
        FrameGenDilate_Run(cmdList, eyeSlot, rawDepth, rawMotionVectors, renderWidth, renderHeight,
                            motionVectorScaleX, motionVectorScaleY, invertedDepth);
    if (!dilated) {
        dev->Release();
        return false;
    }
    ID3D12Resource* output = EnsureScratchOutput(dev, eyeSlot, displayWidth, displayHeight);
    if (!output) { dev->Release(); return false; }

    // See CreateEyeContexts' comment: force reset on the first dispatch after a (re)create,
    // regardless of what the caller passed -- neither optical flow nor frame interpolation has any
    // valid temporal history yet at that point.
    if (g_eyeContextFirstDispatch[eyeSlot]) {
        reset = true;
        g_eyeContextFirstDispatch[eyeSlot] = false;
    }

    // Real optical flow, dispatched FIRST (frame interpolation below reads its output) -- same
    // order AMD's own FSR3 orchestration uses (src/components/fsr3/ffx_fsr3.cpp,
    // ffxFsr3DispatchFrameGeneration): optical flow computed from the SAME current-frame color this
    // dispatch's currentBackBuffer uses, on the SAME command list. Non-fatal on failure: falls back
    // to the old game-MV-only reconstruction (opticalFlowScale left zeroed) for this eye/frame
    // rather than dropping the whole interpolated frame.
    bool haveOpticalFlow = false;
    int ofW = 0, ofH = 0;
    if (CyberpunkVR_FrameGenOpticalFlowEnable && g_ofContextValid[eyeSlot] &&
        EnsureOpticalFlowResources(dev, eyeSlot, displayWidth, displayHeight, &ofW, &ofH)) {
        FfxOpticalflowDispatchDescription ofDesc{};
        ofDesc.commandList = ffxGetCommandListDX12(cmdList);
        ofDesc.color = WrapResource(currentBackBuffer, FFX_RESOURCE_STATE_COMPUTE_READ);
        ofDesc.opticalFlowVector = WrapResource(g_ofVector[eyeSlot], FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        ofDesc.opticalFlowSCD = WrapResource(g_ofScd[eyeSlot], FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        ofDesc.reset = reset;
        // SRGB, not SCRGB: currentBackBuffer is now the real, already-gamma-encoded SDR displayed
        // frame (see CreateEyeContexts' own comment for the full reasoning), not scene-linear HDR.
        ofDesc.backbufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        ofDesc.minMaxLuminance = {0.0f, 1.0f};
        const FfxErrorCode ofErr = ffxOpticalflowContextDispatch(&g_ofContext[eyeSlot], &ofDesc);
        haveOpticalFlow = (ofErr == FFX_OK);
        static unsigned s_ofLogged = 0;
        if (s_ofLogged < 8) {
            ++s_ofLogged;
            Log("[framegen] optical flow dispatch eye=%d -> 0x%08X (%dx%d)\n", eyeSlot,
                static_cast<unsigned>(ofErr), ofW, ofH);
        }
    }
    dev->Release();

    FfxFrameInterpolationDispatchDescription desc{};
    desc.flags = 0;
    desc.commandList = ffxGetCommandListDX12(cmdList);
    desc.displaySize = {static_cast<uint32_t>(displayWidth), static_cast<uint32_t>(displayHeight)};
    desc.renderSize = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
    desc.currentBackBuffer = WrapResource(currentBackBuffer, FFX_RESOURCE_STATE_COMPUTE_READ);
    desc.currentBackBuffer_HUDLess = FfxResource{};  // none yet -- known quality gap, see README note
    desc.output = WrapResource(output, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.dilatedDepth = WrapResource(dilated->dilatedDepth, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.dilatedMotionVectors =
        WrapResource(dilated->dilatedMotionVectors, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.reconstructPrevNearDepth =
        WrapResource(dilated->reconstructPrevNearDepth, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.interpolationRect = {0, 0, displayWidth, displayHeight};
    // Real optical flow when the dispatch above succeeded; opticalFlowScale left zeroed otherwise,
    // which the component gates on to skip registering opticalFlowVector/SceneChangeDetection
    // entirely (Ghidra-confirmed on the game's own shipped DLL) -- falls back to the old game-MV-
    // only reconstruction rather than handing FFX a stale/empty buffer. Scale formula matches AMD's
    // own FSR3 orchestration (ffx_fsr3.cpp): 1/displaySize, constant regardless of the optical flow
    // buffer's own (much smaller, block-downsampled) resolution.
    if (haveOpticalFlow) {
        desc.opticalFlowVector = WrapResource(g_ofVector[eyeSlot], FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        desc.opticalFlowSceneChangeDetection =
            WrapResource(g_ofScd[eyeSlot], FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        desc.opticalFlowBufferSize = {static_cast<uint32_t>(ofW), static_cast<uint32_t>(ofH)};
        desc.opticalFlowScale = {1.0f / static_cast<float>(displayWidth),
                                 1.0f / static_cast<float>(displayHeight)};
    } else {
        desc.opticalFlowVector = FfxResource{};
        desc.opticalFlowSceneChangeDetection = FfxResource{};
        desc.opticalFlowBufferSize = {0, 0};
        desc.opticalFlowScale = {0.0f, 0.0f};
    }
    desc.opticalFlowBlockSize = 8;
    desc.cameraNear = cameraNearMeters;
    desc.cameraFar = cameraFarMeters;
    desc.cameraFovAngleVertical = cameraFovYRadians;
    desc.viewSpaceToMetersFactor = 1.0f;
    desc.frameTimeDelta = frameTimeDeltaMs;
    desc.reset = reset;
    // SRGB, not SCRGB -- see CreateEyeContexts' own comment: currentBackBuffer is the real,
    // already-gamma-encoded SDR displayed frame now, not scene-linear HDR.
    desc.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    desc.minMaxLuminance[0] = 0.0f;
    desc.minMaxLuminance[1] = 1.0f;

    const FfxErrorCode err = ffxFrameInterpolationDispatch(&g_eyeContext[eyeSlot], &desc);
    static unsigned s_logged = 0;
    if (s_logged < 8) {
        ++s_logged;
        Log("[framegen] smoke-test dispatch eye=%d -> 0x%08X (render %dx%d, display %dx%d)\n",
            eyeSlot, static_cast<unsigned>(err), renderWidth, renderHeight, displayWidth,
            displayHeight);
    }
    return err == FFX_OK;
}

}  // namespace

bool FrameGen_Init(ID3D12Device* device, unsigned renderWidth, unsigned renderHeight,
                    unsigned displayWidth, unsigned displayHeight) {
    if (!device) return false;

    if (!g_backendReady) {
        // One shared backend interface for all four contexts (maxContexts=4): 2 eyes x
        // (FfxFrameInterpolationContext + FfxOpticalflowContext), each of which calls the
        // backend's own fpCreateBackendContext once and consumes one of these slots -- sized for 2
        // before optical flow was wired in, which would have overflowed the scratch buffer the
        // moment a 3rd/4th context tried to create.
        const size_t scratchSize = ffxGetScratchMemorySizeDX12(4);
        g_scratchBuffer = std::malloc(scratchSize);
        if (!g_scratchBuffer) {
            Log("[framegen] scratch allocation failed (%zu bytes)\n", scratchSize);
            return false;
        }
        const FfxErrorCode err = ffxGetInterfaceDX12(&g_backendInterface, ffxGetDeviceDX12(device),
                                                     g_scratchBuffer, scratchSize, 4);
        if (err != FFX_OK) {
            Log("[framegen] ffxGetInterfaceDX12 failed 0x%08X\n", static_cast<unsigned>(err));
            std::free(g_scratchBuffer);
            g_scratchBuffer = nullptr;
            return false;
        }
        g_backendReady = true;
    }

    // Grow-only: a context sized for this request already exists, dispatch will pass the ACTUAL
    // per-frame size (<= this) in the dispatch description's renderSize/displaySize, which is what
    // dynamic resolution is meant to do against a fixed max. Only recreate when the request would
    // exceed what is already allocated.
    if (g_eyeContextValid[0] && g_eyeContextValid[1] && renderWidth <= g_contextRenderW &&
        renderHeight <= g_contextRenderH && displayWidth <= g_contextDisplayW &&
        displayHeight <= g_contextDisplayH) {
        return true;
    }

    const unsigned newRenderW = (renderWidth > g_contextRenderW) ? renderWidth : g_contextRenderW;
    const unsigned newRenderH = (renderHeight > g_contextRenderH) ? renderHeight : g_contextRenderH;
    const unsigned newDisplayW = (displayWidth > g_contextDisplayW) ? displayWidth : g_contextDisplayW;
    const unsigned newDisplayH = (displayHeight > g_contextDisplayH) ? displayHeight : g_contextDisplayH;

    DestroyEyeContexts();
    CyberpunkVR_FrameGenReady = 0;
    if (!CreateEyeContexts(newRenderW, newRenderH, newDisplayW, newDisplayH)) return false;

    CyberpunkVR_FrameGenReady = 1;
    Log("[framegen] FSR3 frame-interpolation contexts (re)created for both eyes (%ux%u -> %ux%u)\n",
        newRenderW, newRenderH, newDisplayW, newDisplayH);
    return true;
}

bool FrameGen_DispatchSmokeTest(ID3D12GraphicsCommandList* cmdList, int eyeSlot,
                                 ID3D12Resource* currentBackBuffer, ID3D12Resource* rawDepth,
                                 ID3D12Resource* rawMotionVectors, int renderWidth, int renderHeight,
                                 int displayWidth, int displayHeight, float motionVectorScaleX,
                                 float motionVectorScaleY, bool invertedDepth,
                                 float cameraNearMeters, float cameraFarMeters,
                                 float cameraFovYRadians, float frameTimeDeltaMs, bool reset) {
    if (CyberpunkVR_FrameGenDisabled || eyeSlot < 0 || eyeSlot > 1) return false;
    __try {
        return DispatchSmokeTestUnguarded(cmdList, eyeSlot, currentBackBuffer, rawDepth,
                                          rawMotionVectors, renderWidth, renderHeight, displayWidth,
                                          displayHeight, motionVectorScaleX, motionVectorScaleY,
                                          invertedDepth, cameraNearMeters, cameraFarMeters,
                                          cameraFovYRadians, frameTimeDeltaMs, reset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CyberpunkVR_FrameGenDisabled = 1;
        Log("[framegen] smoke-test dispatch FAULTED (SEH) -- disabled for the rest of this run.\n");
        return false;
    }
}

void FrameGen_CapturePendingEye(int eyeSlot, ID3D12Resource* currentBackBuffer,
                                ID3D12Resource* rawDepth, ID3D12Resource* rawMotionVectors,
                                int renderWidth, int renderHeight, int displayWidth,
                                int displayHeight, float motionVectorScaleX,
                                float motionVectorScaleY, bool invertedDepth,
                                float cameraNearMeters, float cameraFarMeters,
                                float cameraFovYRadians, float frameTimeDeltaMs, bool reset) {
    if (eyeSlot < 0 || eyeSlot > 1 || CyberpunkVR_FrameGenDisabled) return;
    if (!currentBackBuffer || !rawDepth || !rawMotionVectors || renderWidth <= 0 ||
        renderHeight <= 0 || displayWidth <= 0 || displayHeight <= 0) {
        return;
    }
    PendingEye& p = g_pending[eyeSlot];
    p.currentBackBuffer = currentBackBuffer;
    p.rawDepth = rawDepth;
    p.rawMotionVectors = rawMotionVectors;
    p.renderWidth = renderWidth;
    p.renderHeight = renderHeight;
    p.displayWidth = displayWidth;
    p.displayHeight = displayHeight;
    p.motionVectorScaleX = motionVectorScaleX;
    p.motionVectorScaleY = motionVectorScaleY;
    p.invertedDepth = invertedDepth;
    p.cameraNearMeters = cameraNearMeters;
    p.cameraFarMeters = cameraFarMeters;
    p.cameraFovYRadians = cameraFovYRadians;
    p.frameTimeDeltaMs = frameTimeDeltaMs;
    p.reset = reset;
    p.valid = true;
}

// Load-bearing now, not diagnostic-only -- see g_colorSnapshot's comment for the full story. Runs
// EVERY eligible eval call (both eyes, every frame): one tiny compute pass we fully control (one
// SetDescriptorHeaps/RootSig/PSO/Dispatch, exactly like DLSS5-NR's own NGX Evaluate() call already
// does on this same list, every frame, without incident) that copies Color into our OWN texture
// while it is still guaranteed valid, so the REAL FFX dispatch at Present time reads a stable copy
// instead of whatever CP2077 has done to the live resource by then. Returns the snapshot resource
// (to hand to FrameGen_CapturePendingEye as currentBackBuffer) or nullptr on any failure -- caller
// falls back to the live resource so a snapshot hiccup degrades to the old (known-broken-but-not-
// crashing) behavior rather than dropping the frame's dispatch entirely.
ID3D12Resource* FrameGen_SnapshotColorAtEvalTime(ID3D12GraphicsCommandList* cmdList,
                                                  ID3D12Device* dev, int eyeSlot,
                                                  ID3D12Resource* colorRes, int w, int h) {
    if (eyeSlot < 0 || eyeSlot > 1 || !cmdList || !dev || !colorRes || w <= 0 || h <= 0) {
        return nullptr;
    }
    __try {
        if (!g_passthroughCopy.Init(dev)) return nullptr;
        ID3D12Resource* snapshot = EnsureColorSnapshot(dev, eyeSlot, w, h);
        if (!snapshot) return nullptr;
        g_passthroughCopy.Run(dev, cmdList, colorRes, snapshot, w, h);
        return snapshot;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[framegen] SnapshotColorAtEvalTime FAULTED (SEH) eye=%d\n", eyeSlot);
        return nullptr;
    }
}

namespace {

bool RunPendingDispatchesUnguarded() {
    if (!g_pending[0].valid && !g_pending[1].valid) return false;

    // Whichever eye has a capture gives us a device; both eyes render on the same device.
    ID3D12Resource* anyRes =
        g_pending[0].valid ? g_pending[0].currentBackBuffer : g_pending[1].currentBackBuffer;
    ID3D12Device* dev = nullptr;
    if (FAILED(anyRes->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return false;
    ID3D12CommandQueue* queue = CyberpunkVR_GetGameQueue();
    if (!queue || !EnsureOwnCommandList(dev)) {
        dev->Release();
        return false;
    }

    // Wait for the PREVIOUS submission to finish before touching the allocator again -- Reset()
    // on an allocator whose command list is still executing on the GPU is undefined behaviour.
    // Bounded: if the GPU is somehow still chewing on last frame's dilate+dispatch after 200 ms,
    // skip this frame's rather than block the game's own Present indefinitely.
    if (g_fgFenceSubmitted > 0 && g_fgFence->GetCompletedValue() < g_fgFenceSubmitted) {
        g_fgFence->SetEventOnCompletion(g_fgFenceSubmitted, g_fgFenceEvent);
        if (WaitForSingleObject(g_fgFenceEvent, 200) != WAIT_OBJECT_0) {
            Log("[framegen] previous dispatch still running after 200ms -- skipping this frame\n");
            dev->Release();
            return false;
        }
    }
    // The wait above (or the fence already being at rest) covers any capture copy queued last
    // round too -- safe to Map and write it out now, before we reuse the allocator/list.
    WritePendingCaptureIfReady();

    g_fgAllocator->Reset();
    g_fgCmdList->Reset(g_fgAllocator, nullptr);

    // Captured ONCE for both eyes: both are dispatches for the SAME real Present, so both must be
    // tagged with the SAME serial for FrameGen_GetPresentableEye's match-by-serial lookup to work.
    // See PeekNextPresentSerial's own doc comment: called AFTER this frame's own OnPresent already
    // ran (Hooks/SwapChain.cpp calls us after it), so this returns the SAME serial OnPresent's own
    // capture just used, not "the next one" despite the name.
    const uint64_t presentSerialForThisFrame =
        CyberpunkVR_FrameGenPresentEnable ? CyberpunkVR_PeekNextPresentSerial() : 0;

    bool anyOk = false;
    for (int eye = 0; eye < 2; ++eye) {
        PendingEye& p = g_pending[eye];
        if (!p.valid) continue;
        p.valid = false;  // consume -- a stale capture must not be redispatched next Present

        // Re-resolve the color input fresh, right now -- NOT the pointer NgxEvalPassthrough.cpp
        // captured at eval time. `eye` is already MAIN=0/VRCAM=1 by the convention established
        // with CyberpunkVR_ThreadIsVrcamNgxEval. This runs from FrameGen_RunPendingDispatches,
        // which Hooks/SwapChain.cpp now calls AFTER OpenXRManager::Get().OnPresent(swapChain) --
        // so THIS frame's own true final composite has already been published into
        // m_monoPool/m_vrcamEyePool by the time we ask. Falls back to the eval-time pointer
        // (one real Present stale) only if the fresh fetch fails, rather than dropping the
        // dispatch entirely.
        {
            ID3D12Resource* freshTex = nullptr;
            unsigned long long freshSerial = 0;
            const bool freshOk = (eye == 1)
                ? (CyberpunkVR_GetVrcamEyeFinalFrame(&freshTex, &freshSerial) != 0)
                : (CyberpunkVR_GetMainEyeFinalFrame(&freshTex, &freshSerial) != 0);
            if (freshOk && freshTex) {
                p.currentBackBuffer = freshTex;
            }
        }

        if (!FrameGen_Init(dev, static_cast<unsigned>(p.renderWidth),
                           static_cast<unsigned>(p.renderHeight),
                           static_cast<unsigned>(p.displayWidth),
                           static_cast<unsigned>(p.displayHeight))) {
            continue;
        }
        const bool ok = DispatchSmokeTestUnguarded(
            g_fgCmdList, eye, p.currentBackBuffer, p.rawDepth, p.rawMotionVectors, p.renderWidth,
            p.renderHeight, p.displayWidth, p.displayHeight, p.motionVectorScaleX,
            p.motionVectorScaleY, p.invertedDepth, p.cameraNearMeters, p.cameraFarMeters,
            p.cameraFovYRadians, p.frameTimeDeltaMs, p.reset);
        ++CyberpunkVR_FrameGenDispatches;
        CyberpunkVR_FrameGenLastResult = ok ? 0 : 1;
        anyOk = anyOk || ok;

        if (ok && CyberpunkVR_FrameGenPresentEnable) {
            PublishPresentableEye(dev, g_fgCmdList, eye, g_scratchOutput[eye], p.displayWidth,
                                  p.displayHeight, presentSerialForThisFrame);
        }

        // One-shot debug capture: consume the request only once it actually has a dispatched
        // frame to copy from, so a request made before FrameGen is ready just waits. Raw-MV mode
        // reads FrameGenDilate's own output directly -- FFX never touches it.
        // Request==3 means "both eyes, same moment" (see g_captureBothChain's comment) -- eye0
        // starts the chain here; eye1 gets Request==2 set by WritePendingCaptureIfReady once eye0's
        // file is actually written, on a later Present, so this same check catches it naturally.
        const bool bothStart = (CyberpunkVR_FrameGenCaptureRequest == 3 && eye == 0);
        if (ok && (CyberpunkVR_FrameGenCaptureRequest == eye + 1 || bothStart)) {
            const auto consumeRequest = [&]() {
                // Always clear Request here, even for bothStart -- leaving it at 3 would re-match
                // bothStart every subsequent frame until eye0's capture actually finishes (level-
                // triggered re-fire, found while writing this), double-arming RecordCaptureCopy
                // against an already in-flight readback. g_captureBothChain alone (not Request)
                // carries "arm eye1 next" across to WritePendingCaptureIfReady.
                if (bothStart) g_captureBothChain = true;
                CyberpunkVR_FrameGenCaptureRequest = 0;
            };
            if (CyberpunkVR_FrameGenCaptureRawInput) {
                // p.currentBackBuffer IS g_colorSnapshot[eye] now (FrameGen_SnapshotColorAtEvalTime
                // populates it fresh every eval, before this Present runs) -- our own resource, own
                // tracked state, no timing dependency left to get wrong. This is what FFX's dispatch
                // above just used as its currentBackBuffer input, so this capture shows EXACTLY what
                // FFX saw, confirmed clean by the user after the eval-time-snapshot fix.
                consumeRequest();
                RecordCaptureCopy(g_fgCmdList, dev, p.currentBackBuffer, p.displayWidth,
                                  p.displayHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
                g_pendingCaptureEye = eye;
                g_pendingCaptureMode = 2;
            } else if (CyberpunkVR_FrameGenCaptureRawMv) {
                consumeRequest();
                const FrameGenDilateOutputs* dilated = FrameGenDilate_GetLastOutputs(eye);
                if (dilated && dilated->dilatedMotionVectors) {
                    RecordCaptureCopy(g_fgCmdList, dev, dilated->dilatedMotionVectors,
                                      p.renderWidth, p.renderHeight, DXGI_FORMAT_R16G16_FLOAT);
                    g_pendingCaptureEye = eye;
                    g_pendingCaptureMode = 1;
                }
            } else {
                consumeRequest();
                RecordCaptureCopy(g_fgCmdList, dev, g_scratchOutput[eye], p.displayWidth,
                                  p.displayHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
                g_pendingCaptureEye = eye;
                g_pendingCaptureMode = 0;
            }
        }
    }

    g_fgCmdList->Close();
    dev->Release();
    if (!anyOk) return false;  // nothing recorded worth submitting (both eyes skipped/failed init)

    ID3D12CommandList* lists[] = {g_fgCmdList};
    queue->ExecuteCommandLists(1, lists);
    ++g_fgFenceValue;
    queue->Signal(g_fgFence, g_fgFenceValue);
    g_fgFenceSubmitted = g_fgFenceValue;
    if (g_pendingCaptureEye >= 0) g_pendingCaptureFence = g_fgFenceValue;
    return true;
}

}  // namespace

// Consumer side, called from the XR submit thread (OpenXRFrameLoop.cpp), NOT this file's own
// Present-hook thread -- g_presentPoolMutex is the only thing making that safe. Returns the pool
// slot for `eye` tagged with exactly `serial` (the real frame it interpolated from), or nullptr if
// none matches (frame-gen off, not ready yet, or already consumed/overwritten -- all the same
// "nothing to show, caller falls back to its own stale-resubmit copy" case to the caller).
// Deliberately no AddRef/refcount dance: the caller CopyResource's from this pointer moments later,
// on the SAME queue FrameGen submitted through (see [[vr-framegen-dispatch-fixed]] memory, queue-
// identity note) -- a torn read is possible only in the rare instant this pool is being resized
// (backbuffer format/size change), same accepted risk as this file's own other "not worth the
// extra complexity for a first version" calls.
extern "C" ID3D12Resource* CyberpunkVR_FrameGen_GetPresentableEye(int eye, uint64_t serial) {
    if (eye < 0 || eye > 1 || serial == 0) return nullptr;
    std::lock_guard<std::mutex> lock(g_presentPoolMutex);
    for (int slot = 0; slot < kPresentPoolSlots; ++slot) {
        if (g_presentPoolSerial[eye][slot] == serial) return g_presentPool[eye][slot];
    }
    return nullptr;
}

void FrameGen_RunPendingDispatches() {
    if (CyberpunkVR_FrameGenDisabled) return;
    // Ticks every Present regardless of anything else below -- gives the "Save interpolated
    // frame" buttons time to let the user close the F10 menu before the real capture arms, since
    // the button lives inside the very menu that would otherwise contaminate the shot.
    if (CyberpunkVR_FrameGenCaptureDelayFrames > 0) {
        if (--CyberpunkVR_FrameGenCaptureDelayFrames == 0) {
            CyberpunkVR_FrameGenCaptureRequest = CyberpunkVR_FrameGenCapturePendingChoice;
        }
    }
    __try {
        RunPendingDispatchesUnguarded();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CyberpunkVR_FrameGenDisabled = 1;
        Log("[framegen] RunPendingDispatches FAULTED (SEH) -- disabled for the rest of this run.\n");
    }
}

void FrameGen_Shutdown() {
    for (int eye = 0; eye < 2; ++eye) {
        if (g_eyeContextValid[eye]) {
            ffxFrameInterpolationContextDestroy(&g_eyeContext[eye]);
            g_eyeContextValid[eye] = false;
        }
        if (g_ofContextValid[eye]) {
            ffxOpticalflowContextDestroy(&g_ofContext[eye]);
            g_ofContextValid[eye] = false;
        }
        if (g_scratchOutput[eye]) {
            g_scratchOutput[eye]->Release();
            g_scratchOutput[eye] = nullptr;
        }
        g_scratchOutputW[eye] = g_scratchOutputH[eye] = 0;
        if (g_colorSnapshot[eye]) {
            g_colorSnapshot[eye]->Release();
            g_colorSnapshot[eye] = nullptr;
        }
        g_colorSnapshotW[eye] = g_colorSnapshotH[eye] = 0;
        if (g_ofVector[eye]) { g_ofVector[eye]->Release(); g_ofVector[eye] = nullptr; }
        g_ofVectorW[eye] = g_ofVectorH[eye] = 0;
        if (g_ofScd[eye]) { g_ofScd[eye]->Release(); g_ofScd[eye] = nullptr; }
        g_pending[eye] = PendingEye{};
    }
    {
        std::lock_guard<std::mutex> lock(g_presentPoolMutex);
        for (auto& eyeSlots : g_presentPool) {
            for (auto& res : eyeSlots) {
                if (res) { res->Release(); res = nullptr; }
            }
        }
        for (auto& eyeSerials : g_presentPoolSerial) {
            for (auto& s : eyeSerials) s = 0;
        }
        for (auto& eyeFlags : g_presentPoolInCopySrc) {
            for (auto& f : eyeFlags) f = false;
        }
        g_presentPoolW = g_presentPoolH = 0;
        g_presentPoolFormat = DXGI_FORMAT_UNKNOWN;
    }
    if (g_presentBlit.heap) { g_presentBlit.heap->Release(); g_presentBlit.heap = nullptr; }
    if (g_presentBlit.pso) { g_presentBlit.pso->Release(); g_presentBlit.pso = nullptr; }
    if (g_presentBlit.rootSig) { g_presentBlit.rootSig->Release(); g_presentBlit.rootSig = nullptr; }
    if (g_fgCmdList) { g_fgCmdList->Release(); g_fgCmdList = nullptr; }
    if (g_fgAllocator) { g_fgAllocator->Release(); g_fgAllocator = nullptr; }
    if (g_fgFence) { g_fgFence->Release(); g_fgFence = nullptr; }
    if (g_fgFenceEvent) { CloseHandle(g_fgFenceEvent); g_fgFenceEvent = nullptr; }
    g_fgFenceValue = g_fgFenceSubmitted = 0;
    if (g_readback) { g_readback->Release(); g_readback = nullptr; }
    g_readbackCapacity = 0; g_readbackValidBytes = 0; g_readbackW = g_readbackH = 0;
    g_pendingCaptureEye = -1;
    if (g_passthroughCopy.heap) { g_passthroughCopy.heap->Release(); g_passthroughCopy.heap = nullptr; }
    if (g_passthroughCopy.pso) { g_passthroughCopy.pso->Release(); g_passthroughCopy.pso = nullptr; }
    if (g_passthroughCopy.rootSig) { g_passthroughCopy.rootSig->Release(); g_passthroughCopy.rootSig = nullptr; }
    if (g_scratchBuffer) {
        std::free(g_scratchBuffer);
        g_scratchBuffer = nullptr;
    }
    g_backendReady = false;
    g_contextRenderW = g_contextRenderH = g_contextDisplayW = g_contextDisplayH = 0;
    CyberpunkVR_FrameGenReady = 0;
}
