#pragma once
#include "Render/NativeIaBinding.hpp"
#include "Render/NativePassTargets.hpp"
#include <d3d12.h>
#include <atomic>
#include <array>

namespace cvr::stereo::native_probe {
struct PipelineRecord {
    uint64_t original{},instanced{},root{};
    int32_t result{};
    uint32_t targets{},depthFormat{},sampleCount{},cameraRoot=UINT_MAX,cameraTableOffset=UINT_MAX;
    std::array<uint32_t,8> formats{};
};
struct DrawRecord {
    uint64_t sequence{},qpc{},list{},pipeline{},root{};
    uint32_t frame{},node{},side{},indices{},instances{},firstIndex{};
    int32_t baseVertex{};
    uint32_t firstInstance{},vertexMask{},tableMask{},cbvMask{},flags{},rtCount{},rtContiguous{};
    D3D12_INDEX_BUFFER_VIEW index{};
    std::array<D3D12_VERTEX_BUFFER_VIEW,16> vertices{};
    std::array<uint64_t,32> tables{},cbvs{};
    std::array<uint64_t,8> targets{};
    uint64_t depth{};
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
    uint64_t instanceResource{},instanceGpuBase{},instanceBytesTotal{};
    uint32_t instanceDataBytes{},instanceHeapType{};
    std::array<uint8_t,64> instanceData{};
    uint32_t thread{},inputSlotMask{}; // flags bit 7: mask came from this native PSO's input layout
};
static_assert(sizeof(PipelineRecord)==80);
static_assert(sizeof(DrawRecord)==1096);
constexpr uint32_t PipelineCapacity=8,DrawCapacity=4096;
// Private shader assets enable candidate PSO creation. Snapshot and GPU probe
// controls default to zero. Explicit GPU probes can replace one bounded group
// with conditional native fallbacks; ordinary recording does not alter draws.
bool Enabled();
bool ObserveBindings(); // Explicit test-process environment, independent of GPU assets.
void RootCreated(ID3D12RootSignature*,const void*,size_t);
void GraphicsCreated(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC&,ID3D12PipelineState*);
void StreamCreated(ID3D12Device*,const D3D12_PIPELINE_STATE_STREAM_DESC&,ID3D12PipelineState*);
void FrameBoundary(bool liveBindings=false);
void Reset(ID3D12GraphicsCommandList*,ID3D12PipelineState*,bool rateKnown=false,bool newRecording=true);
void Pipeline(ID3D12GraphicsCommandList*,ID3D12PipelineState*);
void VertexBuffers(ID3D12GraphicsCommandList*,UINT,UINT,const D3D12_VERTEX_BUFFER_VIEW*);
geometry::BufferLease InstanceLease(ID3D12GraphicsCommandList*);
void IndexBuffer(ID3D12GraphicsCommandList*,const D3D12_INDEX_BUFFER_VIEW*);
void RootSignature(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
void RootTable(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
void RootCbv(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS);
void Predication(ID3D12GraphicsCommandList*,ID3D12Resource*);
void Heaps(ID3D12GraphicsCommandList*,UINT,ID3D12DescriptorHeap* const*);
void Targets(ID3D12GraphicsCommandList*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*);
void Viewports(ID3D12GraphicsCommandList*,UINT,const D3D12_VIEWPORT*);
void Scissors(ID3D12GraphicsCommandList*,UINT,const D3D12_RECT*);
bool Draw(ID3D12GraphicsCommandList*,ID3D12PipelineState*,uint32_t node,int side,UINT indices,UINT instances,UINT first,INT base,UINT startInstance,bool* retainedObserved=nullptr);
bool CurrentState(ID3D12GraphicsCommandList*,DrawRecord&);
}
extern "C" {
__declspec(dllexport) extern uint32_t CyberpunkVR_NativeStereoPipelineBytes,CyberpunkVR_NativeStereoDrawBytes;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_NativeStereoPipelineCount;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeAssetStatus;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeRequest,CyberpunkVR_NativeStereoProbeState,CyberpunkVR_NativeStereoProbeSeq,CyberpunkVR_NativeStereoDrawCount;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_NativeStereoAllDraws;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_NativeStereoBindingHooks;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_NativeStereoLiveBindings,CyberpunkVR_NativeStereoLiveFailure;
__declspec(dllexport) extern cvr::stereo::native_probe::PipelineRecord CyberpunkVR_NativeStereoPipelines[cvr::stereo::native_probe::PipelineCapacity];
__declspec(dllexport) extern cvr::stereo::native_probe::DrawRecord CyberpunkVR_NativeStereoDraws[cvr::stereo::native_probe::DrawCapacity];
__declspec(dllexport) extern uint32_t CyberpunkVR_NativeStereoTargetRecordBytes;
__declspec(dllexport) extern cvr::stereo::pass_targets::Record CyberpunkVR_NativeStereoTargets[cvr::stereo::native_probe::DrawCapacity];
}
