#pragma once
#include "Render/GeometrySnapshot.hpp"
#include <memory>
#include <vector>

namespace cvr::stereo::geometry_snapshots {
struct Entry;
struct Submission {std::vector<std::shared_ptr<Entry>> entries;};
struct Record {uint32_t sequence{},status{};int32_t result{};uint32_t reserved{};uint64_t bytes{};};
static_assert(sizeof(Record)==24);
void Begin(bool enabled);
void Capture(const geometry::DrawResources&,const std::array<geometry::ReadState,17>&,bool rebase);
Submission BeforeSubmit(UINT,ID3D12CommandList* const*);
void Submitted(ID3D12CommandQueue*,Submission);
void Poll();
bool CopyPair(UINT left,UINT right,GeometryCommandPair&);
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_GeometryFreezeEnable,CyberpunkVR_GeometryFreezeSeq;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_GeometryFreezeCount,CyberpunkVR_GeometryFreezePending,CyberpunkVR_GeometryFreezeFailure;
__declspec(dllexport) extern uint64_t CyberpunkVR_GeometryFreezeRecords,CyberpunkVR_GeometryFreezeBytes;
}
