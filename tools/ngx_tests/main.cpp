#include "Hooks/Ngx.hpp"
#include "Framegen/Inputs.hpp"
#include <dxgi1_4.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using Microsoft::WRL::ComPtr;
extern "C" int CyberpunkVR_RuntimeDiagnostics = 0;
extern "C" uint32_t CyberpunkVR_NgxLegacyCapture;
volatile int g_verboseLog = 0;
void Log(const char*, ...) {}

using TagFn = uint32_t (*)(const void*, const void*, uint32_t, void*);
extern TagFn g_origSlSetTag;
uint32_t HookedSlSetTag(const void*, const void*, uint32_t, void*);

static void check(bool value) {
    if (!value) { std::fputs("NGX gate check failed\n", stderr); std::abort(); }
}
struct Call { const void* viewport; const void* tags; uint32_t count; void* list; };
static Call inputCall{};
static unsigned inputCalls{}, originalCalls{};
namespace cvr::framegen {
bool InstallInputHooks() { return true; }
void RecordTags(const void* vp, const void* tags, uint32_t count, void* list) {
    inputCall = {vp, tags, count, list};
    ++inputCalls;
}
}
static uint32_t original(const void* vp, const void* tags, uint32_t count, void* list) {
    check(inputCall.viewport == vp && inputCall.tags == tags && inputCall.count == count && inputCall.list == list);
    check(inputCalls == originalCalls + 1);
    ++originalCalls;
    return 0xA551u;
}
static void submit(const void* tags, uint32_t count) {
    check(HookedSlSetTag(reinterpret_cast<void*>(0x1234), tags, count, reinterpret_cast<void*>(0x5678)) == 0xA551u);
}
static ComPtr<ID3D12Resource> texture(ID3D12Device* device, DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 64;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = format;
    ComPtr<ID3D12Resource> result;
    check(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&result))));
    return result;
}
static void checkCaptured(ID3D12Resource* mv, ID3D12Resource* depth) {
    ComPtr<ID3D12Resource> capturedMv, capturedDepth;
    capturedMv.Attach(NgxAcquireMotionVectors());
    capturedDepth.Attach(NgxAcquireDepth());
    check(capturedMv.Get() == mv && capturedDepth.Get() == depth);
    check(NgxGetMvWidth() == (mv ? 64u : 0u));
    check(NgxGetMvHeight() == (mv ? 64u : 0u));
    check(NgxGetMvFormat() == (mv ? unsigned(DXGI_FORMAT_R16G16_FLOAT) : 0u));
}
int main() {
    g_origSlSetTag = original;
    // With diagnostics off, even an unreadable tag pointer must simply reach
    // the real framegen observer and original API without a legacy probe.
    submit(reinterpret_cast<void*>(1), 1);
    submit(nullptr, 0);
    checkCaptured(nullptr, nullptr);

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    check(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
    check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    check(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    auto mv = texture(device.Get(), DXGI_FORMAT_R16G16_FLOAT);
    auto depth = texture(device.Get(), DXGI_FORMAT_R32_TYPELESS);
    alignas(8) std::array<uint8_t, 128> mvResource{}, depthResource{}, tags{};
    auto* mvPtr = mv.Get(); auto* depthPtr = depth.Get();
    std::memcpy(mvResource.data() + 0x28, &mvPtr, sizeof(mvPtr));
    std::memcpy(depthResource.data() + 0x28, &depthPtr, sizeof(depthPtr));
    const void* resource = mvResource.data();
    std::memcpy(tags.data() + 0x20, &resource, sizeof(resource));
    uint32_t type = 1;
    std::memcpy(tags.data() + 0x28, &type, sizeof(type));
    resource = depthResource.data();
    std::memcpy(tags.data() + 64 + 0x20, &resource, sizeof(resource));

    // Exercise diagnostic on/off and A/B override transitions repeatedly. Each
    // transition must preserve forwarding and release both retained snapshots.
    for (unsigned i = 0; i != 32; ++i) {
        CyberpunkVR_RuntimeDiagnostics = i & 1;
        CyberpunkVR_NgxLegacyCapture = !(i & 1);
        submit(tags.data(), 2);
        checkCaptured(mv.Get(), depth.Get());
        submit(tags.data(), 2);
        checkCaptured(mv.Get(), depth.Get());
        CyberpunkVR_RuntimeDiagnostics = CyberpunkVR_NgxLegacyCapture = 0;
        submit(reinterpret_cast<void*>(1), 1);
        checkCaptured(nullptr, nullptr);
    }
    check(inputCalls == originalCalls && originalCalls == 98);
    std::puts("NGX diagnostic gating, resource release and all-argument/result forwarding passed");
}
