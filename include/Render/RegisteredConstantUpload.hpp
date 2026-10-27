#pragma once
#include <d3d12.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <wrl/client.h>
#include "Render/ConstantWriteTimeline.hpp"

namespace cvr::stereo::constants {
class RegisteredUploadScope {
    struct Data;
    std::unique_ptr<Data> data_;
public:
    RegisteredUploadScope(uint32_t handle,uint32_t bytes,const void* source);
    // Explicit destination constructor for isolated GPU/API tests.
    RegisteredUploadScope(ID3D12Resource*,uint32_t bytes,const void* source);
    ~RegisteredUploadScope();
};
void ObserveRegisteredCopy(ID3D12GraphicsCommandList*,ID3D12Resource* destination,uint64_t destinationOffset,
                           ID3D12Resource* source,uint64_t sourceOffset,uint64_t bytes);
// Pure whole-buffer shape check. Heap, flags and allocation alignment grant no
// sampling permission; the existing copy observer still validates those facts.
inline uint64_t CopyResourceBufferBytes(const D3D12_RESOURCE_DESC& destination,const D3D12_RESOURCE_DESC& source)noexcept{
    const auto buffer=[](const D3D12_RESOURCE_DESC& value){return value.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && value.Width &&
        value.Height==1 && value.DepthOrArraySize==1 && value.MipLevels==1 && value.Format==DXGI_FORMAT_UNKNOWN &&
        value.SampleDesc.Count==1 && !value.SampleDesc.Quality && value.Layout==D3D12_TEXTURE_LAYOUT_ROW_MAJOR;};
    return buffer(destination) && buffer(source) && destination.Width==source.Width?destination.Width:0;
}
// Actual CopyResource callback only: compatible buffers become one full-span
// observed copy. Unsupported/unknown shapes invalidate the destination.
void ObserveRegisteredResourceCopy(ID3D12GraphicsCommandList*,ID3D12Resource* destination,ID3D12Resource* source);
void RegisteredResourceCreated(ID3D12Resource*,bool placed,ID3D12Heap* heap=nullptr,uint64_t offset=0,uint64_t allocationBytes=0,
                               std::optional<D3D12_RESOURCE_STATES> initialState=std::nullopt);
struct ResourceCreationInfo {
    uint32_t kind{}; // 0=unknown, 1=committed, 2=placed
    bool initialStateKnown{};
    D3D12_RESOURCE_STATES initialState{};
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<ID3D12Heap> heap;
    uint64_t heapOffset{},allocationBytes{};
};
// Current private API-creation receipt only. Retains the resource/placement heap;
// initialState is historical, NOT its current state, byte/alias/order/copy proof.
// Old and enhanced-barrier callers without a state operand remain state-unknown.
bool QueryResourceCreation(const Microsoft::WRL::ComPtr<ID3D12Resource>& heldResource,ResourceCreationInfo&);
void InvalidateRegistered(ID3D12GraphicsCommandList*,ID3D12Resource*);
void RegisteredBarriers(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
void ResetRegistered(ID3D12GraphicsCommandList*);
void SubmitRegistered(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
// With a receipt, a recording consumer may snapshot a pending producer's
// intended bytes. ValidateRegistered must prove ordered submission afterward.
bool ReadRegistered(ID3D12CommandList*,uint64_t,void*,size_t,uint64_t* receipt=nullptr);
bool ValidateRegistered(uint64_t receipt,ID3D12CommandList*,uint64_t,const void*,size_t);
uint64_t MarkRegisteredRead(ID3D12CommandList*);
void RegisteredReadDraw(uint64_t position,uint32_t sequence,uint64_t family,uint32_t side,uint32_t frame);
void RegisteredReadBinding(uint64_t position,uint32_t visibility,uint32_t shaderRegister,uint64_t address,uint32_t bytes);
ConstantWriteTimeline::Result ResolveRegisteredRead(uint64_t position,ID3D12CommandList*,uint64_t,void*,size_t);
// Position-ledger payload only, additionally matched to the held destination.
// No address-cache fallback; source staging bytes still need GPU verification.
ConstantWriteTimeline::Result ResolveRegisteredResourceRead(uint64_t position,ID3D12CommandList*,
    const Microsoft::WRL::ComPtr<ID3D12Resource>& expectedDestination,uint64_t address,void*,size_t);
class RegisteredResourceIntent {
public:
    uint64_t Payload()const noexcept;
    uint64_t Offset()const noexcept;
    uint64_t ReadPosition()const noexcept;
    ConstantWriteTimeline::Position ProducerPosition()const noexcept;
    std::span<const uint8_t> Bytes()const noexcept;
    explicit operator bool()const noexcept;
private:
    struct Data;std::shared_ptr<const Data> data_;
    friend bool CaptureRegisteredResourceIntent(uint64_t,ID3D12CommandList*,
        const Microsoft::WRL::ComPtr<ID3D12Resource>&,uint64_t,size_t,RegisteredResourceIntent&);
    friend bool ValidateRegisteredResourceIntent(const RegisteredResourceIntent&,ConstantWriteTimeline::Result*);
};
// Original recording point only. Owns provisional producer-intended bytes from
// the exact held destination/range; no latest-address or CPU mapping fallback.
// This grants no GPU contents/copy permission. Compare with the private GPU b4
// snapshot separately after final writer/order validation succeeds.
bool CaptureRegisteredResourceIntent(uint64_t position,ID3D12CommandList*,
    const Microsoft::WRL::ComPtr<ID3D12Resource>& expectedDestination,uint64_t address,size_t bytes,RegisteredResourceIntent&);
// Requires freeze and the same final position-ledger payload ID/offset/bytes.
bool ValidateRegisteredResourceIntent(const RegisteredResourceIntent&,ConstantWriteTimeline::Result* resolution=nullptr);
void FreezeRegistered(); // all captured consumer lists have reached submission
bool RegisteredTracking();
// Independent startup opt-in for source-selector intent tracking. Normal runs
// remain off; this does not enable the legacy constant-bank capture/consumer.
bool RegisteredIntentTracking();
void PollRegisteredIntent();
void RegisterFence(ID3D12Fence*);
void RegisteredSignal(ID3D12CommandQueue*,ID3D12Fence*,uint64_t);
void RegisteredWait(ID3D12CommandQueue*,ID3D12Fence*,uint64_t);
void RegisteredCpuSignal(ID3D12Fence*,uint64_t value=0,uint64_t caller=0,uint32_t nested=0);
void RefreshRegisteredFences();
void ObserveQueueApi(ID3D12CommandQueue*);
std::recursive_mutex& RegisteredQueueOrderMutex();
void ClearRegistered(bool publish=false);
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RegisteredIntentEnable;
__declspec(dllexport) extern uint64_t CyberpunkVR_RegisteredIntentCacheStats[10];
__declspec(dllexport) extern uint64_t CyberpunkVR_RegisteredStructuredIntentStats[2];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RegisteredCopyCount,CyberpunkVR_RegisteredCopyRejected,CyberpunkVR_RegisteredCopyChanged;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RegisteredCopyUnscoped;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RegisteredCopyPendingReads,CyberpunkVR_RegisteredCopyOrderRejected;
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_RegisteredCopyReads;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RegisteredCopyTimelineResults[8],CyberpunkVR_RegisteredCopyTimelineStats[5];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RegisteredCopySignals,CyberpunkVR_RegisteredCopyWaits,CyberpunkVR_RegisteredCopyCpuSignals;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RegisteredCopyTraceSeq,CyberpunkVR_RegisteredCopyTraceCount,CyberpunkVR_RegisteredCopyTraceOverflow;
__declspec(dllexport) extern uint64_t CyberpunkVR_RegisteredCopyTraceData;
}
