#pragma once
#include "Render/SinglePassTrace.hpp"
#include "Render/EarlyDepthLimits.hpp"
#include "Render/NativeStereoProbe.hpp"
#include "Render/StereoDepthComparison.hpp"
#include "Render/NativeCameraBindingState.hpp"

namespace cvr::stereo::early_depth {
bool NeedsCamera();
bool Drawing();
bool MaterialPending();
bool RunTracking();
void FrameBoundary();
void BeforeFrameBoundary();
void Camera(const trace::Record&);
void BoundCamera(uint32_t binding,uint32_t handle,uint32_t stages);
struct Group {uint64_t id{};uint32_t frame{};int side=-1;uint32_t restoreCamera{};camera_upload::BindingScope cameraScope;};
Group Begin(uint32_t node,int side,const void* renderer,const void* arguments,const void* span);
void End(Group previous);
bool Draw(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,std::span<const uint8_t> instances);
void Unsupported(ID3D12GraphicsCommandList*,uint32_t reason);
void InstanceWrite(ID3D12GraphicsCommandList*,ID3D12Resource*);
void InstanceAlias(ID3D12GraphicsCommandList*,ID3D12Resource* before,ID3D12Resource* after);
void InstanceBarrier(ID3D12GraphicsCommandList*,ID3D12Resource*);
void Closing(ID3D12GraphicsCommandList*);
void Reset(ID3D12CommandList*);
void BeforeSubmit(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
void Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
void Poll();
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthRequest,CyberpunkVR_EarlyDepthState;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthStats[16];
__declspec(dllexport) extern int32_t CyberpunkVR_EarlyDepthResult;
__declspec(dllexport) extern cvr::stereo::DepthComparisonResult CyberpunkVR_EarlyDepthComparison;
__declspec(dllexport) extern uint32_t CyberpunkVR_EarlyDepthSourceCapacity,CyberpunkVR_EarlyDepthInstanceCapacity;
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthSources[cvr::stereo::early_depth::MaxDraws][2];
__declspec(dllexport) extern uint32_t CyberpunkVR_EarlyDepthCoverage[cvr::stereo::early_depth::MaxDraws][2];
__declspec(dllexport) extern uint32_t CyberpunkVR_EarlyDepthMasks[cvr::stereo::early_depth::MaxDraws][2],CyberpunkVR_EarlyDepthMaskStats[14];
__declspec(dllexport) extern uint32_t CyberpunkVR_EarlyDepthExtraCoverage[cvr::stereo::early_depth::MaxDraws][2];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthFailureData[6];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthWholeTarget;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthRejectCommit;
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthOptimized[8];
__declspec(dllexport) extern uint32_t CyberpunkVR_EarlyDepthNativeCounts[8];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthNativeDifferences[4];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthGpuTimes[6];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthGpuInstances[4];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthInstanceReadScope;
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthInstanceRows,CyberpunkVR_EarlyDepthInstanceBlob;
__declspec(dllexport) extern uint32_t CyberpunkVR_EarlyDepthInstanceCount,CyberpunkVR_EarlyDepthInstanceBytes;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthRunRequest,CyberpunkVR_EarlyDepthRunState,CyberpunkVR_EarlyDepthRunStop;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthRunSeq,CyberpunkVR_EarlyDepthRunCount;
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunStats[16];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunArenas[2];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunDeferred[2];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunInstanceProofs[2];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunCpu[10];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthRunCpuFrequency;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthRunGeometryChecks[2];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthRunRows[1200][16];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthRunFrameStarts[1200];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthNativeCamera;
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthNativeCameraStats[3];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunNativeCamera[2];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthCameraStages;
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthCameraStageMasks[4];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthCameraBindingInfo[16];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunCameraStageMasks[4];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_EarlyDepthSkinning;
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthSkinningStats[6],CyberpunkVR_EarlyDepthShaderProofs[4];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunSkinningStats[6],CyberpunkVR_EarlyDepthRunShaderProofs[4];
__declspec(dllexport) extern uint64_t CyberpunkVR_EarlyDepthBindingChecks[4];
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_EarlyDepthRunBindingChecks[4];
}
