#pragma once
#include "Render/StereoBindingBank.hpp"
#include "Render/NativeStereoFamilies.hpp"
#include "Render/StereoSceneState.hpp"
#include "Render/StereoDepthComparison.hpp"
#include "Render/InstanceCoverage.hpp"
#include "Render/NativeTextureRead.hpp"
#include "Render/NativeMaterialBinding.hpp"
#include "Render/SkinningReadPlan.hpp"
#include <memory>

namespace cvr::stereo {
// Uses the native IA bindings at each actual draw, not a later geometry replay.
// The caller supplies a completely owned constant bank and restores native state.
// Each native recording is submitted once, then Reset before reuse. The live
// caller must prove that engine ownership contract; arbitrary replay is unsupported.
class EarlyDepthPass {
public:
    EarlyDepthPass();
    ~EarlyDepthPass();
    // 0: selected comparison, 1: whole reference, 2: optimized, 3: native timing only.
    HRESULT Create(ID3D12Device*,UINT width,UINT height,UINT nativeMode=0);
    bool StartNative(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,ID3D12Resource*,
                     ID3D12Resource* instanceSource=nullptr,D3D12_RESOURCE_STATES instanceState=D3D12_RESOURCE_STATE_COMMON);
    bool CaptureInstances(ID3D12GraphicsCommandList*,ID3D12Resource*,UINT64 offset,UINT bytes,D3D12_RESOURCE_STATES,bool combinedRead=true);
    void ReleaseInstanceRead(ID3D12GraphicsCommandList*);
    ID3D12Resource* InstanceSource()const;
    std::array<uint64_t,2> InstanceProofCounts()const;
    bool CaptureSkinning(ID3D12GraphicsCommandList*,ID3D12Resource* bones,std::span<const skinning::BoneRange>,const texture_read::Lease&,UINT sequence=0);
    bool CaptureAlpha(ID3D12GraphicsCommandList*,const material_binding::Lease&,const texture_read::Lease&,
                      std::span<const UINT> mips,UINT expectedIndex,UINT drawSequence);
    // Captures, peer b4 copies, peer mip copies, logical b4 bytes, texture bytes, last failure.
    std::array<uint64_t,6> AlphaProofCounts()const;
    bool ShaderSource(ID3D12Resource*)const;
    std::array<uint64_t,4> ShaderProofCounts()const;
    void RejectShaderSnapshots();
    bool ObserveNative(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&);
    bool Original(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&);
    bool PeerRemainder(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,std::span<const InstanceRun> remaining);
    bool FinishNative(ID3D12GraphicsCommandList*,UINT side);
    bool CommitNative(bool allow);
    void AbortNative(ID3D12GraphicsCommandList*,UINT side);
    std::array<uint64_t,8> OptimizedCounts()const;
    std::array<uint32_t,8> NativeCounts()const;
    std::array<uint64_t,4> NativeDifferences()const;
    // MAIN begin/end/frequency, then VRCAM begin/end/frequency. Only valid after Read.
    std::array<uint64_t,6> GpuTimes()const;
    // alphaReference preserves the audited PS only in mode 1 with native camera bindings.
    bool Main(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,const families::Family&,StereoBindingBank&,
              std::span<const uint8_t> masks={},std::span<const uint8_t> peerOnly={},UINT bankDraw=0,bool nativeCamera=false,bool alphaReference=false,bool alphaOptimized=false);
    bool MainConstants(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,const families::Family&,
                       std::span<const BankPair>,std::span<const uint8_t> masks={},std::span<const uint8_t> peerOnly={});
    UINT ArenaCount()const;
    bool Peer(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&);
    bool Readback(ID3D12GraphicsCommandList*);
    HRESULT Read(DepthComparisonResult&);
    // Mode 2 only. S_FALSE means GPU work is still in flight; never waits.
    HRESULT Restart();
    void BeforeSubmit(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
    void Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
    void Reset(ID3D12CommandList*);
    bool MainSubmitted()const;
    bool Failed()const;
private:
    struct Storage;
    Microsoft::WRL::ComPtr<Storage> storage_;
};
}
