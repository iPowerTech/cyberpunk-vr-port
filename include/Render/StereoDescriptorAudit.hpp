#pragma once
#include <d3d12.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include "Render/StereoSceneState.hpp"
#include "Render/NativeStereoProbe.hpp"
#include "Render/StereoDescriptorStore.hpp"
#include "Render/NativeCommandRecording.hpp"

namespace cvr::stereo::descriptors {
struct Binding {
    uint32_t parameter{},type{},space{},shaderRegister{},count{},tableOffset{},valueOffset{},valueCount{};
    uint64_t gpu{},heap{},version{};
    uint32_t status{},visibility{}; // 1=range identity; 2=also exact descriptor IDs; 0=unknown
};
struct DrawBindings {uint32_t sequence{},offset{},count{},complete{};};
struct RootInfo {uint64_t root{};uint32_t parameters{},supported{},ranges{},reasons{};};
static_assert(sizeof(Binding)==64 && sizeof(DrawBindings)==16 && sizeof(Value)==160);
bool Enabled();
void Initialize(ID3D12Device*);
void HeapCreated(ID3D12Device*,ID3D12DescriptorHeap*);
void HeapsBound(ID3D12Device*,UINT,ID3D12DescriptorHeap* const*);
void RootCreated(ID3D12RootSignature*,const void*,size_t);
struct CbvOrigin {uint64_t cookie{},id{};Microsoft::WRL::ComPtr<IUnknown> owner;};
void Cbv(const D3D12_CONSTANT_BUFFER_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE,const CbvOrigin* origin=nullptr);
// Only with a fresh native creation/source resource already owned by the caller.
uint64_t SourceResourceCookieAtCreation(ID3D12Resource*);
// Read-only identity check on an independently held resource. No cookie creation
// or weak-resource lookup; the caller must already own the object.
bool MatchesHeldResourceCookie(ID3D12Resource*,uint64_t cookie)noexcept;
// Existing descriptor identity on an independently held resource. Never assigns
// a cookie or consults the weak resource registry.
bool GetHeldResourceCookie(const Microsoft::WRL::ComPtr<ID3D12Resource>&,uint64_t& cookie)noexcept;
void Srv(ID3D12Resource*,const D3D12_SHADER_RESOURCE_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
void Uav(ID3D12Resource*,ID3D12Resource*,const D3D12_UNORDERED_ACCESS_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
void Sampler(const D3D12_SAMPLER_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
void Copy(ID3D12Device*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,D3D12_DESCRIPTOR_HEAP_TYPE);
void BeginCapture();
void Snapshot(const native_probe::DrawRecord&);
void EndCapture();
bool BeginBindings();
void EndBindings();
bool Healthy();
// Bracket the actual descriptor driver mutation AND its metadata callback.
// Includes internal commands. Installation must certify every mutation entry.
class MutationScope {
public:
    MutationScope();~MutationScope();
    MutationScope(const MutationScope&)=delete;
    MutationScope& operator=(const MutationScope&)=delete;
private:bool entered_{};
};
// False is a permanent coverage failure. True cannot undo it.
void BorrowedRangeObserverReady(bool);
struct BoundTableMetadata {UINT parameter{};D3D12_GPU_DESCRIPTOR_HANDLE table{};};
inline constexpr UINT TableMetadataSlotLimit=65536;
enum class TableMetadataFailure:UINT {None,Arguments,Coverage,RootLayout,Heap,Bounds,Capacity,Changed,Exception};
enum TableMetadataUnknown:UINT {
    TableMetadataOtherRootParameters=1,TableMetadataDirectIndexing=2,TableMetadataUnboundTables=4,
    TableMetadataUnknownSlots=8,TableMetadataWrongDescriptorKind=16,TableMetadataImplicitViews=32
};
struct TableMetadataSummary {
    uint64_t root{},trackingEpoch{};
    UINT rootParameters{},rootFlags{},layoutReasons{},boundTables{},unboundTables{},unknownFlags{};
    uint64_t unknownSlots{};
    bool complete{};
    struct Range {
        UINT parameter{},type{},space{},shaderRegister{},declaredCount{},tableOffset{},visibility{},firstValue{};
        uint64_t table{};bool physicalRemainder{};RangeIdentity identity;
    };
    std::vector<Range> ranges;
    // Original canonical values, including explicit nulls, UAV counter cookies,
    // and unknown all-zero slots. IDs/cookies are metadata, never live resources.
    std::vector<Value> values;
};
class TableMetadataSnapshot {
    struct Data;std::shared_ptr<const Data> data_;
    friend bool CaptureTableMetadata(const Microsoft::WRL::ComPtr<ID3D12RootSignature>&,
        std::span<const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>>,std::span<const BoundTableMetadata>,TableMetadataSnapshot&);
    friend bool ValidateTableMetadataNoOverwrite(const TableMetadataSnapshot&);
    friend bool DescribeTableMetadata(const TableMetadataSnapshot&,TableMetadataSummary&);
public:explicit operator bool()const{return bool(data_);}
};
// The caller already holds the actual root/heaps. Only those objects are
// retained; resource pointers are never resolved or AddRef'd. Captures declared
// ranges of the supplied tables, without assuming any shader reaches them.
// At most64 supplied tables/256 ranges/65536 slots; unbounded declarations cover
// the exact remaining physical heap. True permits coherent incomplete metadata.
bool CaptureTableMetadata(const Microsoft::WRL::ComPtr<ID3D12RootSignature>& heldRoot,
    std::span<const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> heldHeaps,
    std::span<const BoundTableMetadata>,TableMetadataSnapshot&);
// Descriptor-byte stability only. No resource ownership, alias/content/state,
// shader-access coverage, GPU execution or reordering permission is established.
bool ValidateTableMetadataNoOverwrite(const TableMetadataSnapshot&);
bool DescribeTableMetadata(const TableMetadataSnapshot&,TableMetadataSummary&);
TableMetadataFailure TableMetadataLastFailure();
class BorrowedFixedRange;
class BorrowedSourceRanges {
    struct Data;std::shared_ptr<const Data> data_;
    friend bool CaptureBorrowedSourceRanges(ID3D12GraphicsCommandList*,const native_recording::Lease&,
        const scene_state::Bindings&,BorrowedSourceRanges&);
    friend bool ValidateBorrowedSourceRanges(const BorrowedSourceRanges&);
    friend bool DescribeBorrowedSourceRanges(const BorrowedSourceRanges&,struct BorrowedRangeSummary&);
    friend bool InventoryBorrowedSourceRange(const BorrowedSourceRanges&,UINT,struct BorrowedRangeInventory&);
    friend bool CaptureBorrowedFixedRange(ID3D12GraphicsCommandList*,const native_recording::Lease&,const BorrowedSourceRanges&,UINT,BorrowedFixedRange&);
    friend bool ValidateBorrowedFixedRange(const BorrowedFixedRange&,const BorrowedSourceRanges&,UINT);
public:explicit operator bool()const{return bool(data_);}
};
// Original source callback only, while its native token and context are live.
// Requires the actual current PSO and its authenticated shader-family uses.
// Pins COM heaps/root/PSO and only shader-used descriptor intervals; unused root
// tables are omitted. Any used unbounded-root declaration still covers the exact
// remaining physical heap span, not a guessed/reflected runtime array bound.
bool CaptureBorrowedSourceRanges(ID3D12GraphicsCommandList*,const native_recording::Lease&,
    const scene_state::Bindings&,BorrowedSourceRanges&);
// No current TLS/pending-token dependency. This proves only descriptor bytes
// were not overwritten; it grants NO Draw/Bind/material read/native lifetime,
// residency/state permission. The caller must separately join a live invocation.
bool ValidateBorrowedSourceRanges(const BorrowedSourceRanges&);
struct BorrowedRangeSummary {
    uint64_t list{},generation{},root{},pipeline{},family{};
    struct Range {
        uint32_t parameter{},type{},firstOffset{},count{},stride{},shaderDeclaredCount{};
        uint64_t table{},gpu{},heap{},version{};bool physicalRemainder{};
    };
    std::vector<Range> ranges; // At most 256 authenticated numeric observations.
};
// Revalidates the opaque no-overwrite witness before returning diagnostics.
// These numbers cannot be supplied back as authority and grant no GPU access.
bool DescribeBorrowedSourceRanges(const BorrowedSourceRanges&,BorrowedRangeSummary&);
struct BorrowedRangeInventory {
    uint64_t list{},generation{},root{},pipeline{},family{};UINT rangeIndex{};
    BorrowedRangeSummary::Range range;
    BorrowedDescriptorInventory counts;
};
// Dynamic physical-remainder range only. Scans at most Store::SlotLimit slots
// and caches canonical aggregates by exact range/version inside this receipt.
// tracked/untracked cookies are only weak metadata-map membership. No resource
// lookup, AddRef, descriptor creation, native lifetime or read permission occurs.
bool InventoryBorrowedSourceRange(const BorrowedSourceRanges&,UINT rangeIndex,BorrowedRangeInventory&);
// 0 success,1 arguments,2 observation coverage,3 native recording,4 actual
// source bindings,5 authenticated PSO family,6 heap identity/description,
// 7 pinned tracking/epoch,8 root layout,9 active-use plan,10 range bounds,
// 11 changed range/recording,12 exception,13 inventory shape/capacity.
// Details: range/heap index (UINT64_MAX when absent), GPU address,
// expected version/cookie/epoch, actual value. Diagnostics grant no permission.
// Code 7 instead returns {UINT64_MAX,reasonMask,pins<<32|inFlightMutations,
// storeOverflowBits<<32|trackingFailure}. reasonMask bits: 0 !pinned,1 !ready,
// 2 epochChanged,3 mutationInFlight,4 versionsDisabled,5 storeOverflow,
// 6 trackingFailure,7 emptyRanges. This is a failure snapshot, not authority.
uint32_t BorrowedRangeLastFailure();
std::array<uint64_t,4> BorrowedRangeLastDetails();
enum class BorrowedFixedStatus:UINT {
    Unknown,TypedNull,Immutable,UnsupportedDescriptor,UnsupportedView,UnsupportedShape,
    MissingCandidate,PinRefused,CookieMismatch,ImmutableRefused
};
struct BorrowedFixedRangeSummary {
    uint64_t list{},generation{},root{},pipeline{},family{};UINT rangeIndex{};
    BorrowedRangeSummary::Range range;
    std::array<uint64_t,10> counts{};bool allSlotsQualified{};
    struct Slot {UINT offset{},asset=UINT_MAX;uint64_t cookie{},candidate{};BorrowedFixedStatus status{};};
    struct Asset {
        uint64_t cookie{},candidate{},actualCookie{};BorrowedFixedStatus status{};
        UINT stage{},failure{};std::array<uint64_t,8> pinDetails{};
    };
    struct Sample {UINT offset{},asset=UINT_MAX;BorrowedFixedStatus status{};Value canonical{};};
    std::vector<Slot> slots;std::vector<Asset> assets;
    std::vector<Sample> samples; // At most four canonical metadata rows per status.
};
class BorrowedFixedRange {
    struct Data;std::shared_ptr<const Data> data_;
    friend bool CaptureBorrowedFixedRange(ID3D12GraphicsCommandList*,const native_recording::Lease&,
        const BorrowedSourceRanges&,UINT,BorrowedFixedRange&);
    friend bool ValidateBorrowedFixedRange(const BorrowedFixedRange&,const BorrowedSourceRanges&,UINT);
    friend bool DescribeBorrowedFixedRange(const BorrowedFixedRange&,BorrowedFixedRangeSummary&);
public:explicit operator bool()const{return bool(data_);}
};
// True means a coherent classified diagnostic, which may be INCOMPLETE. Numeric
// weak candidates are never AddRef'd: native fixed-allocation pins come first,
// then an existing resource cookie is read and immutable history is acquired.
bool CaptureBorrowedFixedRange(ID3D12GraphicsCommandList*,const native_recording::Lease&,
    const BorrowedSourceRanges&,UINT rangeIndex,BorrowedFixedRange&);
// Requires every slot to be typed null or a still-valid owned immutable lease.
// Reuses only held leases and exact descriptor range/canonical values, with no
// late native lookup. SDK residency and shader index/access coverage are separate.
bool ValidateBorrowedFixedRange(const BorrowedFixedRange&,const BorrowedSourceRanges&,UINT rangeIndex);
// Capture-time numeric evidence for partial captures too; never exposes resources
// or permits reads. Reuse requires Validate rather than this saved classification.
// Asset stages:1 native pin,2 held resource,3 cookie,4 immutable,5 recheck.
bool DescribeBorrowedFixedRange(const BorrowedFixedRange&,BorrowedFixedRangeSummary&);
namespace borrowed_fixed_detail {
enum class DescriptorClass {Unknown,TypedNull,Candidate,UnsupportedDescriptor,UnsupportedView,UnsupportedShape};
// Pure metadata classification. Candidate requires native ownership bootstrap;
// it is never an immutable lease or read permission by itself.
DescriptorClass Classify(const Value&,D3D12_SHADER_RESOURCE_VIEW_DESC&)noexcept;
// This validates one explicit view against an ALREADY safely held resource.
// It is shape metadata only and cannot turn a candidate pointer into ownership.
bool ViewMatches(const Value&,const D3D12_RESOURCE_DESC&,const D3D12_SHADER_RESOURCE_VIEW_DESC&)noexcept;
}
void BeforeSubmit(UINT,ID3D12CommandList* const*);
void Reset(ID3D12GraphicsCommandList*);
void Root(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
void Table(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
void ClearBindings(ID3D12GraphicsCommandList*);
// Finds exactly one visible descriptor-table range in the captured root layout.
// UINT_MAX descriptor count is unbounded; the returned offset is still concrete.
bool FindBinding(uint64_t root,D3D12_DESCRIPTOR_RANGE_TYPE,UINT space,UINT shaderRegister,
                 D3D12_SHADER_VISIBILITY,UINT& parameter,UINT& tableOffset);
struct CbvBinding {uint64_t descriptor{},heap{},address{};uint32_t bytes{};};
bool ResolveCbv(ID3D12GraphicsCommandList*,uint64_t root,UINT parameter,UINT tableOffset,CbvBinding&);
bool ResolveCbvAddress(uint64_t descriptor,CbvBinding&);
bool ResolveCpuCbvAddress(uint64_t descriptor,CbvBinding&);
struct DescriptorStamp {uint64_t gpu{},heap{};Value value{};};
// Explicit CBV/SRV metadata only, without retaining the heap or resource. The
// caller owns their lifetimes and revalidates before submission. Copied values
// remain meaningful when live metadata overwrites or recycles internal IDs.
bool CaptureDescriptor(ID3D12GraphicsCommandList*,uint64_t root,UINT parameter,UINT tableOffset,DescriptorStamp&);
bool CaptureCbvSource(ID3D12GraphicsCommandList*,uint64_t root,UINT parameter,UINT tableOffset,
    DescriptorStamp&,Microsoft::WRL::ComPtr<IUnknown>& creationOrigin);
// Resolves the saved GPU slot, independent of the list's later root bindings.
bool MatchesDescriptor(const DescriptorStamp&);
struct SrvBinding {
    Value description{};
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    DescriptorStamp stamp{};
};
// Only at a native draw, while the engine owns its currently bound resources.
// Retains that live buffer/Texture2D/Texture2DArray with an atomic descriptor stamp;
// never resolves a historical capture. Metadata does not prove texture contents.
bool ResolveSrvAtDraw(ID3D12GraphicsCommandList*,uint64_t root,UINT parameter,UINT tableOffset,SrvBinding&);
struct SamplerBinding {DescriptorStamp stamp{};D3D12_SAMPLER_DESC view{};};
// Dynamic sampler tables only. Static samplers have no descriptor slot.
bool ResolveSamplerAtDraw(ID3D12GraphicsCommandList*,uint64_t root,UINT parameter,UINT tableOffset,SamplerBinding&);
struct SourceSlotUse {UINT offset{},count{};D3D12_DESCRIPTOR_RANGE_TYPE type{};};
struct SourceTableRequest {
    UINT parameter{},count{};
    // Caller certifies complete shader-reachable intervals for this EXACT draw.
    // Omitted slots must be proven unused; declaration counts are not bounds.
    std::span<const SourceSlotUse> uses;
};
struct SourceCbvBacking {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<IUnknown> owner;
};
using SourceCbvProvider=bool(*)(void*,ID3D12GraphicsCommandList*,UINT parameter,UINT offset,const DescriptorStamp&,
    const Microsoft::WRL::ComPtr<IUnknown>& creationOrigin,SourceCbvBacking&);
struct SourceResourceBinding {
    UINT parameter{},offset{};
    D3D12_DESCRIPTOR_RANGE_TYPE type{};
    DescriptorStamp stamp;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource,counter;
};
struct OwnedSourceTable {
    UINT parameter{},count{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cpuHeap;
    Microsoft::WRL::ComPtr<IUnknown> owner;
    uint64_t identity{},version{},sourceGpu{},sourceHeap{};
    // Immutable actual-capture qualification, separate from interned storage.
    // Only CaptureSourceTablesAtDraw creates it; absent receipts fail matching.
    Microsoft::WRL::ComPtr<IUnknown> provenance;
};
// Exact earlier captured CPU slot and actual held SRV resource/cookie only.
// Uses immutable private capture/owner receipts, never current descriptor state
// or a historical-resource lookup. No content/read-epoch/execution permission.
bool MatchesCapturedSourceSrv(const OwnedSourceTable&,UINT offset,const SrvBinding&)noexcept;
inline constexpr UINT SourceSlotCaptureLimit=4096;
struct OwnedSourceSlot {
    UINT offset{},compactIndex{};
    D3D12_DESCRIPTOR_RANGE_TYPE type{};
    DescriptorStamp stamp{};
};
struct OwnedSourceSlots {
    UINT parameter{},count{}; // Original table extent, not compact heap size.
    std::vector<OwnedSourceSlot> slots;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cpuHeap;
    Microsoft::WRL::ComPtr<IUnknown> owner;
    uint64_t identity{},version{},sourceGpu{},sourceHeap{};
};
// Exact current canonical value at the saved GPU heap slot, including null SRVs,
// UAVs/counters and samplers. Metadata matching only; no ownership/content grant.
bool MatchesSourceSlot(const OwnedSourceSlot&);
// Original callback only, with the source resource already held by the caller.
// Atomic canonical CPU-SRV equality plus unchanged saved GPU slot; no revival
// of a resource through old metadata and no content/state/copy permission.
bool MatchesCpuSourceSrv(uint64_t cpu,const DescriptorStamp& heldSource);
class SourceTableCache {
public:
    SourceTableCache();
    ~SourceTableCache();
    SourceTableCache(const SourceTableCache&)=delete;
    SourceTableCache& operator=(const SourceTableCache&)=delete;
    void Clear();
    UINT TableCount()const;
    UINT DescriptorCount()const;
    UINT SlotTableCount()const;
    UINT SlotDescriptorCount()const;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
    friend bool CaptureSourceTablesAtDraw(ID3D12Device*,ID3D12GraphicsCommandList*,const scene_state::Bindings&,
        std::span<const SourceTableRequest>,SourceCbvProvider,void*,SourceTableCache&,std::vector<OwnedSourceTable>&,std::vector<SourceResourceBinding>*);
    friend bool CaptureSourceSlotsAtDraw(ID3D12Device*,ID3D12GraphicsCommandList*,const scene_state::Bindings&,
        std::span<const SourceTableRequest>,SourceCbvProvider,void*,SourceTableCache&,std::vector<OwnedSourceSlots>&,std::vector<SourceResourceBinding>*);
};
// Source callback only, while native ownership protects the potentially used
// resources. Creates immutable FLAG_NONE CPU descriptors and holds COM/counter
// owners. Identical captured versions reuse the same CPU heap. No buffer/texture
// content, physical allocation, queue ordering or deferred execution is proved.
// CBV provider runs outside metadata locking; exact source slots are rechecked.
// Optional dependencies receives the exact held SRV/UAV resources and counters,
// including typed null descriptors and original sparse offsets. It must be empty;
// failure leaves it unchanged. These references do not freeze resource contents.
bool CaptureSourceTablesAtDraw(ID3D12Device*,ID3D12GraphicsCommandList*,const scene_state::Bindings&,
    std::span<const SourceTableRequest>,SourceCbvProvider,void*,SourceTableCache&,std::vector<OwnedSourceTable>&,
    std::vector<SourceResourceBinding>* dependencies=nullptr);
// Exact caller-supplied used slots only, sorted/deduplicated by ORIGINAL offset.
// Each immutable FLAG_NONE CPU heap stores only its selected descriptors; holes
// are neither emitted nor initialized. Compact indices cannot replace shader
// indices. Retains live resource/counter/origin owners at the original callback,
// without input-content, allocation, copy, queue-order or execution permission.
// At most SourceSlotCaptureLimit unique selected slots across one capture call.
bool CaptureSourceSlotsAtDraw(ID3D12Device*,ID3D12GraphicsCommandList*,const scene_state::Bindings&,
    std::span<const SourceTableRequest>,SourceCbvProvider,void*,SourceTableCache&,std::vector<OwnedSourceSlots>&,
    std::vector<SourceResourceBinding>* dependencies=nullptr);
struct SourceTableFailure {UINT reason{},parameter=UINT_MAX,offset=UINT_MAX;};
SourceTableFailure LastSourceTableFailure();
}
extern "C" {
__declspec(dllexport) extern uint32_t CyberpunkVR_DescriptorBindingBytes,CyberpunkVR_DescriptorValueBytes;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_DescriptorCaptureSeq,CyberpunkVR_DescriptorDrawCount,CyberpunkVR_DescriptorBindingCount,CyberpunkVR_DescriptorIdCount,CyberpunkVR_DescriptorValueCount,CyberpunkVR_DescriptorCaptureFailure;
__declspec(dllexport) extern uint64_t CyberpunkVR_DescriptorDraws,CyberpunkVR_DescriptorBindings,CyberpunkVR_DescriptorIds,CyberpunkVR_DescriptorValues;
__declspec(dllexport) extern uint64_t CyberpunkVR_DescriptorStoreStats[6];
__declspec(dllexport) extern uint64_t CyberpunkVR_DescriptorHeapSlots;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_DescriptorDenseSlots,CyberpunkVR_DescriptorDenseActive;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_DescriptorLiveMetadata,CyberpunkVR_DescriptorLiveMetadataActive;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_DescriptorPendingLists;
__declspec(dllexport) extern uint32_t CyberpunkVR_DescriptorRootCount;
__declspec(dllexport) extern cvr::stereo::descriptors::RootInfo CyberpunkVR_DescriptorRoots[128];
}
