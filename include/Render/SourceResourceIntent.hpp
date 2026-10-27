#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>
#include <span>
#include <vector>

namespace cvr::stereo::source_resource_intent {
// Global pressure evicts ordinary copies without current structured coverage
// first, then small copies of those owners, then oldest copies across all owners. Per-resource
// byte limits remain shared. Small actual copies have a separate bounded
// record budget; size does not classify them as CBVs or prove a GPU epoch.
inline constexpr size_t MaxCopyBytes=65536,MaxResourceBytes=2*1024*1024,MaxGlobalBytes=32*1024*1024;
inline constexpr size_t MaxRangesPerResource=512,MaxOwners=4096,MaxPayloads=65536;
inline constexpr size_t MaxSmallCopyBytes=512,MaxSmallRangesPerResource=8192;
inline constexpr size_t MaxGenericRangesPerResource=MaxRangesPerResource+MaxSmallRangesPerResource;
inline constexpr size_t MaxStructuredRangesPerResource=65536;
namespace detail {struct Payload;}
// Actual CPU-observed copy labels, with the exact extent selected from that
// copy. These carry no command submission, queue or GPU-read certificate.
struct PieceReceipt {
    uint64_t producerList{},copySequence{},sourceResource{},destinationOffset{},sourceOffset{},copiedBytes{};
    uint64_t pieceDestinationOffset{},pieceSourceOffset{},pieceBytes{};
};
static_assert(sizeof(PieceReceipt)==9*sizeof(uint64_t));
class Candidate {
public:
    std::span<const uint8_t> Bytes()const noexcept;
    // Observation labels only, never recording, submission or GPU-read receipts.
    uint64_t ProducerList()const noexcept;
    uint64_t CopySequence()const noexcept;
    uint64_t DestinationOffset()const noexcept;
    uint64_t SourceOffset()const noexcept;
    uint64_t ViewOffset()const noexcept;
    size_t CopiedBytes()const noexcept;
    bool IsComposed()const noexcept;
    std::span<const PieceReceipt> Pieces()const noexcept;
    ID3D12Resource* Destination()const noexcept;
    explicit operator bool()const noexcept;
private:
    std::shared_ptr<const detail::Payload> data_;
    Microsoft::WRL::ComPtr<ID3D12Resource> destination_;
    std::vector<PieceReceipt> pieces_;
    size_t dataOffset_{},viewOffset_{},viewBytes_{};
    bool composed_{};
    friend bool CaptureAtDraw(const Microsoft::WRL::ComPtr<ID3D12Resource>&,uint64_t,size_t,Candidate&);
    friend bool CaptureStructuredAtDraw(const Microsoft::WRL::ComPtr<ID3D12Resource>&,uint64_t,size_t,Candidate&);
};
struct Statistics {
    uint64_t currentBytes{},rangeCount{},livePayloadBytes{},livePayloads{},ownerCount{};
    uint64_t copySequence{},evictions{},captures{},rejected{},generation{};
    uint64_t structuredBytes{},structuredRanges{};
};
enum class DiagnosticOutcome:uint64_t {Started,Success,Disabled,Arguments,UnsupportedDestination,SourceDefault,
    UnsupportedHeap,NoHint,MapFailed,Unstable,Budget,GenerationChanged,Changed,Capacity,ApiFailed,InvalidView};
enum class DiagnosticStage:uint64_t {Begin,Destination,Hint,Source,FirstSample,SecondSample,Compare,Publication,Complete};
struct CopyDiagnostic {
    uint64_t version{},resource{},sequence{},generation{},publicationGeneration{},producerList{},sourceResource{};
    uint64_t destinationOffset{},sourceOffset{},bytes{},destinationGpu{},destinationWidth{};
    uint64_t sourceHeapType{},sourceCpuPage{},sourceHeapFlags{},hintPresent{},hintGeneration{},hintFresh{},hintOffset{},hintBytes{};
    uint64_t outcome{},stage{},reserved{},checksum{};
};
struct MarkDiagnostic {
    uint64_t version{},resource{},sequence{},generation{},outcome{},firstElement{},numElements{},stride{},format{},dimension{};
    uint64_t componentMapping{},flags{},viewOffset{},viewBytes{},resourceGpu{},resourceWidth{},heapType{},cpuPage{},heapFlags{};
    uint64_t hintAccepted{},stage{},reserved[2]{},checksum{};
};
static_assert(sizeof(CopyDiagnostic)==24*sizeof(uint64_t) && sizeof(MarkDiagnostic)==24*sizeof(uint64_t));
struct StructuredCoverageDiagnostic {
    uint64_t version{},resource{},generation{},ownerPresent{};
    uint64_t markerGeneration{},markerFresh{},ownerHintGeneration{},ownerHintMatchesMarker{};
    uint64_t viewOffset{},viewBytes{},structuredBytes{},structuredRanges{};
    uint64_t queryOffset{},queryBytes{},coveredBytes{},firstHoleOffset{UINT64_MAX};
};
static_assert(sizeof(StructuredCoverageDiagnostic)==16*sizeof(uint64_t));
enum class GenericOperation:uint64_t {None,Invalidate,InvalidateRange,ObserveCopy,RecordIntendedCopy,Clear,OwnerEviction,GenericEviction,ResourceDestroyed};
struct GenericCoverageDiagnostic {
    uint64_t version{},resource{},generation{},ownerPresent{},rangeCount{},livePayloadBytes{},queryOffset{},queryBytes{};
    uint64_t overlapCount{},singleCoverCount{},coveredBytes{},firstHoleOffset{UINT64_MAX},oldestSequence{},newestSequence{};
    uint64_t fullInvalidations{},rangeInvalidations{},genericEvictions{},lastInvalidationOffset{},lastInvalidationBytes{};
    uint64_t lastOperation{},lastOperationSequence{},lastOperationSource{},lastOperationProducerList{},lastOperationGeneration{};
};
static_assert(sizeof(GenericCoverageDiagnostic)==24*sizeof(uint64_t));
// Current generic ranges only, with bounded union coverage and PRESENT copy
// sequence extrema. Counters/last-operation labels contain no historical bytes
// and grant no recording, submission or GPU epoch certificate. Query creates
// no owner; livePayloadBytes includes immutable captures sharing its budget.
bool QueryGenericCoverage(const Microsoft::WRL::ComPtr<ID3D12Resource>&,
    uint64_t offset,size_t bytes,GenericCoverageDiagnostic& output)noexcept;
inline constexpr size_t MaxCacheEventsPerOwner=64;
enum class CacheEventReason:uint64_t {None,PublishedCopy,ObserveCopyInvalidation,ExplicitRangeInvalidation,ExplicitFullInvalidation,
    DirectCopyInvalidation,OverwrittenPayload,Clear,OwnerCapacityEviction,GlobalBytesEviction,GlobalPayloadEviction,
    ResourceBytesEviction,GenericRangeEviction,ResourceDestroyed,RejectedBase=32};
// RejectedBase + DiagnosticOutcome identifies the actual rejected observation.
struct CacheEventReceipt {
    uint64_t generation{},sourceResource{},producerList{},offset{},bytes{},copySequence{},reason{},eventSequence{};
};
struct CacheEventDiagnostic {
    uint64_t version{},resource{},generation{},ownerPresent{},queryOffset{},queryBytes{},storedEvents{},droppedEvents{};
    CacheEventReceipt lastPublishedCopy,lastInvalidated,lastEvicted;
};
static_assert(sizeof(CacheEventReceipt)==8*sizeof(uint64_t) && sizeof(CacheEventDiagnostic)==32*sizeof(uint64_t));
// Metadata only, at most64 events for each registered owner (at most4096).
// Clear/owner retirement release storage and mark discarded history. Queries
// never create owners, acquire past resources or return historical copy bytes.
bool QueryCacheEvents(const Microsoft::WRL::ComPtr<ID3D12Resource>&,
    uint64_t offset,size_t bytes,CacheEventDiagnostic& output)noexcept;
// Current exact-resource coverage only. A valid bounded query succeeds even
// without an owner; coveredBytes then stays zero. UINT64_MAX means no hole.
// No owner/data is created, no historical bytes revived, and no GPU claim made.
bool QueryStructuredCoverage(const Microsoft::WRL::ComPtr<ID3D12Resource>&,
    uint64_t offset,size_t bytes,StructuredCoverageDiagnostic& output)noexcept;
// Latest actual callback labels only. Both POD receipts validate checksum,
// version and the exact caller-held resource; no historical object is acquired.
bool QueryCopyDiagnostic(const Microsoft::WRL::ComPtr<ID3D12Resource>&,CopyDiagnostic& output)noexcept;
bool QueryMarkDiagnostic(const Microsoft::WRL::ComPtr<ID3D12Resource>&,MarkDiagnostic& output)noexcept;
// Caller has already sampled the exact UPLOAD source twice. This records only
// the current CPU-intended copy candidate, independently of any position ledger.
// First private-owner attachment pins this diagnostic module until process exit;
// Clear drops data but attached resource-private interfaces retain its vtables.
bool RecordIntendedCopy(ID3D12GraphicsCommandList*,ID3D12Resource* destination,uint64_t destinationOffset,
    uint64_t sourceOffset,std::span<const uint8_t> stableSample);
// Current generation, exact resource, explicit structured128 SRV only. This
// typed sampling hint grants no native read/state/allocation/epoch permission.
// Large copies must fit inside the marked view; ordinary copies stay <=64KiB.
bool MarkStructuredView(ID3D12Resource*,const D3D12_SHADER_RESOURCE_VIEW_DESC*,const std::atomic<uint32_t>* enabled=nullptr);
// Standalone sampler: actual UPLOAD only; failed/unknown writes invalidate the
// overlapping current range before any source read or rejection. One copy up
// to2MiB may be sampled only with a fresh typed hint; candidate views stay64KiB.
bool ObserveCopy(ID3D12GraphicsCommandList*,ID3D12Resource* destination,uint64_t destinationOffset,
    ID3D12Resource* source,uint64_t sourceOffset,uint64_t bytes,const std::atomic<uint32_t>* enabled=nullptr);
void Invalidate(ID3D12Resource* destination);
void Invalidate(ID3D12Resource* destination,uint64_t offset,uint64_t bytes);
// One covering copy only. Partial overwrites discard the prior complete copy;
// disjoint fragments are never combined into a fabricated candidate.
bool CaptureAtDraw(const Microsoft::WRL::ComPtr<ID3D12Resource>& heldDestination,
    uint64_t address,size_t bytes,Candidate& output);
// Fresh exact structured128 marker only, <=64KiB, and every selected byte must
// come from a successful observed UPLOAD copy. Partial/unknown writes remove
// only their overwritten structured coverage. Different copies may compose a
// provisional CPU snapshot; IsComposed/Pieces retain that distinction, and all
// single-copy scalar labels are zero for a composed snapshot.
bool CaptureStructuredAtDraw(const Microsoft::WRL::ComPtr<ID3D12Resource>& heldDestination,
    uint64_t address,size_t bytes,Candidate& output);
// Drops current ranges through a bounded weak owner registry. Captured immutable
// candidates may keep payload bytes and their destination alive until released.
void Clear();
Statistics GetStatistics();
uint64_t CurrentBytes();
uint64_t RangeCount();
}
