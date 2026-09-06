#include "FrameGen/FrameGenFsr3.hpp"
#include "FrameGen/FrameGenDilate.hpp"
#include "FrameGen/FrameGenCapture.hpp"

#include <windows.h>
#include <d3d12.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern void Log(const char* fmt, ...);
// Set by Stereo/DeviceHooks.cpp the first time the game submits work -- the queue our own
// command list is executed on, so our submission lands in the same GPU timeline as the game's.
extern "C" ID3D12CommandQueue* CyberpunkVR_GetGameQueue();

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

namespace {

void* g_scratchBuffer = nullptr;
FfxInterface g_backendInterface{};
FfxFrameInterpolationContext g_eyeContext[2]{};
bool g_eyeContextValid[2] = {false, false};
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
    }
}

bool CreateEyeContexts(unsigned renderWidth, unsigned renderHeight, unsigned displayWidth,
                       unsigned displayHeight) {
    for (int eye = 0; eye < 2; ++eye) {
        FfxFrameInterpolationContextDescription desc{};
        desc.backendInterface = g_backendInterface;
        desc.flags = 0;
        desc.maxRenderSize = {renderWidth, renderHeight};
        desc.displaySize = {displayWidth, displayHeight};
        desc.backBufferFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
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

    if (!g_readback) {
        Log("[framegen] capture eye=%d -> no readback buffer (EnsureReadback never succeeded)\n",
            g_pendingCaptureEye);
        CyberpunkVR_FrameGenCaptureLastOk = 0;
        g_pendingCaptureEye = -1;
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
        sprintf_s(path, "framegen_capture_eye%d_%s_%llu.bmp", g_pendingCaptureEye,
                  g_pendingCaptureMode == 1 ? "mv" : "color",
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
                                float motionVectorScaleX, float motionVectorScaleY,
                                bool invertedDepth, float cameraNearMeters, float cameraFarMeters,
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
    ID3D12Resource* output = EnsureScratchOutput(dev, eyeSlot, renderWidth, renderHeight);
    dev->Release();
    if (!output) return false;

    FfxFrameInterpolationDispatchDescription desc{};
    desc.flags = 0;
    desc.commandList = ffxGetCommandListDX12(cmdList);
    desc.displaySize = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
    desc.renderSize = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
    desc.currentBackBuffer = WrapResource(currentBackBuffer, FFX_RESOURCE_STATE_COMPUTE_READ);
    desc.currentBackBuffer_HUDLess = FfxResource{};  // none yet -- known quality gap, see README note
    desc.output = WrapResource(output, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.dilatedDepth = WrapResource(dilated->dilatedDepth, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.dilatedMotionVectors =
        WrapResource(dilated->dilatedMotionVectors, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.reconstructPrevNearDepth =
        WrapResource(dilated->reconstructPrevNearDepth, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.interpolationRect = {0, 0, renderWidth, renderHeight};
    // No optical flow yet: opticalFlowScale left zeroed, which the component gates on to skip
    // registering opticalFlowVector/opticalFlowSceneChangeDetection entirely (Ghidra-confirmed on
    // the game's own shipped DLL). Interpolation runs on game motion vectors alone until this is
    // wired -- expect more artifacts on disocclusion/reflections than a full FSR3 integration.
    desc.opticalFlowVector = FfxResource{};
    desc.opticalFlowSceneChangeDetection = FfxResource{};
    desc.opticalFlowBufferSize = {0, 0};
    desc.opticalFlowScale = {0.0f, 0.0f};
    desc.opticalFlowBlockSize = 8;
    desc.cameraNear = cameraNearMeters;
    desc.cameraFar = cameraFarMeters;
    desc.cameraFovAngleVertical = cameraFovYRadians;
    desc.viewSpaceToMetersFactor = 1.0f;
    desc.frameTimeDelta = frameTimeDeltaMs;
    desc.reset = reset;
    desc.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SCRGB;  // scene-linear HDR
    desc.minMaxLuminance[0] = 0.0f;
    desc.minMaxLuminance[1] = 1000.0f;

    const FfxErrorCode err = ffxFrameInterpolationDispatch(&g_eyeContext[eyeSlot], &desc);
    static unsigned s_logged = 0;
    if (s_logged < 8) {
        ++s_logged;
        Log("[framegen] smoke-test dispatch eye=%d -> 0x%08X (%dx%d)\n", eyeSlot,
            static_cast<unsigned>(err), renderWidth, renderHeight);
    }
    return err == FFX_OK;
}

}  // namespace

bool FrameGen_Init(ID3D12Device* device, unsigned renderWidth, unsigned renderHeight,
                    unsigned displayWidth, unsigned displayHeight) {
    if (!device) return false;

    if (!g_backendReady) {
        // One shared backend interface for both eye contexts (maxContexts=2): one scratch
        // allocation, one set of backend callbacks -- the two eyes are independent
        // FfxFrameInterpolationContext instances, not two backends.
        const size_t scratchSize = ffxGetScratchMemorySizeDX12(2);
        g_scratchBuffer = std::malloc(scratchSize);
        if (!g_scratchBuffer) {
            Log("[framegen] scratch allocation failed (%zu bytes)\n", scratchSize);
            return false;
        }
        const FfxErrorCode err = ffxGetInterfaceDX12(&g_backendInterface, ffxGetDeviceDX12(device),
                                                     g_scratchBuffer, scratchSize, 2);
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
                                 float motionVectorScaleX, float motionVectorScaleY,
                                 bool invertedDepth, float cameraNearMeters, float cameraFarMeters,
                                 float cameraFovYRadians, float frameTimeDeltaMs, bool reset) {
    if (CyberpunkVR_FrameGenDisabled || eyeSlot < 0 || eyeSlot > 1) return false;
    __try {
        return DispatchSmokeTestUnguarded(cmdList, eyeSlot, currentBackBuffer, rawDepth,
                                          rawMotionVectors, renderWidth, renderHeight,
                                          motionVectorScaleX, motionVectorScaleY, invertedDepth,
                                          cameraNearMeters, cameraFarMeters, cameraFovYRadians,
                                          frameTimeDeltaMs, reset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CyberpunkVR_FrameGenDisabled = 1;
        Log("[framegen] smoke-test dispatch FAULTED (SEH) -- disabled for the rest of this run.\n");
        return false;
    }
}

void FrameGen_CapturePendingEye(int eyeSlot, ID3D12Resource* currentBackBuffer,
                                ID3D12Resource* rawDepth, ID3D12Resource* rawMotionVectors,
                                int renderWidth, int renderHeight, float motionVectorScaleX,
                                float motionVectorScaleY, bool invertedDepth,
                                float cameraNearMeters, float cameraFarMeters,
                                float cameraFovYRadians, float frameTimeDeltaMs, bool reset) {
    if (eyeSlot < 0 || eyeSlot > 1 || CyberpunkVR_FrameGenDisabled) return;
    if (!currentBackBuffer || !rawDepth || !rawMotionVectors || renderWidth <= 0 ||
        renderHeight <= 0) {
        return;
    }
    PendingEye& p = g_pending[eyeSlot];
    p.currentBackBuffer = currentBackBuffer;
    p.rawDepth = rawDepth;
    p.rawMotionVectors = rawMotionVectors;
    p.renderWidth = renderWidth;
    p.renderHeight = renderHeight;
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

    bool anyOk = false;
    for (int eye = 0; eye < 2; ++eye) {
        PendingEye& p = g_pending[eye];
        if (!p.valid) continue;
        p.valid = false;  // consume -- a stale capture must not be redispatched next Present
        if (!FrameGen_Init(dev, static_cast<unsigned>(p.renderWidth),
                           static_cast<unsigned>(p.renderHeight),
                           static_cast<unsigned>(p.renderWidth),
                           static_cast<unsigned>(p.renderHeight))) {
            continue;
        }
        const bool ok = DispatchSmokeTestUnguarded(
            g_fgCmdList, eye, p.currentBackBuffer, p.rawDepth, p.rawMotionVectors, p.renderWidth,
            p.renderHeight, p.motionVectorScaleX, p.motionVectorScaleY, p.invertedDepth,
            p.cameraNearMeters, p.cameraFarMeters, p.cameraFovYRadians, p.frameTimeDeltaMs,
            p.reset);
        ++CyberpunkVR_FrameGenDispatches;
        CyberpunkVR_FrameGenLastResult = ok ? 0 : 1;
        anyOk = anyOk || ok;

        // One-shot debug capture: consume the request only once it actually has a dispatched
        // frame to copy from, so a request made before FrameGen is ready just waits. Raw-MV mode
        // reads FrameGenDilate's own output directly -- FFX never touches it.
        if (ok && CyberpunkVR_FrameGenCaptureRequest == eye + 1) {
            CyberpunkVR_FrameGenCaptureRequest = 0;
            if (CyberpunkVR_FrameGenCaptureRawMv) {
                const FrameGenDilateOutputs* dilated = FrameGenDilate_GetLastOutputs(eye);
                if (dilated && dilated->dilatedMotionVectors) {
                    RecordCaptureCopy(g_fgCmdList, dev, dilated->dilatedMotionVectors,
                                      p.renderWidth, p.renderHeight, DXGI_FORMAT_R16G16_FLOAT);
                    g_pendingCaptureEye = eye;
                    g_pendingCaptureMode = 1;
                }
            } else {
                RecordCaptureCopy(g_fgCmdList, dev, g_scratchOutput[eye], p.renderWidth,
                                  p.renderHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
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
        if (g_scratchOutput[eye]) {
            g_scratchOutput[eye]->Release();
            g_scratchOutput[eye] = nullptr;
        }
        g_scratchOutputW[eye] = g_scratchOutputH[eye] = 0;
        g_pending[eye] = PendingEye{};
    }
    if (g_fgCmdList) { g_fgCmdList->Release(); g_fgCmdList = nullptr; }
    if (g_fgAllocator) { g_fgAllocator->Release(); g_fgAllocator = nullptr; }
    if (g_fgFence) { g_fgFence->Release(); g_fgFence = nullptr; }
    if (g_fgFenceEvent) { CloseHandle(g_fgFenceEvent); g_fgFenceEvent = nullptr; }
    g_fgFenceValue = g_fgFenceSubmitted = 0;
    if (g_readback) { g_readback->Release(); g_readback = nullptr; }
    g_readbackCapacity = 0; g_readbackValidBytes = 0; g_readbackW = g_readbackH = 0;
    g_pendingCaptureEye = -1;
    if (g_scratchBuffer) {
        std::free(g_scratchBuffer);
        g_scratchBuffer = nullptr;
    }
    g_backendReady = false;
    g_contextRenderW = g_contextRenderH = g_contextDisplayW = g_contextDisplayH = 0;
    CyberpunkVR_FrameGenReady = 0;
}
