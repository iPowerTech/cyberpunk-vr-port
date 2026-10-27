#pragma once
#include "Render/NativeIaSnapshot.hpp"
#include "Render/NativeSourceSrvBinding.hpp"
#include "Render/SourceDescriptorViews.hpp"

namespace cvr::stereo::material_buffer_snapshot {
enum class Failure:UINT {None,Disabled,Arguments,Scope,Native,Creation,Allocation,Pool,State,Capacity,Changed,Retention,Exception};
inline constexpr uint64_t Capacity=8*1024*1024,Alignment=128;
struct Epoch {uint64_t list{},generation{},occurrence{};bool certified{};};
struct Copy {
    Microsoft::WRL::ComPtr<IUnknown> owner;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    uint64_t sourceOffset{},offset{},bytes{};
};
class ActualCallScope {
public:
    ActualCallScope(ID3D12GraphicsCommandList*,uint64_t,uint64_t);
    ~ActualCallScope();
    ActualCallScope(const ActualCallScope&)=delete;ActualCallScope& operator=(const ActualCallScope&)=delete;
    bool Active()const;
private:indirect_copy::detail::ScopeStamp stamp_;
};
class Arena {
public:
    Arena();~Arena();
    Arena(const Arena&)=delete;Arena& operator=(const Arena&)=delete;
    HRESULT Create(ID3D12Device*,ID3D12GraphicsCommandList*,uint64_t generation);
    bool Finalize(ID3D12GraphicsCommandList*);
    void Abort();
    uint64_t Bytes()const;
private:
    struct Storage;Microsoft::WRL::ComPtr<Storage> storage_;
    friend bool Capture(ID3D12GraphicsCommandList*,uint64_t,const source_srv::SourceSrvBacking&,
        const preparation::GraphParentRecord&,const Epoch&,Arena&,Copy&);
};
// Copies the complete original t4/t6 view at its certified native read occurrence.
// Native fixed-read pools/placed allocations are pinned; no source state changes.
bool Capture(ID3D12GraphicsCommandList*,uint64_t root,const source_srv::SourceSrvBacking&,
    const preparation::GraphParentRecord&,const Epoch&,Arena&,Copy&);
bool ValidateSubmission(const Copy&,ID3D12CommandQueue*);
bool Ready(const Copy&,const native_recording::Lease&,ID3D12CommandQueue*,ID3D12Fence*,uint64_t signal,bool submitted);
bool MatchesPrivate(const Copy&,const descriptors::SourceResourceBinding&);
// Descriptor construction only, after Finalize and ValidateSubmission certified
// this exact native recording on this queue. Does not establish GPU completion
// or execution ordering; the caller must order the copy before private reads.
bool MatchesStagedPrivate(const Copy&,const descriptors::SourceResourceBinding&,
    const native_recording::Lease&,ID3D12CommandQueue*);
Failure LastFailure();
namespace detail {
inline bool SameRecording(const native_recording::Lease& a,const native_recording::Lease& b){
    return a.token && a.token==b.token && a.list==b.list && a.generation==b.generation && a.context==b.context && a.registry==b.registry;
}
inline bool SameBufferView(const D3D12_SHADER_RESOURCE_VIEW_DESC& a,const D3D12_SHADER_RESOURCE_VIEW_DESC& b){
    return a.ViewDimension==D3D12_SRV_DIMENSION_BUFFER && b.ViewDimension==a.ViewDimension && a.Format==b.Format &&
        a.Shader4ComponentMapping==b.Shader4ComponentMapping && a.Buffer.FirstElement==b.Buffer.FirstElement &&
        a.Buffer.NumElements==b.Buffer.NumElements && a.Buffer.StructureByteStride==b.Buffer.StructureByteStride && a.Buffer.Flags==b.Buffer.Flags;
}
inline bool SourceBinding(const source_srv::SourceSrvBacking& source,const descriptors::SourceResourceBinding& binding){
    const auto& original=source.binding;descriptor_views::SrvResult decoded;
    return (source.shaderRegister==4 || source.shaderRegister==6) && original.resource &&
        binding.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV && binding.parameter==source_srv::RootParameter && binding.offset==source.shaderRegister &&
        binding.resource==original.resource && !binding.counter && original.description==original.stamp.value &&
        binding.stamp.value==original.stamp.value && binding.stamp.heap==original.stamp.heap && binding.stamp.gpu==original.stamp.gpu &&
        descriptor_views::DecodeSrv(binding.stamp.value,decoded) && decoded.resourceCookie && !decoded.implicit && SameBufferView(decoded.view,original.view);
}
inline bool PrivateView(const D3D12_SHADER_RESOURCE_VIEW_DESC& original,UINT reg,uint64_t offset,uint64_t bytes,
    D3D12_SHADER_RESOURCE_VIEW_DESC& result){
    result={};const uint64_t stride=reg==4?4:64;
    if((reg!=4 && reg!=6) || original.ViewDimension!=D3D12_SRV_DIMENSION_BUFFER ||
        original.Shader4ComponentMapping!=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING || !bytes || bytes!=uint64_t(original.Buffer.NumElements)*stride || offset%stride)return false;
    if(reg==4){if(original.Format!=DXGI_FORMAT_R32_TYPELESS || original.Buffer.Flags!=D3D12_BUFFER_SRV_FLAG_RAW || original.Buffer.StructureByteStride)return false;}
    else if(original.Format!=DXGI_FORMAT_UNKNOWN || original.Buffer.Flags || original.Buffer.StructureByteStride!=64)return false;
    result=original;result.Buffer.FirstElement=offset/stride;return true;
}
}
}
