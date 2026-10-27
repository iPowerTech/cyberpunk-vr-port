#pragma once
#include "Render/StereoGeometryCommands.hpp"
#include "Render/NativeStereoFamilies.hpp"
#include "Render/StereoBindingBank.hpp"
#include "Render/SourceBindingAssembly.hpp"
#include <optional>

namespace cvr::stereo {
struct GeometrySharedResourceRead {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    std::optional<D3D12_RESOURCE_STATES> state;
    Microsoft::WRL::ComPtr<IUnknown> allocationOwner;
    uint64_t producerPosition{};
};
struct GeometrySharedEyeInput {
    // Native side: element0 is geometry.eyes[0] (side1/VRCAM), element1 is
    // geometry.eyes[1] (side0/MAIN), not the native source vector's index order.
    source_binding_assembly::Scope scope;
    const source_selectors::Result* selectors{};
    Microsoft::WRL::ComPtr<IUnknown> gpuInputOwner;
};
struct GeometrySharedReadContract;
using ValidateGeometrySharedRead=bool(*)(const GeometrySharedReadContract&,const families::Family&,
    std::span<const BankPair>,std::span<const SharedBankTable>,ID3D12GraphicsCommandList* actualRecording);
struct GeometrySharedReadContract {
    // Destination recording is numeric to avoid list -> batch -> pass -> list.
    uint64_t list{},generation{};
    Microsoft::WRL::ComPtr<IUnknown> epochOwner,allocatorOwner,queueOwner;
    std::span<const GeometrySharedResourceRead> resources;
    std::array<GeometrySharedEyeInput,2> eyes;
    // Trusted owning caller validates the complete actual descriptor resource
    // set, states, allocations, producer order and immutable read-only epoch
    // through this pass/GPU completion. For dynamic uses it also certifies BOTH
    // qualified original GPU inputs and every shader access site. Equality and
    // Result completion flags are not certificates. No new native grant occurs.
    // Read-only: no commands/state changes/acquisition. Called at Create with
    // nullptr, then Record with the actual destination list. Owners must not
    // retain that destination list. Caller proof loss rejects recording.
    ValidateGeometrySharedRead validate{};
};
struct GeometryPassDraw {
    GeometryCommandPair geometry;
    std::shared_ptr<const families::Family> family;
    // Exact patched singleton CBV/SRV pairs; original unpatched inputs below.
    std::span<const BankPair> constants;
    UINT viewMask=3;
    std::span<const SharedBankTable> shared;
    GeometrySharedReadContract sharedRead;
};
struct GeometryPassTargets {
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> colors;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
};
struct GeometrySourcePassDraw {
    GeometrySourceCommand geometry;
    SourceBankDraw binding;
    families::PassContract contract;
};
using SourceTargetBind=bool(*)(void*,ID3D12GraphicsCommandList*,UINT source);
// Records a whole prepared pass into two-layer outputs. Caller must initialize
// targets, establish resource states and geometry-content dependencies before
// Record, and schedule consumers afterwards. No native draws are removed here.
// Record consumes one private DIRECT recording; it does not restore game state.
class StereoGeometryPass {
public:
    StereoGeometryPass();
    ~StereoGeometryPass();
    HRESULT Create(ID3D12Device*,std::span<const GeometryPassDraw>,const GeometryPassTargets&);
    // Alternative original-root mixed stream. No family conversion, rebase or
    // pairing. Caller certifies source PSO/contract/root/blob association, all
    // reachable input contents/states/allocations, and target/queue ordering.
    HRESULT CreateSources(ID3D12Device*,std::span<const GeometrySourcePassDraw>);
    // Ordered, once-only source emission on one caller-certified recording.
    // Target binder runs before each draw; it must not execute geometry itself.
    // On failure SourceRecordedCount is the exact already-emitted prefix. Caller
    // drains the remainder, not the whole eye, and restores destination state.
    // Reset/replay/foreign submission require rebuilding and fresh admission.
    bool RecordSource(ID3D12GraphicsCommandList*,UINT source,SourceTargetBind,void*);
    UINT SourceCount() const;
    UINT SourceRecordedCount() const;
    bool Record(ID3D12GraphicsCommandList*);
    UINT DrawCount() const;
    UINT64 InstanceBytes() const;
    UINT ValidationStage() const {return validationStage_;}
    UINT ValidationDraw() const {return validationDraw_;}
    UINT ValidationBits() const {return validationBits_;}
private:
    struct Storage;
    Microsoft::WRL::ComPtr<Storage> storage_;
    UINT validationStage_{},validationDraw_{},validationBits_{};
};
}
