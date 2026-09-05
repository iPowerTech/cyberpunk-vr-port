#pragma once

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

// Lifecycle for the per-eye FSR3 frame-interpolation contexts (vr_framegen branch scaffold).
//
// Real motion-vector + depth frame generation, built from AMD's public FidelityFX-SDK (tag
// fsr3-v3.0.4, see CMakeLists.txt) instead of the color-only optical flow OFXR-Bridge does --
// Ghidra confirmed the game's own shipped ffx_frameinterpolation_x64.dll matches this SDK tag
// byte-for-byte, so this is the same ABI without depending on whatever version CDPR ships.
//
// NOT wired into the render path yet. This only proves the SDK links and the create/destroy
// lifecycle is correct -- one shared FfxInterface backend, two FfxFrameInterpolationContext (one
// per eye, since each eye is a fully independent render in this port's real-stereo pipeline). The
// actual per-frame Dispatch needs dilated depth/motion vectors and the Color/MotionVectors/Depth
// capture this reuses from src/Hooks/NgxEvalPassthrough.cpp -- that is the next step.
bool FrameGen_Init(ID3D12Device* device, unsigned renderWidth, unsigned renderHeight,
                    unsigned displayWidth, unsigned displayHeight);
void FrameGen_Shutdown();

// SMOKE TEST ONLY (bisection step "capture + dispatch, discard the result", same doctrine
// NgxEvalPassthrough.cpp's own steps follow): runs the dilate pass (FrameGenDilate_Run) then a real
// ffxFrameInterpolationDispatch for one eye, writing the interpolated frame into a scratch texture
// this module owns. The caller's currentBackBuffer/rawDepth/rawMotionVectors are only ever READ
// (wrapped FFX_RESOURCE_STATE_COMPUTE_READ, no barrier inserted) -- nothing here writes to a
// resource the game or the XR submit path owns, and the result is discarded by design. This is NOT
// wired into VR presentation: no optical-flow input yet (opticalFlowScale left at 0, which the SDK
// gates on to skip that path), and displaySize == renderSize (no FSR upscale).
//
// SEH-guarded internally with the same fault-latch pattern as CyberpunkVR_NrDisabled: a crash here
// disables further attempts for the process lifetime rather than repeating it every frame.
bool FrameGen_DispatchSmokeTest(ID3D12GraphicsCommandList* cmdList, int eyeSlot,
                                 ID3D12Resource* currentBackBuffer, ID3D12Resource* rawDepth,
                                 ID3D12Resource* rawMotionVectors, int renderWidth, int renderHeight,
                                 float motionVectorScaleX, float motionVectorScaleY,
                                 bool invertedDepth, float cameraNearMeters, float cameraFarMeters,
                                 float cameraFovYRadians, float frameTimeDeltaMs, bool reset);

// Records the resources/params for one eye's dispatch WITHOUT touching any command list --
// FrameGen_RunPendingDispatches actually runs it later, on our own dedicated list. Called from
// Hooks/NgxEvalPassthrough.cpp's DLSS-eval hook, where these resource pointers are known-valid but
// the game's command list is the wrong place to run FFX's own multi-pass pipeline on (see the fix
// this replaced: ffx_dx12.cpp's backend leaves its OWN descriptor heap/PSO/root signature bound on
// whatever command list you hand it -- no save/restore -- which corrupted the game's OWN later
// draws on that same list for the rest of the frame, every frame, and is what actually caused the
// GPU hangs on the first live tests). Only stores pointers -- cheap, no GPU work, safe to call
// unconditionally.
void FrameGen_CapturePendingEye(int eyeSlot, ID3D12Resource* currentBackBuffer,
                                ID3D12Resource* rawDepth, ID3D12Resource* rawMotionVectors,
                                int renderWidth, int renderHeight, float motionVectorScaleX,
                                float motionVectorScaleY, bool invertedDepth,
                                float cameraNearMeters, float cameraFarMeters,
                                float cameraFovYRadians, float frameTimeDeltaMs, bool reset);

// Called from the Present hook (Hooks/SwapChain.cpp), AFTER the game has already submitted this
// frame's own command list(s) -- Present never returns before that submission happens, D3D12
// forbids it, so by construction the writes to whatever FrameGen_CapturePendingEye captured are
// already queued on the GPU. Runs both eyes' dilate + ffxFrameInterpolationDispatch on OUR OWN
// command list/allocator (allocated once, reused frame to frame behind a fence wait), then
// ExecuteCommandLists on the game's own queue -- same-queue submission order guarantees our reads
// happen after the game's writes, with no heap/PSO ever shared with a list the game records into.
// SEH-guarded, fault-latches CyberpunkVR_FrameGenDisabled like every other entry point here.
void FrameGen_RunPendingDispatches();
