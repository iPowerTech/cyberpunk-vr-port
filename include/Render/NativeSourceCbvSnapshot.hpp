#pragma once
#include "Render/NativeSourceCbvDetails.hpp"
#include "Render/NativeIndirectCopy.hpp"
#include "Render/NativeBufferRead.hpp"
#include "Render/RegisteredConstantUpload.hpp"
#include <span>
#include <vector>

namespace cvr::stereo::source_cbv_snapshot {
enum class Failure:uint32_t {None,Disabled,Arguments,Scope,Code,Parent,Source,Creation,Class,State,Allocation,Pool,Range,
    Capacity,Changed,Retention,Registration,RecoveryVerified,RecoveryFatal,AlreadyRecorded,Exception};
struct Request {source_cbv::SourceCbvBacking source;UINT parameter{},offset{},reflectedBytes{};};
// Supplied only by the trusted original-source callback. This is the caller's
// valid original GPU read-epoch certificate, including producer publication
// and source protection through that original submission, never metadata alone.
struct ReadEpochCertificate {uint64_t list{},generation{},occurrence{};bool certified{};};
struct PrivateCbv {
    UINT parameter{},offset{},reflectedBytes{},paddedBytes{};
    uint64_t sourceOffset{},privateOffset{},copiedBytes{};
    D3D12_CONSTANT_BUFFER_VIEW_DESC view{};
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<IUnknown> owner;
};
struct PreparedCopies {
    Microsoft::WRL::ComPtr<IUnknown> owner;
    uint64_t list{},generation{},occurrence{},copiedBytes{};UINT count{};
};
namespace detail {
inline constexpr UINT MaxRequests=28;
inline constexpr uint64_t MaxArenaBytes=64*1024*1024;
enum class Kind:uint32_t {Unknown,Upload,Pool,Tracked};
struct Admission {
    uint64_t resource{},gpu{},width{},comOffset{},nativeGpu{},nativeBytes{},creationHeap{},creationBytes{},creationOffset{};
    uint64_t allocation{},allocationResource{},allocationBytes{},allocationHeap{},allocatorPimpl{},allocationPimpl{};
    uint32_t reflectedBytes{},viewBytes{},heap{},flags{},nativeKind{},handle{},creator{},creationKind{},initialState{},allocationKind{};
    geometry::ReadState state;
    bool sourceCurrent{},creationKnown{},initialKnown{},detailKnown{},stateKnown{},poolKnown{},allocationKnown{},allocatorKnown{},direct{},aliasFieldsEmpty{};
};
inline bool Range(const Admission& a)noexcept {
    if(!a.resource || !a.gpu || !a.width || a.gpu>UINT64_MAX-a.width || !a.reflectedBytes || a.reflectedBytes>65536 ||
        !a.viewBytes || a.viewBytes>65536 || (a.viewBytes&255) || a.reflectedBytes>a.viewBytes ||
        a.comOffset>a.width || a.viewBytes>a.width-a.comOffset || ((a.gpu+a.comOffset)&255))return false;
    if(a.handle && a.nativeKind==4)return a.nativeBytes && a.reflectedBytes<=a.nativeBytes;
    return !a.handle || (a.nativeGpu && a.nativeBytes && a.nativeGpu>=a.gpu && a.nativeGpu-a.gpu<=a.width &&
        a.nativeBytes<=a.width-(a.nativeGpu-a.gpu) && a.gpu+a.comOffset>=a.nativeGpu &&
        a.gpu+a.comOffset-a.nativeGpu<=a.nativeBytes && a.reflectedBytes<=a.nativeBytes-(a.gpu+a.comOffset-a.nativeGpu));
}
inline bool ReadMask(geometry::ReadState state,bool copy=false)noexcept {
    return state.status==2 && state.kind==3 && (state.value&D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) &&
        !(state.value&~UINT(D3D12_RESOURCE_STATE_GENERIC_READ)) && (!copy || (state.value&D3D12_RESOURCE_STATE_COPY_SOURCE));
}
inline bool RecoveryMask(uint32_t previous,geometry::ReadState current,uint32_t& required)noexcept {
    required=0;if(!(previous&D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) || (previous&~UINT(D3D12_RESOURCE_STATE_GENERIC_READ)) ||
        current.status!=2 || current.kind!=3 || (current.value&~UINT(D3D12_RESOURCE_STATE_GENERIC_READ)))return false;
    required=previous|current.value|D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;return true;
}
inline Failure Admit(const Admission& a,Kind& kind)noexcept {
    kind=Kind::Unknown;if(!a.sourceCurrent || !a.detailKnown)return Failure::Source;
    if(!a.creationKnown || !a.initialKnown)return Failure::Creation;
    if(a.flags || !a.stateKnown)return Failure::State;
    if(!Range(a))return Failure::Range;
    if(a.heap==D3D12_HEAP_TYPE_UPLOAD){
        if(a.creationKind!=1 || a.creationHeap || a.initialState!=D3D12_RESOURCE_STATE_GENERIC_READ ||
            a.state.status!=1 || a.state.value!=D3D12_RESOURCE_STATE_GENERIC_READ ||
            !((!a.handle && a.creator==0x1F2960) || (a.handle && a.nativeKind==4 && a.creator==0x1F0BF7)))return Failure::Class;
        kind=Kind::Upload;return Failure::None;
    }
    if(a.heap!=D3D12_HEAP_TYPE_DEFAULT || !a.handle || (a.nativeKind!=1 && a.nativeKind!=3))return Failure::Class;
    if(!a.allocationKnown || !a.allocatorKnown || !a.allocation || a.allocationResource!=a.resource || a.allocationBytes<a.width ||
        !a.allocationPimpl || a.allocatorPimpl!=a.allocationPimpl)return Failure::Allocation;
    if(a.nativeKind==1){
        if(a.creationKind!=1 || a.creationHeap || a.allocationKind!=0 || a.allocationHeap || !a.poolKnown || a.nativeBytes>512 ||
            (a.initialState!=D3D12_RESOURCE_STATE_COMMON && a.initialState!=D3D12_RESOURCE_STATE_GENERIC_READ) ||
            (a.state.status!=8 && a.state.status!=9) || a.state.kind!=1 || a.state.value!=D3D12_RESOURCE_STATE_GENERIC_READ)return Failure::Pool;
        kind=Kind::Pool;return Failure::None;
    }
    if(a.creationKind!=2 || a.allocationKind!=1 || !a.creationHeap || a.creationHeap!=a.allocationHeap || !a.creationBytes ||
        a.creationBytes!=a.allocationBytes || a.creationOffset>UINT64_MAX-a.creationBytes || !a.direct || !a.aliasFieldsEmpty ||
        a.initialState!=D3D12_RESOURCE_STATE_COMMON || a.comOffset || a.nativeGpu!=a.gpu || a.viewBytes!=a.width ||
        a.nativeBytes>UINT64_MAX-255 || ((a.nativeBytes+255)&~uint64_t(255))!=a.width)return Failure::Allocation;
    if(!ReadMask(a.state))return Failure::State;kind=Kind::Tracked;return Failure::None;
}
inline bool Reserve(uint64_t cursor,uint64_t capacity,std::span<const uint32_t> views,std::vector<uint64_t>& offsets,uint64_t& end){
    offsets.clear();end=cursor;if(!capacity || capacity>MaxArenaBytes || cursor>capacity || (cursor&255) || views.empty() || views.size()>MaxRequests)return false;
    std::vector<uint64_t> next;next.reserve(views.size());for(const auto bytes:views){
        if(!bytes || bytes>65536 || (bytes&255) || bytes>capacity-end)return false;next.push_back(end);end+=bytes;
    }offsets=std::move(next);return true;
}
inline bool Epoch(const ReadEpochCertificate& epoch,uint64_t list,uint64_t generation,uint64_t occurrence)noexcept {
    return epoch.certified && list && generation && occurrence && epoch.list==list && epoch.generation==generation && epoch.occurrence==occurrence;
}
inline bool RecordedCopyFields(const PreparedCopies& proof,const ReadEpochCertificate& epoch,uint64_t copied,size_t count)noexcept{
    return Epoch(epoch,proof.list,proof.generation,proof.occurrence) && copied && count && count<=MaxRequests &&
        proof.copiedBytes==copied && proof.count==count;
}
struct PrivateCbvIdentity {
    uint64_t resource{},owner{},sourceOffset{},privateOffset{},copiedBytes{},gpu{};
    UINT parameter{},offset{},reflectedBytes{},paddedBytes{},viewBytes{};
    bool operator==(const PrivateCbvIdentity&)const=default;
};
inline PrivateCbvIdentity PrivateIdentity(const PrivateCbv& view)noexcept{
    return {reinterpret_cast<uintptr_t>(view.resource.Get()),reinterpret_cast<uintptr_t>(view.owner.Get()),
        view.sourceOffset,view.privateOffset,view.copiedBytes,view.view.BufferLocation,
        view.parameter,view.offset,view.reflectedBytes,view.paddedBytes,view.view.SizeInBytes};
}
// Numeric equality/extent only. Runtime must first authenticate the receipt and
// completed recording stage; this helper cannot manufacture copy authority.
inline bool RecordedPrivateIdentity(const PrivateCbvIdentity& actual,const PrivateCbvIdentity& saved,
                                    uint64_t resource,uint64_t owner,uint64_t gpu,uint64_t cursor,uint64_t capacity)noexcept{
    return actual==saved && resource && owner && gpu && capacity && capacity<=MaxArenaBytes && cursor && cursor<=capacity &&
        gpu<=UINT64_MAX-capacity && actual.resource==resource && actual.owner==owner && actual.parameter<64 &&
        actual.reflectedBytes && actual.reflectedBytes<=65536 && actual.copiedBytes==actual.reflectedBytes &&
        actual.paddedBytes && actual.paddedBytes<=65536 && !(actual.paddedBytes&255) &&
        actual.viewBytes==actual.paddedBytes && actual.reflectedBytes<=actual.paddedBytes && !(actual.privateOffset&255) &&
        actual.privateOffset<=cursor && actual.paddedBytes<=cursor-actual.privateOffset &&
        actual.gpu==gpu+actual.privateOffset;
}
inline bool InlineDirectUpload(const source_cbv::SourceCbvBacking& source,const ReadEpochCertificate& epoch,UINT bytes)noexcept {
    const auto& created=source.creationRecording;const auto& original=source.recording;
    return source.creatorRva==0x1F2960 && source.completedProducerRva==0x1F292F && bytes &&
        bytes<=source.logicalBytes && source.logicalBytes<=65536 && source.reservedBytes==source.descriptorBytes &&
        source.reservedBytes==((uint64_t(source.logicalBytes)+255)&~uint64_t(255)) &&
        created.token && original.token && created.token==original.token && created.list==original.list &&
        created.generation==original.generation && created.context==original.context && created.registry==original.registry &&
        Epoch(epoch,original.list,original.generation,epoch.occurrence);
}
inline bool InlineTransientUpload(const source_cbv::SourceCbvBacking& source,const ReadEpochCertificate& epoch,UINT bytes)noexcept {
    const auto& created=source.creationRecording;const auto& written=source.writerRecording;const auto& original=source.recording;
    return source.creatorRva==0x1F0BF7 && source.completedProducerRva==0x1F08C2 && source.native.kind==4 && source.native.handle &&
        source.writerNonce && source.writerDestination && bytes && bytes<=source.writerBytes && source.writerBytes<=source.logicalBytes &&
        source.logicalBytes<=65536 && source.reservedBytes==source.descriptorBytes &&
        source.reservedBytes==((uint64_t(source.logicalBytes)+255)&~uint64_t(255)) &&
        created.token && written.token && original.token && created.token==original.token && written.token==original.token &&
        created.list==original.list && written.list==original.list && created.generation==original.generation && written.generation==original.generation &&
        created.context==original.context && written.context==original.context && created.registry==original.registry && written.registry==original.registry &&
        Epoch(epoch,original.list,original.generation,epoch.occurrence);
}
inline bool InlineUpload(const source_cbv::SourceCbvBacking& source,const ReadEpochCertificate& epoch,UINT bytes)noexcept {
    return InlineDirectUpload(source,epoch,bytes) || InlineTransientUpload(source,epoch,bytes);
}
}
// Capture original retirement/signal bodies before any native detours change
// them. The inline UPLOAD route inherits, but never extends, the original epoch.
void PrepareUploadCodeGuards(uintptr_t gameBase)noexcept;
bool UploadCompatible()noexcept;
class ActualCallScope {
public:
    ActualCallScope(ID3D12GraphicsCommandList*,uint64_t generation,uint64_t occurrence)noexcept;
    ~ActualCallScope();
    ActualCallScope(const ActualCallScope&)=delete;ActualCallScope& operator=(const ActualCallScope&)=delete;
    bool Active()const noexcept;
private:indirect_copy::detail::ScopeStamp stamp_;
};
class SnapshotArena {
public:
    SnapshotArena();~SnapshotArena();
    SnapshotArena(const SnapshotArena&)=delete;SnapshotArena& operator=(const SnapshotArena&)=delete;
    HRESULT Create(ID3D12Device*,ID3D12GraphicsCommandList*,uint64_t generation,uint64_t capacityBytes);
    // Called at native Seal, after the last copy. GENERIC_READ permits CBV use,
    // GPU selector comparison and diagnostic copies without read-state cycling.
    bool Finalize(ID3D12GraphicsCommandList*);
    bool Finalized()const;uint64_t Bytes()const;
private:
    struct Storage;Microsoft::WRL::ComPtr<Storage> storage_;
    friend bool PrepareAtActualCall(ID3D12GraphicsCommandList*,uint64_t,std::span<const Request>,const preparation::GraphParentRecord&,
        const ReadEpochCertificate&,SnapshotArena&,PreparedCopies&);
};
// Opt-in and current original source occurrence only. Proves/allocates/pins the
// COMPLETE request set before any native registration or GPU command. The caller
// certifies original GPU read ordering and complete reflected source use. No
// alias/GPU_UPLOAD/unobserved class admission, CPU read/wait or native suppression.
bool PrepareAtActualCall(ID3D12GraphicsCommandList*,uint64_t root,std::span<const Request>,
    const preparation::GraphParentRecord&,const ReadEpochCertificate&,SnapshotArena&,PreparedCopies&);
bool ValidateAtActualCall(ID3D12GraphicsCommandList*,const PreparedCopies&);
bool RecordAtActualCall(ID3D12GraphicsCommandList*,const PreparedCopies&,std::vector<PrivateCbv>&);
// Authenticates an exact already-recorded private slice and finalized arena.
// Does not wait for GPU completion or inspect a later native source. The caller
// separately proves queue producer order, residency and execution permission.
bool MatchesRecordedPrivateCbv(const PreparedCopies&,const PrivateCbv&)noexcept;
bool ValidateSourceSubmission(const PreparedCopies&,ID3D12CommandQueue*);
Failure LastFailure();
}
extern "C" {__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_SourceCbvSnapshotsEnable,CyberpunkVR_SourceCbvSnapshotsUploadEnable;}
