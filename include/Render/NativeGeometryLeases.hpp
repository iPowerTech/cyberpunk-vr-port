#pragma once
#include "Render/NativeIaBinding.hpp"
#include "Render/NativeStereoProbe.hpp"
#include "Render/StereoSceneState.hpp"
#include <atomic>
#include <span>
#include <vector>

namespace cvr::stereo::geometry {
struct ResourceRecord {uint64_t resource{},gpu{},bytes{};uint32_t heapType{},flags{};};
struct DrawLeases {
    uint32_t sequence{},index{},usedMask{},ownedMask{};
    std::array<uint32_t,16> vertices{};
    uint32_t flags{},frame{},side{},reserved{};
};
struct DrawReadStates {uint32_t sequence{};std::array<ReadState,17> buffers;}; // index, then VB0..15
static_assert(sizeof(DrawReadStates)==208);
static_assert(sizeof(ResourceRecord)==32 && sizeof(DrawLeases)==96);
void Begin(bool enabled);
// Live IA ownership without diagnostic draw arrays, instance copies or freezes.
// Exclusive with Begin(true); called at the same frame/recording boundaries.
void BeginBindings();
void EndBindings();
void End();
void ClearBindings();
void Reset(ID3D12GraphicsCommandList*);
void Index(ID3D12GraphicsCommandList*,const D3D12_INDEX_BUFFER_VIEW*);
void Vertices(ID3D12GraphicsCommandList*,UINT start,UINT count,const D3D12_VERTEX_BUFFER_VIEW*);
void IndexLease(ID3D12GraphicsCommandList*,const D3D12_INDEX_BUFFER_VIEW*,BufferLease);
void VertexLeases(ID3D12GraphicsCommandList*,UINT start,UINT count,const D3D12_VERTEX_BUFFER_VIEW*,std::array<BufferLease,16>);
void Snapshot(const native_probe::DrawRecord&,std::span<const uint8_t> instances={},const scene_state::DrawState* state=nullptr,bool freeze=false);
// Strong references, not permission to assume that mutable buffer contents or
// graphics state are still valid at a different GPU position.
struct DrawResources {BufferLease index;std::array<BufferLease,16> vertices;native_probe::DrawRecord draw;std::vector<uint8_t> instances;scene_state::DrawState state;};
bool CopyDraw(uint32_t sequence,DrawResources&);
// Copies only exact currently bound IA views and their strong native leases.
// Handles indirect calls without direct draw args or a diagnostic sequence.
// Caller supplies the current PSO mask and separately certifies content, state,
// allocation and native scheduling. Non-indexed pure DRAW may omit an IB.
bool CopyCurrentBindings(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,bool indexed,DrawResources&);
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_GeometryLeaseSeq,CyberpunkVR_GeometryLeaseCompatible,CyberpunkVR_GeometryLeaseFailure;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_GeometryLeaseDrawCount,CyberpunkVR_GeometryLeaseResourceCount;
__declspec(dllexport) extern uint64_t CyberpunkVR_GeometryLeaseDraws,CyberpunkVR_GeometryLeaseResources;
__declspec(dllexport) extern uint64_t CyberpunkVR_GeometryLeaseReadStates;
}
