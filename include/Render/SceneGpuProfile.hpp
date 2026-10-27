#pragma once
#include <d3d12.h>
#include <atomic>
#include <memory>
#include <vector>

namespace cvr::gpu::scene_profile {
struct Batch;
using Submission=std::vector<std::shared_ptr<Batch>>;
extern std::atomic<bool> active;
void Mark(ID3D12GraphicsCommandList*,uint32_t node,int side,ID3D12PipelineState*,uint32_t detail=0);
void EndRange(ID3D12GraphicsCommandList*);
void Close(ID3D12GraphicsCommandList*);
void Reset(ID3D12GraphicsCommandList*);
Submission BeforeSubmit(UINT,ID3D12CommandList* const*);
void Submitted(ID3D12CommandQueue*,Submission);
void Poll();
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_SceneGpuRequest,CyberpunkVR_SceneGpuReportSeq;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_SceneGpuGroupMode,CyberpunkVR_SceneGpuNodeFilter;
__declspec(dllexport) extern char CyberpunkVR_SceneGpuReport[262144];
}
