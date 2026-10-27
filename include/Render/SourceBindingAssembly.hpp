#pragma once
#include "Render/NativeSourceCbvSnapshot.hpp"
#include "Render/SourceSelectorPlan.hpp"
#include "Render/SourceTablePlan.hpp"
#include "Render/StructuredSnapshotView.hpp"
#include "Render/StereoBindingBank.hpp"
#include <optional>

namespace cvr::stereo::source_binding_assembly {
// Caller attaches receipts while still owning the exact original occurrence.
// Numeric labels never acquire a native resource or grant execution permission.
struct Scope {
    uint64_t root{},list{},generation{},occurrence{},source{};
    UINT eye=UINT_MAX;
    bool operator==(const Scope&)const=default;
};
struct OwnedSrvSlot {
    Scope contributor;
    UINT offset{},compactIndex{};
    descriptors::DescriptorStamp stamp;
    uint64_t resourceCookie{},originalIdentity{},originalVersion{},originalGpu{},originalHeap{};
    UINT originalCount{};
    Microsoft::WRL::ComPtr<IUnknown> originalOwner;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> originalCpuHeap;
};
struct OwnedSrvCoverageView {
    Scope source;
    std::shared_ptr<const OwnedSrvCoverage> owner;
    // Duplicate offsets may preserve multiple certified union contributors;
    // their full canonical stamps must agree. Generated null holes never occur.
    // Union storage may exceed this occurrence's original count. These entries
    // describe storage only; they do not expand its original access certificate.
    std::span<const OwnedSrvSlot> slots;
};
// Sparse SRVs copied by the trusted builder only. No late native heap lookup,
// pointer revival, texture-read epoch, residency or execution permission.
// Both queries require the exact issued table shape, scope and retained leases.
// Private SRV overrides and finite tables without canonical slot provenance
// intentionally have no coverage receipt. Caller authenticates texture leases
// separately before using any slot as a GPU membership bit.
bool DescribeOwnedSrvCoverage(const SharedBankTable&,const Scope&,OwnedSrvCoverageView&)noexcept;
bool MatchesOwnedSrvCoverage(const SharedBankTable&,const Scope&,UINT offset,
                            const descriptors::DescriptorStamp&,OwnedSrvCoverageView&)noexcept;
struct FiniteTable {Scope scope;const descriptors::OwnedSourceTable* table{};};
struct SparseTable {Scope scope;const descriptors::OwnedSourceSlots* table{};};
struct PrivateCbv {
    Scope scope;
    const source_cbv_snapshot::PrivateCbv* value{};
    const source_cbv_snapshot::PreparedCopies* copies{};
    // True only after this copy arena's Finalize succeeded.
    bool finalized{};
};
struct OriginalCbv {
    Scope scope;
    UINT parameter{},offset{},reflectedBytes{};
    D3D12_CONSTANT_BUFFER_VIEW_DESC view{};
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<IUnknown> owner;
    // Trusted caller proves this original slice's read epoch and immutable
    // contents through all intended users. Resource ownership alone is not it.
    bool readEpochCertified{};
};
struct PrivateSrv {
    Scope scope;
    UINT parameter{},offset{};
    const structured_snapshot_view::PrivateView* value{};
    descriptors::SrvBinding original;
    Microsoft::WRL::ComPtr<IUnknown> originalOwner;
    uint64_t originalIdentity{},originalVersion{},originalGpu{},originalHeap{},resourceCookie{};
    UINT originalCount{};
    // Caller-provided contract from the exact original CaptureAtDraw occurrence:
    // this held resource/cookie/full canonical binding was the finite CPU slot.
    // Numeric equality alone is not that provenance, and no late lookup is used.
    bool originalSlotCertified{};
    const source_cbv_snapshot::PrivateCbv* privateB4{};
    const source_cbv_snapshot::PreparedCopies* privateB4Copies{};
    std::span<const uint8_t> privateB4Bytes;
    // Caller certifies these actual private GPU bytes belong to the held
    // resource/view/owner/copy receipt below, not merely the same numeric scope.
    bool privateB4BytesCertified{};
};
struct Input {
    Scope scope;
    const D3D12_ROOT_SIGNATURE_DESC1* root{};
    std::span<const families::ResourceUse> shaderUses;
    std::span<const FiniteTable> finite;
    std::span<const SparseTable> sparse;
    std::span<const PrivateCbv> privateCbvs;
    std::span<const OriginalCbv> originalCbvs;
    source_selectors::FamilyKey family;
    Scope selectorScope;
    const source_selectors::Result* selectors{};
    // Caller certifies that supplied selections cover every dynamic access for
    // this occurrence, including inputs the selector package cannot enumerate.
    bool dynamicCoverageCertified{};
    std::span<const PrivateSrv> privateSrvs;
};
enum class Failure : UINT {
    None,Arguments,Root,Scope,Extent,Type,Collision,MissingTable,MissingUse,
    DynamicCoverage,CbvRange,CbvOwner,Capacity,NotReady,Device,Exception,SrvRange,SrvOwner
};
struct MissingCbv {UINT parameter{},offset{},reflectedBytes{};};
struct ShaderInputCheck {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<IUnknown> owner;
    uint64_t offset{};
    std::vector<uint8_t> expected;
};
struct GuardedValidation;
class CpuTableCache;
class Plan {
public:
    bool Ready()const noexcept{return ready_;}
    const Scope& Source()const noexcept{return scope_;}
    UINT DescriptorCount()const noexcept{return descriptors_;}
    UINT TableCount()const noexcept{return UINT(tables_.size());}
    std::span<const MissingCbv> MissingCbvs()const noexcept{return missing_;}
private:
    struct Copy {UINT offset{},sourceOffset{},count{};Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;};
    struct Cbv {UINT offset{};D3D12_CONSTANT_BUFFER_VIEW_DESC view{};};
    struct Srv {UINT offset{};D3D12_SHADER_RESOURCE_VIEW_DESC view{};Microsoft::WRL::ComPtr<ID3D12Resource> resource;};
    struct Table {
        UINT parameter{},count{};
        Scope sourceScope;
        D3D12_DESCRIPTOR_HEAP_TYPE type{};
        std::vector<D3D12_DESCRIPTOR_RANGE_TYPE> slotTypes;
        std::vector<Copy> copies;
        std::vector<Cbv> cbvs;
        std::vector<Srv> srvs;
        uint64_t sourceRoot{},sourceGpu{},sourceHeap{};
        std::vector<descriptors::OwnedSourceSlot> sparseSlots;
        std::vector<OwnedSrvSlot> srvCoverage;
        bool sparseUnionCertified{},unionStorage{};
        // Owners of descriptor backing. CBV copy receipts stay only in leases;
        // the genuine overlay owner also retains its finalized snapshot inputs.
        std::vector<Microsoft::WRL::ComPtr<IUnknown>> descriptorOwners;
        std::vector<Microsoft::WRL::ComPtr<IUnknown>> leases;
    };
    Scope scope_;
    std::vector<Table> tables_;
    std::vector<MissingCbv> missing_;
    UINT descriptors_{};
    bool ready_{};
    friend bool Build(const Input&,Plan&,Failure&);
    friend bool BuildCore(const Input&,const GuardedValidation*,Plan&,Failure&);
    friend HRESULT Assemble(ID3D12Device*,const Plan&,std::vector<SharedBankTable>&,Failure&);
    friend class CpuTableCache;
    friend HRESULT Assemble(ID3D12Device*,const Plan&,CpuTableCache&,std::vector<SharedBankTable>&,Failure&,UINT);
    friend HRESULT AssemblePass(ID3D12Device*,std::span<const Plan* const>,CpuTableCache&,
        std::vector<std::vector<SharedBankTable>>&,Failure&,UINT);
};
class CpuTableCache {
public:
    static constexpr UINT DefaultDescriptorLimit=1000000;
    CpuTableCache();~CpuTableCache();
    CpuTableCache(const CpuTableCache&)=delete;CpuTableCache& operator=(const CpuTableCache&)=delete;
    UINT UniqueDescriptorCount()const noexcept;
    UINT TableCount()const noexcept;
    uint64_t HitCount()const noexcept;
    uint64_t MissCount()const noexcept;
private:
    struct Storage;std::unique_ptr<Storage> storage_;
    friend HRESULT Assemble(ID3D12Device*,const Plan&,CpuTableCache&,std::vector<SharedBankTable>&,Failure&,UINT);
    friend HRESULT AssemblePass(ID3D12Device*,std::span<const Plan* const>,CpuTableCache&,
        std::vector<std::vector<SharedBankTable>>&,Failure&,UINT);
};
// Failure clears output. Missing/unfinalized CBV ownership is a successful
// diagnostic plan, with Ready false and no consumable bank tables.
bool Build(const Input&,Plan& output,Failure&);
// Creates immutable CPU tables suitable for StereoBindingBank::CreateSourcePass.
// Original indices remain unchanged; null-filled holes are certified unused.
// Keeps finite/sparse descriptor owners, CBV/SRV owners/resources and ALL copy owners.
// Transactional output. No GPU work, source contents, submission or suppression.
// Private SRV readiness grants no scheduling permission: its consumer still
// needs the exact overlay-copy recording or a completed destination fence.
HRESULT Assemble(ID3D12Device*,const Plan&,std::vector<SharedBankTable>& output,Failure&);
// Exact immutable descriptor storage only, qualified by original root/parameter.
// All produced immutable heaps carry explicit bank-storage sharing metadata.
// Current plan readiness/scope and
// ALL current occurrence leases still apply on hits. Unique extents consume the
// budget; output/cache/counters remain unchanged after any failed transaction.
HRESULT Assemble(ID3D12Device*,const Plan&,CpuTableCache&,std::vector<SharedBankTable>& output,Failure&,
    UINT uniqueDescriptorLimit=CpuTableCache::DefaultDescriptorLimit);
// Pass-local unions only for certified sparse SRV tables in the same original
// root/heap/base/parameter domain. Full canonical conflicts form separate unions.
// Each source keeps its original count, exact access certificate and current
// leases; extra union descriptors grant no access or execution permission.
// All contributing immutable capture owners survive with the shared storage.
// Whole output, cache and counters commit together, including nonunion tables.
HRESULT AssemblePass(ID3D12Device*,std::span<const Plan* const>,CpuTableCache&,
    std::vector<std::vector<SharedBankTable>>& output,Failure&,
    UINT uniqueDescriptorLimit=CpuTableCache::DefaultDescriptorLimit);

struct PredictedCbv {source_selectors::CbvKey key;std::span<const uint8_t> expected;};
struct PredictedT5 {
    // Original descriptor provenance is identical to the verified path. Its
    // value/privateB4/byte-certification fields must remain unset here.
    PrivateSrv original;
    structured_buffer_snapshot::PrivateRange copy;
    structured_buffer_snapshot::PreparedCopy receipt;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    std::span<const uint8_t> expected;
    // Actual native RecordedView plus finalized original-copy arena. This
    // certifies storage, not GPU contents, source submission or scheduling.
    bool recordedViewCertified{};
};
struct GuardedInput {
    Input source;
    const source_selectors::FamilyPlan* recipe{};
    std::span<const PredictedCbv> cbvs;
    const PredictedT5* t5{};
};
struct GuardedT5View {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<IUnknown> owner;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    uint64_t offset{},bytes{};
};
struct GuardedTables {
    Scope source;
    std::vector<SharedBankTable> tables;
    std::vector<ShaderInputCheck> checks;
    std::optional<GuardedT5View> t5;
};
class GuardedPlan {
public:
    bool Prepared()const{return plan_.Ready();}
    UINT CheckCount()const{return UINT(checks_.size());}
private:
    Plan plan_;
    std::vector<ShaderInputCheck> checks_;
    std::optional<GuardedT5View> t5_;
    friend bool BuildGuarded(const GuardedInput&,GuardedPlan&,Failure&);
    friend HRESULT AssembleGuardedPass(ID3D12Device*,std::span<const GuardedPlan* const>,CpuTableCache&,std::vector<GuardedTables>&,Failure&);
};
// Re-evaluates the complete selector recipe from predictions and binds each
// prediction to its exact finalized GPU copy. Never claims CPU byte validation.
// The resulting tables MUST remain gated by every returned GPU check.
bool BuildGuarded(const GuardedInput&,GuardedPlan&,Failure&);
HRESULT AssembleGuardedPass(ID3D12Device*,std::span<const GuardedPlan* const>,CpuTableCache&,std::vector<GuardedTables>&,Failure&);
}
