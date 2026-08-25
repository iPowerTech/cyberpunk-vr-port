// CyberpunkVRPort as a RED4ext plugin -- the replacement for the dxgi.dll proxy.
//
// WHY THIS EXISTS
//
// As dxgi.dll we stood in front of DXGI and owned what we assumed was the process's only
// swapchain. Every path grew around that assumption. The moment a second one appeared -- the
// VRCAM mirror's window -- it broke in a new way on each attempt: our own IAT hooks recursing
// through our module, the overlay drawing into the mirror's backbuffer, a shared surface
// coming back DXGI_ERROR_ACCESS_DENIED. None of those are mirror bugs; they are consequences
// of being the proxy. The build that actually ran the mirror was a red4ext plugin with no
// proxy at all, so this puts us back in that shape.
//
// WHAT CHANGES, AND WHAT DOES NOT
//
// Almost nothing moves. The engine hooks, OpenXR submit, capture, overlay and stereo module
// are the same translation units; they never needed to be a proxy, they only needed a device,
// a queue and Present. Only the way those three are acquired changes:
//
//   device + queue : sync_stereo hooks d3d12!D3D12CreateDevice before the game creates its
//                    device and keeps both. The plugin waits for them instead of being handed
//                    them by CreateSwapChain.
//   Present        : the swapchain vtable is shared process-wide, so a throwaway swapchain
//                    exposes the same table the game presents through.
//   game window    : learned from the first Present that is not one of ours.
//
// DEPLOYMENT: this DLL goes in bin\x64\plugins\cyberpunkvrport\, and bin\x64\dxgi.dll must be
// REMOVED. Running both means two copies of every hook fighting for the same addresses.

#include <windows.h>
#include <d3d12.h>
#include <atomic>

#include <RED4ext/RED4ext.hpp>

#include "Runtimes/OpenXRManager.hpp"
#include "Overlay/ImGuiOverlay.hpp"

extern void Log(const char* fmt, ...);

// Runtime paths (log file, vrport.ini, CET mod folders). Was called from DllMain.
extern void InitRuntimePaths();
// One-shot on a fresh install: copy the shipped UserSettings.json over the game's own. Reads and
// sets first_launch in vrport.ini, so it happens exactly once and never touches a player's tuning
// afterwards. As early as we get -- the game may still have read its settings first, in which case
// they take on the next launch.
extern "C" void ApplyFirstLaunchGameSettings();
// The game-hook pass: camera, FOV, LoD, DLSS resolution, XInput. Sleeps ~8 s first, then
// pattern-scans. Unchanged -- none of it depended on being the proxy.
extern DWORD WINAPI WorkerThread(LPVOID);
// Device/queue/Present/overlay wiring, formerly done at CreateSwapChain time.
extern "C" __declspec(dllexport) void CyberpunkVRPort_PluginBootstrap();
// Boots sync_stereo (which installs the D3D12CreateDevice hook that captures device+queue).
extern "C" void CyberpunkVRPort_InitStereo();
extern "C" ID3D12Device*       CyberpunkVR_GetGameDevice();
extern "C" ID3D12CommandQueue* CyberpunkVR_GetGameQueue();
// The script-callable natives (VRBodyBonePos, VRPalmModelPos, the lot). They used to be a SECOND
// RED4ext plugin, CyberpunkVR_Hands.dll; they are now translation units of this one and only their
// entry points had to give -- see the note at the definition.
extern void CyberpunkVR_RegisterHandsNatives();

namespace {

std::atomic<bool> g_started{false};

// The game hooks (camera, FOV, LoD, DLSS resolution, XInput) on their own thread, exactly as
// DllMain used to start them. WorkerThread does its own wait before pattern-scanning.
void StartWorkerThread() {
    if (HANDLE h = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr)) CloseHandle(h);
}

} // namespace

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle, RED4ext::v1::EMainReason reason,
                                        const RED4ext::v1::Sdk*) {
    switch (reason) {
    case RED4ext::v1::EMainReason::Load: {
        if (g_started.exchange(true)) break;
        InitRuntimePaths();
        Log("=== CyberpunkVRPort red4ext plugin loaded (no dxgi proxy) ===\n");
        // FIRST, and deliberately: registering the RTTI callbacks is cheap and has a window. The
        // graphics init below waits on the game's device and can take seconds; doing it first is
        // how the natives would come back nil in Lua.
        CyberpunkVR_RegisterHandsNatives();
        ApplyFirstLaunchGameSettings();
        // Must run before the game's D3D12CreateDevice: this is what captures the device and
        // queue, and what patches the descriptor-heap size the second view needs.
        CyberpunkVRPort_InitStereo();
        // Install the DXGI factory hook NOW, not after waiting for a device: it has to be in
        // place before the game creates its swapchain, because that single call is where the
        // resolution override is applied and where the device, queue, overlay and OpenXR all
        // get wired. Waiting for the device first is what missed it -- the game reached the
        // loading screen with no override and an XR path that was never initialised, and sat
        // there.
        CyberpunkVRPort_PluginBootstrap();
        StartWorkerThread();
        break;
    }
    case RED4ext::v1::EMainReason::Unload:
        // RED4ext unloads the plugin BEFORE the process finishes tearing down the graphics runtime,
        // and nothing here used to take that window (dabinn, TofuExpress fbe336fa). Stopping OpenXR
        // and dropping our overlay's device references while D3D is still alive is the difference
        // between an orderly exit and the runtime calling through objects D3D shutdown has already
        // poisoned -- which is the crash-on-quit.
        OverlaySetDeviceAndQueue(nullptr, nullptr);
        OpenXRManager::Get().Shutdown();
        Log("=== CyberpunkVRPort red4ext plugin unloaded ===\n");
        break;
    }
    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* info) {
    info->name = L"CyberpunkVRPort";
    info->author = L"CyberpunkVRPort";
    info->version = RED4EXT_V1_SEMVER(0, 1, 0);
    // Matches the loader actually installed here (runtime 1.29.1), which predates API v1 and
    // skips plugins that declare it outright -- the testbed plugin had to do the same.
    info->runtime = RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
    info->sdk = RED4EXT_V1_SDK_VERSION_1_0_0_COMPAT_0_5_0;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports() { return RED4EXT_API_VERSION_1_COMPAT_0; }
