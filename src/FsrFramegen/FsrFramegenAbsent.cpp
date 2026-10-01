// What this fork's FSR3 frame generator looks like when it is not compiled in.
//
// src/FsrFramegen is held out of the build while upstream 0.1.7's native generator is the one being
// evaluated: the two want different FidelityFX SDK revisions and cannot both link (see
// CMakeLists.txt and cmake/NativeFramegen.cmake). But the FSR3 generator is WIRED IN, not optional
// -- Hooks/NgxEvalPassthrough.cpp, Hooks/SwapChain.cpp, Overlay/OverlayPanels.cpp and
// Runtimes/OpenXRFrameLoop.cpp all call into it -- so excluding the implementation alone leaves
// nineteen unresolved externals.
//
// This file is the alternative to editing those four callers, and the four are deliberately left
// alone: every one of them is also a hand-merge target for the upstream integration still to come,
// and #if blocks threaded through them would make that merge harder for no gain.
//
// THE VALUES ARE HONEST, NOT MERELY INERT. Ready stays 0 and Disabled stays 1, which is exactly
// what the F10 panel reads to show the feature as unavailable rather than as present-and-idle. The
// entry points do nothing and say nothing: a log line per frame from a feature that was compiled
// out is noise.
//
// DELETING THIS FILE IS HALF OF TURNING FSR3 BACK ON. The other half is dropping the
// list(FILTER ... EXCLUDE REGEX "/FsrFramegen/") line in CMakeLists.txt and restoring the
// fsr3-v3.0.4 pin that cmake/NativeFramegen.cmake replaced.

#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

extern "C" {
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenReady = 0;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenDisabled = 1;   // compiled out, not faulted off
__declspec(dllexport) uint32_t CyberpunkVR_FrameGenDispatches = 0;
__declspec(dllexport) uint32_t CyberpunkVR_FrameGenLastResult = 0;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenCaptureRequest = 0;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenCaptureLastOk = -1;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenCaptureDelayFrames = 0;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenCapturePendingChoice = 0;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenCaptureRawMv = 0;
__declspec(dllexport) float    CyberpunkVR_FrameGenCaptureMvRange = 0.1f;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenCaptureRawInput = 0;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenOpticalFlowEnable = 0;
__declspec(dllexport) int32_t  CyberpunkVR_FrameGenPresentEnable = 0;

ID3D12Resource* CyberpunkVR_FrameGen_GetPresentableEye(int, uint64_t) { return nullptr; }
}

bool FrameGen_Init(ID3D12Device*, unsigned, unsigned, unsigned, unsigned) { return false; }
void FrameGen_Shutdown() {}
void FrameGen_RunPendingDispatches() {}
void FrameGen_CapturePendingEye(int, ID3D12Resource*, ID3D12Resource*, ID3D12Resource*,
                                int, int, int, int, float, float, bool,
                                float, float, float, float, bool) {}
ID3D12Resource* FrameGen_SnapshotColorAtEvalTime(ID3D12GraphicsCommandList*, ID3D12Device*, int,
                                                 ID3D12Resource*, int, int) { return nullptr; }
bool FrameGen_DispatchSmokeTest(ID3D12GraphicsCommandList*, int, ID3D12Resource*, ID3D12Resource*,
                                ID3D12Resource*, int, int, int, int, float, float, bool,
                                float, float, float, float, bool) { return false; }
