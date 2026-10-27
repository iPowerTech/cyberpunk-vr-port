#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace cvr::stereo::instance_ring {
struct Code {uint32_t rva{},bytes{};uint64_t fnv{};};
inline constexpr std::array<Code,8> NativeCode{{
    {0x87C0EC,0x162,0x3CE34784B106FB86ull},
    {0x87C250,0x7B,0xE4F4E290C2173E57ull},
    {0x1F3E40,0xDB,0x308C3423D7684BCDull},
    {0x95B308,0x51,0x4251259E7ABD2CB6ull},
    {0x20A778,0x64,0xFF519609A55A97FAull},
    {0x20A7DC,0xDC,0x4BA524A92884A479ull},
    {0x1F7CF4,0x7E,0xA3366F05FF97BF1Full},
    {0x1FDCAC,0xB1,0x2954BAB07D0C4285ull}
}};
inline constexpr uint32_t NormalWriterReturn=0x1F2039,SkinWriterReturn=0x1F026B;

// Numeric observations never acquire a native pointer, pin an allocation, or
// establish producer publication, no-overwrite, submission or GPU copy permission.
// head/tail/capacity are INSTANCE counts, not bytes. Native ring offsets are:
// handle=0,mapped=8,capacity=0x10,stride=0x14,retiredTail=0x38,head=0x40.
struct RingObservation {
    uint64_t ring{},generation{},resource{},gpu{},mapped{},registry{};
    uint64_t allocation{},allocator{},heap{},width{},capacity{},head{},tail{};
    uint32_t handle{},stride{},kind{};
    bool operator==(const RingObservation&)const=default;
};
struct Scope {
    uint64_t list{},generation{},occurrence{},thread{},context{},registry{},stamp{};
    bool operator==(const Scope&)const=default;
};
struct WriterTicket {
    uint64_t id{},observerEpoch{},ring{},ringGeneration{};
    Scope scope;
    bool operator==(const WriterTicket&)const=default;
};
struct CompletedWriter {
    WriterTicket writer;
    uint64_t absoluteBegin{};
    uint32_t firstInstance{},instances{};
};
// Exactly one native 16-byte {submission stamp, absolute head} publication.
// These labels are not a submission/completion certificate.
struct Publication {Scope scope;uint64_t head{};bool operator==(const Publication&)const=default;};
struct ReadRequest {
    WriterTicket writer;
    // Separate source labels. Their association with the exact actual IA
    // resource/range and recording is a native caller proof, not this ledger.
    Scope source;
    uint64_t absoluteBegin{};
    uint32_t firstInstance{},instances{};
    bool operator==(const ReadRequest&)const=default;
};
struct Range {uint64_t absoluteEnd{},byteOffset{},bytes{};};
bool InstanceRange(const RingObservation&,uint64_t absoluteBegin,uint32_t firstInstance,uint32_t instances,Range&)noexcept;

enum class Status:uint32_t {
    Observed,Completed,Published,Recreated,UnknownRing,InvalidObservation,Changed,
    InvalidScope,UnknownWriter,Pending,RangeMismatch,Overlap,Retired,UnknownWrite,Capacity,Exception
};
struct Limits {
    size_t rings=64,pendingWriters=256,ranges=4096,publications=4096;
    size_t writerBytes=65536,liveCpuBytes=16*1024*1024;
};
namespace detail {struct Payload;}
class CpuCandidate {
public:
    explicit operator bool()const noexcept;
    // Remains the immutable observed CPU version after reuse/recreation. Call
    // Query again to test current provenance; held bytes grant no native access.
    std::span<const uint8_t> Bytes()const noexcept;
    const CompletedWriter& Producer()const noexcept;
    const ReadRequest& Source()const noexcept{return source_;}
    const Publication& Published()const noexcept{return publication_;}
private:
    std::shared_ptr<const detail::Payload> data_;
    ReadRequest source_;
    Publication publication_;
    size_t offset_{},bytes_{};
    friend class Ledger;
};
// Caller serializes ledger operations. Complete accepts only bytes sampled from
// the actual mapped destination AFTER the genuine native writer returned. It
// copies them, never reads native memory, and cannot certify that sampling fact.
// Genuine backing ownership, producer/source joins and the original GPU read
// epoch remain external caller proofs. No operation grants copy permission.
class Ledger {
public:
    explicit Ledger(Limits={});
    ~Ledger();
    Ledger(const Ledger&)=delete;Ledger& operator=(const Ledger&)=delete;
    Status Observe(const RingObservation&)noexcept;
    Status Begin(const RingObservation&,const Scope&,WriterTicket&)noexcept;
    Status QualifyWrite(const RingObservation&,const WriterTicket&,uint64_t absoluteBegin,uint32_t firstInstance,uint32_t instances)noexcept;
    Status ValidateWrite(const WriterTicket&)const noexcept;
    // Only after this exact native writer has returned. Its known range was
    // invalidated before writing; dropping its sample preserves other writers.
    Status DiscardCompleted(const WriterTicket&)noexcept;
    // A guarded native cached-write branch supplies a physical range even if
    // its earlier absolute reservation was not observed. It invalidates only.
    Status InvalidatePhysical(const RingObservation&,uint32_t firstInstance,uint32_t instances)noexcept;
    Status FinishUnobserved(const RingObservation&,const WriterTicket&,uint32_t firstInstance,uint32_t instances)noexcept;
    Status Complete(const RingObservation&,const CompletedWriter&,std::span<const uint8_t> observedMappedBytes)noexcept;
    Status Publish(const RingObservation&,const Publication&)noexcept;
    // Clears local CPU provenance after a genuine NEW native reservation. It
    // keeps the same native lifecycle generation; numeric Observe cannot do it.
    Status RestartObservation(const RingObservation&)noexcept;
    // An actual reservation/reuse observation invalidates intersecting prior
    // versions. The absolute range must come from the native reservation, not
    // an inference from a later head or a matching physical address.
    Status Reuse(const RingObservation&,uint64_t absoluteBegin,uint32_t instances,const WriterTicket* activeWriter=nullptr)noexcept;
    void Unknown(uint64_t ring)noexcept;
    void Destroy(uint64_t ring)noexcept;
    void Reset()noexcept;
    Status Query(const RingObservation&,const ReadRequest&,CpuCandidate&)noexcept;
    // Tracks cache membership only, not permission to read native memory.
    // Expires on eviction/retirement even while immutable CPU bytes are held.
    std::weak_ptr<const WriterTicket> CompletedPresence(const WriterTicket&)const noexcept;
    size_t LiveCpuBytes()const noexcept;
private:
    struct Data;std::unique_ptr<Data> data_;
};
}
