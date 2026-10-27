#pragma once
#include "Render/StereoPairedRoot.hpp"
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <string>

namespace cvr::stereo::families {
struct ResourceUse {UINT type{},visibility{},space{},shaderRegister{},count{},bytes{};};
struct RootVariant {Microsoft::WRL::ComPtr<ID3D12RootSignature> root;PairedRootPlan plan;};
struct PassContract {
    D3D12_RT_FORMAT_ARRAY targets{};
    DXGI_FORMAT depth=DXGI_FORMAT_UNKNOWN;
    DXGI_SAMPLE_DESC samples{};
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology{};
    bool fixedRasterState{};
    bool pixelShader=true;
    D3D12_DEPTH_STENCIL_DESC depthState{};
    bool depthStateKnown{};
};
struct Family {
    uint64_t id{};UINT asset{},record=UINT_MAX;
    std::string rootSha256,vertexSha256,pixelSha256;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> sourceRoot;
    std::shared_ptr<const RootVariant> pairedRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> nativeCameraPipeline;
    UINT nativeCameraStages{}; // Native uploader stage bits: VS=1, PS=2.
    UINT skinningReadContract{};
    UINT alphaDepthContract{};
    std::vector<ResourceUse> uses;
    bool instanceRebaseSafe{};
    bool instanceOrdinalIndependent{};
    PassContract pass;
};
struct Record {
    uint64_t id{},original{},root{},pairedRoot{},pipeline{},draws[2]{};
    uint32_t asset{},pairs{},uses{};int32_t result{};
};
static_assert(sizeof(Record)==72);
constexpr UINT Capacity=4096;
// Off unless CVR_STEREO_FAMILY_MANIFEST names an explicit verified development
// package. Creation/lookup never replaces a native PSO, draw or command list.
bool Enabled();
void RootCreated(ID3D12RootSignature*,UINT nodeMask,const void*,size_t);
void GraphicsCreated(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC&,ID3D12PipelineState*);
void StreamCreated(ID3D12Device*,const D3D12_PIPELINE_STATE_STREAM_DESC&,ID3D12PipelineState*);
std::shared_ptr<const Family> Lookup(ID3D12PipelineState*);
bool NativeDepthCompatible(ID3D12PipelineState*);
uint32_t NativeDepthStatus(ID3D12PipelineState*);
uint32_t NativeGeometryStatus(ID3D12PipelineState*);
std::shared_ptr<const Family> Bound(ID3D12GraphicsCommandList*); // exact compatible native root required
void Reset(ID3D12GraphicsCommandList*,ID3D12PipelineState*);
void PipelineBound(ID3D12GraphicsCommandList*,ID3D12PipelineState*);
void RootBound(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
void ObserveDraw(ID3D12GraphicsCommandList*,UINT node,int side);
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoFamilyAssetStatus,CyberpunkVR_StereoFamilyAssetCount;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoFamilyNativeCameras;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoFamilySeq,CyberpunkVR_StereoFamilyCount,CyberpunkVR_StereoFamilyOverflow;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoFamilyMisses,CyberpunkVR_StereoFamilyFailures,CyberpunkVR_StereoFamilyHits,CyberpunkVR_StereoFamilyRootHits;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoFamilyObserve;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_StereoFamilyBindingFailures;
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_StereoFamilyUnmatched[2],CyberpunkVR_StereoFamilyRootMismatch[2];
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoFamilyRecordBytes;
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoFamilyNativeCameraRecordBytes;
__declspec(dllexport) extern uint64_t CyberpunkVR_StereoFamilyNativeCameraPipelines[cvr::stereo::families::Capacity][2];
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoFamilySkinningReadContracts[cvr::stereo::families::Capacity];
__declspec(dllexport) extern uint32_t CyberpunkVR_StereoFamilyAlphaDepthContracts[cvr::stereo::families::Capacity];
__declspec(dllexport) extern cvr::stereo::families::Record CyberpunkVR_StereoFamilyRecords[cvr::stereo::families::Capacity];
__declspec(dllexport) extern char CyberpunkVR_StereoFamilyError[256];
__declspec(dllexport) extern char CyberpunkVR_StereoFamilyManifestHash[65];
}
