#include "FrameGen/FrameGenFsr3.hpp"

#include <windows.h>
#include <d3d12.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>

#include <cstdlib>

extern void Log(const char* fmt, ...);

namespace {

void* g_scratchBuffer = nullptr;
FfxInterface g_backendInterface{};
FfxFrameInterpolationContext g_eyeContext[2]{};
bool g_eyeContextValid[2] = {false, false};
bool g_backendReady = false;

}  // namespace

bool FrameGen_Init(ID3D12Device* device, unsigned renderWidth, unsigned renderHeight,
                    unsigned displayWidth, unsigned displayHeight) {
    if (g_backendReady) return true;
    if (!device) return false;

    // One shared backend interface for both eye contexts (maxContexts=2): one scratch allocation,
    // one set of backend callbacks -- the two eyes are independent FfxFrameInterpolationContext
    // instances, not two backends.
    const size_t scratchSize = ffxGetScratchMemorySizeDX12(2);
    g_scratchBuffer = std::malloc(scratchSize);
    if (!g_scratchBuffer) {
        Log("[framegen] scratch allocation failed (%zu bytes)\n", scratchSize);
        return false;
    }
    FfxErrorCode err = ffxGetInterfaceDX12(&g_backendInterface, ffxGetDeviceDX12(device),
                                           g_scratchBuffer, scratchSize, 2);
    if (err != FFX_OK) {
        Log("[framegen] ffxGetInterfaceDX12 failed 0x%08X\n", static_cast<unsigned>(err));
        std::free(g_scratchBuffer);
        g_scratchBuffer = nullptr;
        return false;
    }

    for (int eye = 0; eye < 2; ++eye) {
        FfxFrameInterpolationContextDescription desc{};
        desc.backendInterface = g_backendInterface;
        desc.flags = 0;
        desc.maxRenderSize = {renderWidth, renderHeight};
        desc.displaySize = {displayWidth, displayHeight};
        desc.backBufferFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
        err = ffxFrameInterpolationContextCreate(&g_eyeContext[eye], &desc);
        if (err != FFX_OK) {
            Log("[framegen] ffxFrameInterpolationContextCreate eye=%d failed 0x%08X\n", eye,
                static_cast<unsigned>(err));
            for (int j = 0; j < eye; ++j) {
                ffxFrameInterpolationContextDestroy(&g_eyeContext[j]);
                g_eyeContextValid[j] = false;
            }
            std::free(g_scratchBuffer);
            g_scratchBuffer = nullptr;
            return false;
        }
        g_eyeContextValid[eye] = true;
    }

    g_backendReady = true;
    Log("[framegen] FSR3 frame-interpolation contexts created for both eyes (%ux%u -> %ux%u)\n",
        renderWidth, renderHeight, displayWidth, displayHeight);
    return true;
}

void FrameGen_Shutdown() {
    for (int eye = 0; eye < 2; ++eye) {
        if (g_eyeContextValid[eye]) {
            ffxFrameInterpolationContextDestroy(&g_eyeContext[eye]);
            g_eyeContextValid[eye] = false;
        }
    }
    if (g_scratchBuffer) {
        std::free(g_scratchBuffer);
        g_scratchBuffer = nullptr;
    }
    g_backendReady = false;
}
