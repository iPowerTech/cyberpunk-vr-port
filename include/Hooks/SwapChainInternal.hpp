#pragma once

// ================================================================================================
// What the swapchain family hands itself, after src/Hooks/SwapChain.cpp was split by subject.
//
// Same rule as the other internal headers: a name belongs here when a file OTHER than the one defining
// it uses that name. The boot order is the thing to keep in view -- DRED must be enabled before any
// device exists, the OS hooks before the window is shown, and the depth capture after the device.
// ================================================================================================

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <unordered_map>

#include <mutex>
#include <cstdint>
using EnumOutputsFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter*, UINT, IDXGIOutput**);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using GetDisplayModeList1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput1*, DXGI_FORMAT, UINT, UINT*, DXGI_MODE_DESC1*);
using GetDisplayModeListFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput*, DXGI_FORMAT, UINT, UINT*, DXGI_MODE_DESC*);
bool IsDeviceRemovedHr(HRESULT hr);
bool PatchVtableMethod(void** vtable, size_t slot, void* hook);
const char* DebugName(const char* ansiName, const wchar_t* wideName, char* buffer, size_t bufferSize);
extern HWND g_gameHwnd;
extern Microsoft::WRL::ComPtr<ID3D12Device> g_dredDevice;
extern bool g_cursorClipped;
extern bool g_dredDumped;
extern std::atomic<void*> g_eclSeenQueues[16];
extern std::mutex g_dredMutex;
extern std::mutex g_vtableMutex;
extern std::unordered_map<uintptr_t, void*> g_originalVtableMethods;
void DumpDredOnce(IDXGISwapChain* swapChain, HRESULT presentHr);
void EnableDredOnce();
void InstallAdapterHook(IDXGIAdapter* adapter);
void InstallCommandQueueDiagHook(ID3D12CommandQueue* queue);
void InstallDepthCaptureHooks(ID3D12Device* device);
void InstallOSHooks();
void InstallOutputHook(IDXGIOutput* output);
void RememberDredDevice(ID3D12Device* device);
void RememberDredDeviceFromSwapChain(IDXGISwapChain* swapChain);
void UpdateCursorCapture(HWND hwnd);

// Brace-initialised atomics. Worth a line of its own: the pass that generated the rest of this header
// matches a name followed by `(`, `[`, `=` or `;` and these are followed by `{`, so it reported them as
// unresolvable while the build failed on them. Copied from the definitions in DepthCapture.cpp.
#include <atomic>
extern std::atomic<uint64_t> g_eclTotalCalls;
extern std::atomic<uint64_t> g_eclTotalLists;
extern std::atomic<uint64_t> g_eclPresentCalls;
extern std::atomic<uint64_t> g_eclPresentLists;
extern std::atomic<uint32_t> g_eclDistinctQueues;
extern std::atomic<uint32_t> g_presentHeight;

extern std::atomic<ID3D12CommandQueue*> g_sceneDepthWriterQueue;
extern std::atomic<ID3D12Resource*> g_sceneDepthRes;
extern std::atomic<UINT> g_sceneDepthFmt;
extern std::atomic<UINT> g_sceneDepthH;
extern std::atomic<UINT> g_sceneDepthState;
extern std::atomic<UINT> g_sceneDepthW;
extern std::atomic<bool>     g_sceneDepthSeen;
extern std::atomic<uint32_t> g_presentWidth;
extern std::atomic<uint32_t> g_sceneDepthMissFrames;
extern std::atomic<uint64_t> g_sceneDepthArea;
extern std::atomic<void*> g_presentQueue;
uintptr_t MakeVtableSlotKey(void** vtable, size_t slot);

// LAST IN THE FILE, deliberately. A template is compiled where it is instantiated, so every name it
// uses must already be declared above it -- putting it at the top produced four "undeclared
// identifier" errors inside the header itself.
// A TEMPLATE, so the definition and not a declaration -- DisplayModes.cpp instantiates it with two
// different function-pointer types. The map it reads is declared above (defined in SwapChain.cpp).
template <typename T>
T GetOriginalMethod(void** vtable, size_t slot) {
    std::lock_guard<std::mutex> lock(g_vtableMutex);
    auto it = g_originalVtableMethods.find(MakeVtableSlotKey(vtable, slot));
    return it != g_originalVtableMethods.end() ? reinterpret_cast<T>(it->second) : nullptr;
}
