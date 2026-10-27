#pragma once
#include "Render/MultilayerSelectorPlan.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <span>

namespace cvr::stereo {
struct MultilayerMembershipSource {
    source_selectors::FamilyKey family;
    Microsoft::WRL::ComPtr<ID3D12Resource> b4,t5;
    UINT64 b4Offset{},t5Offset{},t5Bytes{};
    UINT firstElement{},numElements{};
    // Bits must identify exact owned descriptors and their retained inputs, not
    // merely occupied native heap cells. Authentication belongs to the caller.
    std::array<UINT,1024> membership{};
    UINT64 sourceId{};
    UINT eye{};
};
// Checks the exact multilayer PS's actual B4/T5 bytes against an owned descriptor
// bank. No expected source bytes, native calls, GPU waits or implicit readback.
// Inputs and producer gate must be finalized NON_PIXEL_SHADER_RESOURCE; the
// caller proves producer order, input lifetime and descriptor/residency authority.
// A failed check does NOT authorize suppressing the native draw or create a
// valid fallback. A zero producer gate disables BOTH output predicates.
class GpuMultilayerMembership {
public:
    enum Error:UINT {InvalidBase=1,RecordOverflow=2,ViewBounds=4,DescriptorBounds=8,MissingDescriptor=16};
    GpuMultilayerMembership();~GpuMultilayerMembership();
    HRESULT Create(ID3D12Device*,std::span<const MultilayerMembershipSource>,ID3D12Resource* producerGate,UINT64 gateOffset=0);
    // One DIRECT recording. Clears predication and changes compute bindings;
    // caller restores bindings. CommandResources retains all captured objects.
    bool Record(ID3D12GraphicsCommandList*);
    // Predicates finish PREDICATION|NON_PIXEL_SHADER_RESOURCE|COPY_SOURCE;
    // Results finish NON_PIXEL_SHADER_RESOURCE|COPY_SOURCE. Diagnostic copies
    // and CPU mapping belong to the caller after its actual completion fence.
    ID3D12Resource* Predicates()const;
    ID3D12Resource* Results()const;
    static constexpr UINT64 PredicateOffset(UINT source,bool rejected=false){return UINT64(source)*16+(rejected?8:0);}
    // Eight UINTs per source: enabled,error,base,mask,records,selectors,
    // firstFailureBit,firstFailureIndex (UINT_MAX when absent/inapplicable).
    static constexpr UINT64 ResultOffset(UINT source){return UINT64(source)*32;}
    UINT SourceCount()const;
    // One dispatch per distinct held (B4,T5) resource pair, in first-seen group
    // order. Output rows keep the original source index. Zero before Record.
    UINT DispatchCount()const;
    UINT64 ReadbackBytes()const;
private:
    struct Storage;Microsoft::WRL::ComPtr<Storage> storage_;
};
}
