// Dred -- what killed the device, when D3D12 will not say.
//
// A device-removed HRESULT on its own carries no information: the call that returned it is usually not
// the call that broke anything. DRED (Device Removed Extended Data) is the engine-side answer -- it
// records auto-breadcrumbs of the last GPU work submitted and the page fault that ended it.
//
// IT MUST BE ENABLED BEFORE ANY DEVICE EXISTS, which is why EnableDredOnce is called from the plugin's
// earliest entry point and not from anywhere that looks like it belongs to the swapchain. Enable it
// after the device is created and it records nothing, silently.
//
// DumpDredOnce fires at most once per session: the first device removal is the one with a cause, and a
// loop of them is the same cause reported repeatedly.

#include <MinHook.h>
#include "Hooks/SwapChainInternal.hpp"
#include <thread>
#include "Hooks/SwapChain.hpp"
#include "Overlay/ImGuiOverlay.hpp"
#include "Hooks/Ngx.hpp"
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <wrl.h>

extern void Log(const char* fmt, ...);
extern volatile int g_verboseLog; // gate per-frame spam (ClipCursor / depth-diag)
extern "C" void PrepareStartupLiveControls();
extern "C" int CyberpunkVR_IsMainViewRecording();
extern "C" int CyberpunkVR_ViewKeyHookActive();
extern "C" void CyberpunkVR_ProfPublish();
extern "C" UINT GetForcedSwapchainWidth();
extern "C" UINT GetForcedSwapchainHeight();
extern "C" UINT GetForcedDisplayModeWidth();
extern "C" UINT GetForcedDisplayModeHeight();
extern "C" UINT GetForcedWindowWidth();
extern "C" UINT GetForcedWindowHeight();
extern "C" int GetMenuMode();

// WE WERE LOSING EVERY DRED DUMP TO A RACE WE COULD NOT WIN FROM Present.
//
// DumpDredOnce used to be reachable from ONE place: the Present hook, when Present itself returned a
// device-removed HRESULT (SwapChain.cpp). CP2077 never gets that far. Its own GPU watchdog notices
// the removal first and goes straight to
//     gpuApiDX12Error.cpp:42  "Gpu Crash for unknown reasons! ... Check if Breadcrumbs or Aftermath
//     logged anything useful."
// which tears the process down. So every one of those crashes shipped with DRED enabled, breadcrumbs
// recorded, and nothing ever read them -- the engine was literally asking for the data we already had.
//
// This watchdog reads it. It polls GetDeviceRemovedReason on the tracked device, off the render
// thread so a hung or blocked render thread cannot silence it, and dumps the moment the device goes.
// 50 ms is chosen against the engine, not against the GPU: the removal has already happened by the
// time either of us can see it, and the only question is who reads DRED before the process dies.
//
// GetDeviceRemovedReason on a live device is a cheap flag read, so this costs nothing until it
// matters. The thread exits after one dump -- DumpDredOnce is once-per-session by design, and a
// removed device produces the same answer forever after.
static std::atomic<bool> g_dredWatchdogStarted{false};

static void StartDredWatchdogOnce() {
    bool expected = false;
    if (!g_dredWatchdogStarted.compare_exchange_strong(expected, true)) {
        return;
    }
    std::thread([] {
        for (;;) {
            Sleep(50);
            Microsoft::WRL::ComPtr<ID3D12Device> device;
            {
                std::lock_guard<std::mutex> lock(g_dredMutex);
                if (g_dredDumped) return;     // Present, or a previous tick, already got it
                device = g_dredDevice;
            }
            if (!device) continue;
            const HRESULT reason = device->GetDeviceRemovedReason();
            if (reason == S_OK) continue;
            Log("[DRED] watchdog: device removed, reason=0x%08X -- dumping before the engine's "
                "own handler tears the process down\n", static_cast<unsigned>(reason));
            DumpDredOnce(nullptr, reason);
            return;
        }
    }).detach();
}

void RememberDredDevice(ID3D12Device* device) {
    if (!device) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_dredMutex);
        if (g_dredDevice.Get() != device) {
            g_dredDevice = device;
            g_dredDumped = false;
            Log("[DRED] Tracking D3D12 device %p\n", device);
        }
    }
    // Outside the lock: the watchdog takes the same mutex on its first tick.
    StartDredWatchdogOnce();
}

void RememberDredDeviceFromSwapChain(IDXGISwapChain* swapChain) {
    if (!swapChain) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    HRESULT hr = swapChain->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
        hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (SUCCEEDED(hr) && backBuffer) {
            hr = backBuffer->GetDevice(IID_PPV_ARGS(&device));
        }
    }

    if (SUCCEEDED(hr) && device) {
        RememberDredDevice(device.Get());
    }
}

// DRED auto-breadcrumbs + page-fault reporting must be enabled BEFORE
// D3D12CreateDevice runs; otherwise GetAutoBreadcrumbsOutput/
// GetPageFaultAllocationOutput return empty data on device-removed and our
// DumpDredOnce() logs nothing useful. Call from each CreateDXGIFactory* entry
// — those run before any D3D12 device is created.
void EnableDredOnce() {
    static std::once_flag s_flag;
    std::call_once(s_flag, []() {
        HMODULE d3d12 = LoadLibraryA("d3d12.dll");
        if (!d3d12) {
            Log("[DRED] LoadLibrary(d3d12.dll) failed; DRED not enabled\n");
            return;
        }
        using PFN_D3D12GetDebugInterface = HRESULT(WINAPI*)(REFIID, void**);
        auto fn = reinterpret_cast<PFN_D3D12GetDebugInterface>(
            GetProcAddress(d3d12, "D3D12GetDebugInterface"));
        if (!fn) {
            Log("[DRED] D3D12GetDebugInterface export missing; DRED not enabled\n");
            return;
        }

        // Prefer Settings1 (Windows 10 2004+) — gives BreadcrumbContext for
        // richer DRED dumps; fall back to base Settings on older Windows.
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings1;
        if (SUCCEEDED(fn(IID_PPV_ARGS(&settings1))) && settings1) {
            settings1->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings1->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            Log("[DRED] Enabled (settings1): AutoBreadcrumbs+PageFault+BreadcrumbContext\n");
            return;
        }
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
        if (SUCCEEDED(fn(IID_PPV_ARGS(&settings))) && settings) {
            settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            Log("[DRED] Enabled (settings): AutoBreadcrumbs+PageFault\n");
            return;
        }
        Log("[DRED] D3D12GetDebugInterface QI for DRED settings failed; DRED not enabled\n");
    });
}

bool IsDeviceRemovedHr(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED ||
        hr == DXGI_ERROR_DEVICE_HUNG ||
        hr == DXGI_ERROR_DEVICE_RESET;
}

void LogDredBreadcrumbs(ID3D12DeviceRemovedExtendedData* dred) {
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
    const HRESULT hr = dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
    if (FAILED(hr)) {
        Log("[DRED] GetAutoBreadcrumbsOutput failed: hr=0x%08X\n", static_cast<unsigned>(hr));
        return;
    }

    unsigned nodeIndex = 0;
    for (const D3D12_AUTO_BREADCRUMB_NODE* node = breadcrumbs.pHeadAutoBreadcrumbNode;
         node != nullptr && nodeIndex < 64;
         node = node->pNext, ++nodeIndex) {
        char listName[256]{};
        char queueName[256]{};
        const UINT last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : UINT_MAX;
        const unsigned op = (node->pCommandHistory && last < node->BreadcrumbCount)
            ? static_cast<unsigned>(node->pCommandHistory[last])
            : UINT_MAX;
        Log("[DRED] Breadcrumb[%u] queue=%s list=%s completed=%u/%u op=%u queuePtr=%p listPtr=%p\n",
            nodeIndex,
            DebugName(node->pCommandQueueDebugNameA, node->pCommandQueueDebugNameW, queueName, sizeof(queueName)),
            DebugName(node->pCommandListDebugNameA, node->pCommandListDebugNameW, listName, sizeof(listName)),
            last,
            node->BreadcrumbCount,
            op,
            node->pCommandQueue,
            node->pCommandList);
    }
}

void LogDredAllocations(const char* label, const D3D12_DRED_ALLOCATION_NODE* head) {
    unsigned index = 0;
    for (const D3D12_DRED_ALLOCATION_NODE* node = head;
         node != nullptr && index < 128;
         node = node->pNext, ++index) {
        char name[256]{};
        Log("[DRED] %s[%u] type=%u name=%s\n",
            label,
            index,
            static_cast<unsigned>(node->AllocationType),
            DebugName(node->ObjectNameA, node->ObjectNameW, name, sizeof(name)));
    }
}

void LogDredPageFault(ID3D12DeviceRemovedExtendedData* dred) {
    D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
    const HRESULT hr = dred->GetPageFaultAllocationOutput(&pageFault);
    if (FAILED(hr)) {
        Log("[DRED] GetPageFaultAllocationOutput failed: hr=0x%08X\n", static_cast<unsigned>(hr));
        return;
    }

    Log("[DRED] PageFaultVA=0x%016llX\n", static_cast<unsigned long long>(pageFault.PageFaultVA));
    LogDredAllocations("ExistingAllocation", pageFault.pHeadExistingAllocationNode);
    LogDredAllocations("RecentFreedAllocation", pageFault.pHeadRecentFreedAllocationNode);
}

void DumpDredOnce(IDXGISwapChain* swapChain, HRESULT presentHr) {
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    {
        std::lock_guard<std::mutex> lock(g_dredMutex);
        if (g_dredDumped) {
            return;
        }
        device = g_dredDevice;
        g_dredDumped = true;
    }

    if (!device && swapChain) {
        swapChain->GetDevice(IID_PPV_ARGS(&device));
    }
    if (!device) {
        Log("[DRED] Present failed with hr=0x%08X but no D3D12 device is tracked\n", static_cast<unsigned>(presentHr));
        return;
    }

    const HRESULT removedReason = device->GetDeviceRemovedReason();
    Log("[DRED] Device removed detected. presentHr=0x%08X reason=0x%08X device=%p\n",
        static_cast<unsigned>(presentHr),
        static_cast<unsigned>(removedReason),
        device.Get());

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    const HRESULT qiHr = device.As(&dred);
    if (FAILED(qiHr) || !dred) {
        Log("[DRED] ID3D12DeviceRemovedExtendedData unavailable: hr=0x%08X\n", static_cast<unsigned>(qiHr));
        return;
    }

    LogDredBreadcrumbs(dred.Get());
    LogDredPageFault(dred.Get());
}
