#include "FrameGen/FrameGenFsr3.hpp"
#include "FrameGen/FrameGenDilate.hpp"

#include <windows.h>
#include <d3d12.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>

#include <cstdint>
#include <cstdlib>

extern void Log(const char* fmt, ...);

// UI-visible status (Overlay/OverlayPanels.cpp, FSR3 tab). Same shape as the DLSS5-NR globals in
// NgxEvalPassthrough.cpp: a plain exported int the overlay reads directly and can clear.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenReady = 0;     // 1 once both eye contexts exist
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenDisabled = 0;  // 1 = a fault stopped it

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
    }
    if (g_scratchBuffer) {
        std::free(g_scratchBuffer);
        g_scratchBuffer = nullptr;
    }
    g_backendReady = false;
    g_contextRenderW = g_contextRenderH = g_contextDisplayW = g_contextDisplayH = 0;
    CyberpunkVR_FrameGenReady = 0;
}
