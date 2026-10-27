#pragma once
#include "Render/NativeStereoProbe.hpp"
#include <span>
namespace cvr::stereo::gpu_probe {
struct GroupContext {uint64_t id{};int side=-1;uint32_t plane=UINT_MAX;uint64_t lastPipeline{};uint32_t depthRun{};bool depthCandidate{};};
struct Difference {uint32_t target{},eye{},plane{},x{},y{},reference{},shared{};};
GroupContext BeginGroup(int side,uint32_t plane);
void EndGroup(GroupContext previous);
void Draw(ID3D12GraphicsCommandList*,ID3D12PipelineState* variant,const native_probe::DrawRecord&,std::span<const uint8_t> instances={});
void Boundary(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,bool requirePipelineChange=true,ID3D12PipelineState* candidateVariant=nullptr);
bool RouteNative(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&);
void BeforeSubmit(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
void Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
void Poll();
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeRequest,CyberpunkVR_StereoGpuProbeState;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeNativeReference,CyberpunkVR_StereoGpuProbeReferenceSkipped;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeQueueWaits;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeGroup,CyberpunkVR_StereoGpuProbeSourceDraws,CyberpunkVR_StereoGpuProbeMainDraws;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeMainViewMask,CyberpunkVR_StereoGpuProbeMainReferenceMask;
__declspec(dllexport) extern uint64_t CyberpunkVR_StereoGpuProbeEyeDifferences[2],CyberpunkVR_StereoGpuProbeTargetDifferences[4];
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoGpuProbeDifferenceCount;
__declspec(dllexport) extern cvr::stereo::gpu_probe::Difference CyberpunkVR_StereoGpuProbeDifferences[128];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbePrepareGateMs,CyberpunkVR_StereoGpuProbePrepareGateReason;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeLateVisibility,CyberpunkVR_StereoGpuProbeMatchedDraws,CyberpunkVR_StereoGpuProbeFallbackDraws;
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_StereoGpuProbePrepareGateStart,CyberpunkVR_StereoGpuProbePrepareGateMain,CyberpunkVR_StereoGpuProbePrepareGateRelease;
__declspec(dllexport) extern int32_t CyberpunkVR_StereoGpuProbeResult;
__declspec(dllexport) extern uint64_t CyberpunkVR_StereoGpuProbeDifferentBytes,CyberpunkVR_StereoGpuProbeComparedBytes;
__declspec(dllexport) extern uint64_t CyberpunkVR_StereoGpuProbeCoverage[2];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeSceneDepth,CyberpunkVR_StereoGpuProbeSceneReject;
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoGpuProbeSceneSize[2],CyberpunkVR_StereoGpuProbeScenePhase[2];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeSceneRoute,CyberpunkVR_StereoGpuProbeSceneCommitted;
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoGpuProbeSceneRouted[2];
__declspec(dllexport) extern uint64_t CyberpunkVR_StereoGpuProbeSceneSamples[2];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeTiming,CyberpunkVR_StereoGpuProbeTimingDrops;
__declspec(dllexport) extern uint64_t CyberpunkVR_StereoGpuProbeTicks[7][2],CyberpunkVR_StereoGpuProbeFrequency[2];
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoGpuProbeTimedCount[7][2];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeMixedMaterials;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeFineRate;
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoGpuProbeVrs[2][4],CyberpunkVR_StereoGpuProbeVrsChanges[2];
__declspec(dllexport) extern uint64_t CyberpunkVR_StereoGpuProbeVrsImage[2];
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoGpuProbeVrsSize[2],CyberpunkVR_StereoGpuProbeVrsCaptured[2];
__declspec(dllexport) extern uint64_t CyberpunkVR_StereoGpuProbeVrsDifference,CyberpunkVR_StereoGpuProbeVrsHistogram[2][16];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeDepthPrepass;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeStartIndices;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoGpuProbeStartRun;
}
