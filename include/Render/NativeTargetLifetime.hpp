#pragma once
#include "Render/NativeTextureRead.hpp"
#include "Render/NativeQueueResidencyScope.hpp"
#include "Render/NativeSourceCbvDetails.hpp"
#include <climits>

namespace cvr::stereo::target_lifetime {
namespace detail {
using ReadMemory=queue_residency::detail::ReadMemory;
inline constexpr uint64_t RetirementRva=0x3AA67C0,RegistryRva=0x3438A28;
inline constexpr uint32_t RingSize=32769;
inline constexpr uint64_t SwapchainTableOffset=0xC97F20,RegistryLockOffset=0x2F180;
inline constexpr uint32_t SwapchainCount=32;
inline constexpr uint32_t ResizeRva=0x87466C,ResizeBytes=1042;
inline constexpr uint64_t ResizeHash=0xACD5BEDEE6CECC40ull,ResizeSuffixHash=0xFA1423D5B68EE457ull;
inline constexpr uint32_t CetResizeRva=0x2BD10,CetResizeBytes=245,CetResetRva=0xAA90,CetResetBytes=463;
inline constexpr uint64_t CetResizeHash=0x3F494FE96A691BB0ull,CetResetHash=0xD16377FED0A771ACull;
inline constexpr uint32_t ShutdownRva=0x10E02B4,ShutdownBytes=112,CetShutdownRva=0x2BE10,CetShutdownBytes=107;
inline constexpr uint64_t ShutdownHash=0x629A9859BE5E0E56ull,ShutdownSuffixHash=0xF19A4C0DC1521A40ull;
inline constexpr uint64_t CetShutdownHash=0x6EB46F90E0FD5752ull;
struct HookContract {
    uint32_t nativeRva,nativeBytes;uint64_t nativeHash,suffixHash;
    uint32_t wrapperRva,wrapperBytes;uint64_t wrapperHash,originalOffset;
    std::array<uint8_t,7> stolen;uint32_t stolenBytes;
};
inline constexpr HookContract ResizeHook{ResizeRva,ResizeBytes,ResizeHash,ResizeSuffixHash,
    CetResizeRva,CetResizeBytes,CetResizeHash,0x2410,{0x48,0x8B,0xC4,0x44,0x88,0x48,0x20},7};
inline constexpr HookContract ShutdownHook{ShutdownRva,ShutdownBytes,ShutdownHash,ShutdownSuffixHash,
    CetShutdownRva,CetShutdownBytes,CetShutdownHash,0x2418,{0x40,0x53,0x48,0x83,0xEC,0x20,0},6};
// Full native bodies, including out-of-line chunks. NativeCommandRecording
// separately authenticates acquisition and the surrounding submit/return path.
inline constexpr std::array<native_recording::detail::Code,49> Contract{{
    {0x1FA164,145,0x61B5A10A99C316E9ull},{0xB2C460,175,0x86061566D5EC0526ull},
    {0x91C59C,185,0x0990890D546C54BDull},{0x91C894,242,0x073F687FA3D6BF90ull},
    {0xA34594,624,0x74155FEF74957E23ull},{0x1F4D0DC,536,0x5B26C8D1451D84A3ull},
    {0x91C4A0,195,0x773420FC3024FE47ull},{0x91C814,38,0xB56EF3B145CCE822ull},
    {0x6480E4,200,0xAA9F0C6396A47195ull},{0x1EC96DA,13,0x20E18F032BE94718ull},
    {0x6483F8,438,0xF20D07F3C00D184Full},{0x6485D4,806,0x9667FCCC34D1C033ull},
    {0x6485B0,36,0xDB62FD1AEF42DA72ull},{0x22318C,97,0x588D9705765208D0ull},
    {0x8776E0,80,0xE0139C76EE006880ull},{0x8765B8,346,0x6E1062B700B72556ull},
    {0x876714,682,0x920B1746CE34F233ull},{0x876E98,24,0x20DF5B92407C73E4ull},
    {0x21FF18,35,0xAFAD89E225DAE8EDull},{0x21FF3C,38,0xD8310396C182F071ull},
    {0x1444C4,41,0x86F34059C9A7E048ull},{0x21FEAC,55,0x5137C7DA093DD0D4ull},
    {0x222E2C,133,0xDFEDCE894319584Eull},{0x2A3F7CC,135,0x38FCAFE70967B57Aull},
    // The dynamic chain checks separately admit only original entries or audited
    // CET entry/relay/wrapper/original-trampoline chain. The ownership argument
    // still excludes all 32 swapchain texture handles, including inactive slots.
    {0x874671,1037,0xFA1423D5B68EE457ull},{0x221930,771,0x51BA1810B87B452Eull},
    {0x91BE60,247,0x5D919F0B54F96AE8ull},{0x91F158,229,0x4BC3EFD38EC58363ull},
    {0x1E3DAB4,7,0x40717C7A89366AC2ull},{0x18E9720,13,0x4B71F66010D54C4Dull},
    {0x8780A4,33,0xEFE63006A4D426CBull},{0x8780C8,195,0x8DFB4CD8294CBA43ull},
    {0x878238,33,0x7E4E66C9F9AA2F81ull},{0x1892658,105,0xFB511668CDBFA21Cull},
    {0x18925EC,52,0x95650F6E1318A268ull},{0x2A462D0,57,0x2BA6EF10E4DE61C9ull},
    {0x1FB564,333,0x589B9E0C5E74800Full},
    {0x91EA38,102,0xAE0343B16C3C8BE1ull},{0x8721C4,114,0xCEA9048EE2A9A579ull},
    {0x873DE4,1302,0x55625DB1357B5061ull},{0x10E0208,160,0x6E2F847D51A0038Eull},
    {0x10E00CC,282,0x4F2C2E002AAAEF4Full},{0x18CE7BC,160,0xE1422569D46719B7ull},
    {0x10E02B9,107,0xF19A4C0DC1521A40ull},{0x21C820,187,0xB34407874FE48086ull},
    {0x1D823D0,392,0x6026D355662DB891ull},{0x1D82560,177,0x9521C42D0BB82F9Bull},
    {0x10E01E8,30,0xC61C709CD0D405E6ull},{0x10E02A8,9,0x83A0AB8395F967AFull}
}};
struct Key {
    uint64_t registry{},context{},list{},resource{},heap{},bytes{},alignment{};
    uint32_t handle{};
    bool operator==(const Key&)const=default;
};
struct Epoch {
    uint64_t address{},bucket{},fence{},value{},reserved{},head{},committed{};
    uint32_t index{},count{},outstanding{};
    bool operator==(const Epoch&)const=default;
};
struct Swapchains {
    uint64_t table{},lock{};uint32_t freeCount{};
    std::array<int32_t,SwapchainCount> references{};
    std::array<uint32_t,SwapchainCount> handles{};
    bool operator==(const Swapchains&)const=default;
};
struct Snapshot {
    uint64_t base{},record{},owner{},queueSlot=UINT64_MAX;
    int32_t handleReferences{},ownerReferences{};
    Key key;Epoch epoch;Swapchains swapchains;
    std::array<uint8_t,0x38> control{};
    source_cbv_details::Allocation allocation;
    source_cbv_details::Allocator allocator;
};
template<class T> bool Field(ReadMemory read,void* user,uint64_t address,uint64_t offset,T& output){
    return queue_residency::detail::Field(read,user,address,offset,output);
}
struct ResizeChain {
    uint64_t relay{},cet{},singleton{},trampoline{};uint32_t mode{};
    bool operator==(const ResizeChain&)const=default;
};
inline bool RelativeTarget(uint64_t next,int32_t displacement,uint64_t& output){
    output=0;
    if(displacement<0){const auto distance=uint64_t(-int64_t(displacement));if(next<distance)return false;output=next-distance;}
    else{if(next>UINT64_MAX-uint32_t(displacement))return false;output=next+uint32_t(displacement);}
    return output!=0;
}
// This is compatibility with one installed CET implementation, not a lifetime
// proof for arbitrary third-party code. No pointer in this chain is executed.
// Rejections: 1 native read, 2 entry/body, 3 relative target, 4 relay, 5 module,
// 6 wrapper, 7 ResetState, 8 singleton, 9 original pointer, 10 trampoline,
// 11 changing chain. CET module must come from the loaded named module.
template<const HookContract& spec> inline bool ReadHookChain(ReadMemory read,void* user,uint64_t base,uint64_t cet,
                                                          ResizeChain& output,uint32_t* failure=nullptr){
    output={};if(failure)*failure=0;ResizeChain value;
    const auto fail=[&](uint32_t code){if(failure)*failure=code;output=value;return false;};
    std::array<uint8_t,spec.nativeBytes> native{};
    if(!base || base>UINT64_MAX-spec.nativeRva-spec.nativeBytes ||
        !queue_residency::detail::Read(read,user,base+spec.nativeRva,native.data(),native.size()))return fail(1);
    if(native_recording::detail::Fingerprint(native)==spec.nativeHash){
        std::array<uint8_t,spec.nativeBytes> again{};
        if(!queue_residency::detail::Read(read,user,base+spec.nativeRva,again.data(),again.size()) || again!=native)return fail(11);
        value.mode=1;output=value;return true;
    }
    if(native[0]!=0xE9 || native_recording::detail::Fingerprint(std::span(native).subspan(5))!=spec.suffixHash)return fail(2);
    int32_t displacement{};std::memcpy(&displacement,native.data()+1,4);
    if(!RelativeTarget(base+spec.nativeRva+5,displacement,value.relay))return fail(3);
    std::array<uint8_t,14> relay{};constexpr std::array<uint8_t,6> jump{0xFF,0x25,0,0,0,0};
    uint64_t destination{};
    if(!queue_residency::detail::Read(read,user,value.relay,relay.data(),relay.size()) ||
        !std::equal(jump.begin(),jump.end(),relay.begin()))return fail(4);
    std::memcpy(&destination,relay.data()+6,8);value.cet=cet;
    if(!cet || cet>UINT64_MAX-0x730000 || destination!=cet+spec.wrapperRva)return fail(5);
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!queue_residency::detail::Read(read,user,cet,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew<=0 || dos.e_lfanew>0x1000 || !Field(read,user,cet,uint32_t(dos.e_lfanew),nt) ||
        nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.TimeDateStamp!=0x68D9142A || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage!=0x730000)return fail(5);
    std::array<uint8_t,spec.wrapperBytes> wrapper{};std::array<uint8_t,CetResetBytes> reset{};
    if(!queue_residency::detail::Read(read,user,cet+spec.wrapperRva,wrapper.data(),wrapper.size()) ||
        native_recording::detail::Fingerprint(wrapper)!=spec.wrapperHash)return fail(6);
    if(!queue_residency::detail::Read(read,user,cet+CetResetRva,reset.data(),reset.size()) ||
        native_recording::detail::Fingerprint(reset)!=CetResetHash)return fail(7);
    if(!Field(read,user,cet,0x6DD8F0,value.singleton) || !value.singleton)return fail(8);
    if(!Field(read,user,value.singleton,spec.originalOffset,value.trampoline) || !value.trampoline)return fail(9);
    std::array<uint8_t,spec.stolenBytes+14> trampoline{};uint64_t rejoin{};
    if(!queue_residency::detail::Read(read,user,value.trampoline,trampoline.data(),trampoline.size()) ||
        !std::equal(spec.stolen.begin(),spec.stolen.begin()+spec.stolenBytes,trampoline.begin()) ||
        !std::equal(jump.begin(),jump.end(),trampoline.begin()+spec.stolenBytes))return fail(10);
    std::memcpy(&rejoin,trampoline.data()+spec.stolenBytes+6,8);if(rejoin!=base+spec.nativeRva+spec.stolenBytes)return fail(10);
    std::array<uint8_t,spec.nativeBytes> nativeAgain{};std::array<uint8_t,14> relayAgain{};
    std::array<uint8_t,spec.stolenBytes+14> trampolineAgain{};uint64_t singletonAgain{},originalAgain{};
    std::array<uint8_t,spec.wrapperBytes> wrapperAgain{};std::array<uint8_t,CetResetBytes> resetAgain{};
    IMAGE_DOS_HEADER dosAgain{};IMAGE_NT_HEADERS64 ntAgain{};
    if(!queue_residency::detail::Read(read,user,base+spec.nativeRva,nativeAgain.data(),nativeAgain.size()) || nativeAgain!=native ||
        !queue_residency::detail::Read(read,user,value.relay,relayAgain.data(),relayAgain.size()) || relayAgain!=relay ||
        !queue_residency::detail::Read(read,user,cet,&dosAgain,sizeof(dosAgain)) || std::memcmp(&dosAgain,&dos,sizeof(dos)) ||
        !Field(read,user,cet,uint32_t(dos.e_lfanew),ntAgain) || std::memcmp(&ntAgain,&nt,sizeof(nt)) ||
        !queue_residency::detail::Read(read,user,cet+spec.wrapperRva,wrapperAgain.data(),wrapperAgain.size()) || wrapperAgain!=wrapper ||
        !queue_residency::detail::Read(read,user,cet+CetResetRva,resetAgain.data(),resetAgain.size()) || resetAgain!=reset ||
        !Field(read,user,cet,0x6DD8F0,singletonAgain) || singletonAgain!=value.singleton ||
        !Field(read,user,value.singleton,spec.originalOffset,originalAgain) || originalAgain!=value.trampoline ||
        !queue_residency::detail::Read(read,user,value.trampoline,trampolineAgain.data(),trampolineAgain.size()) || trampolineAgain!=trampoline)
        return fail(11);
    value.mode=2;output=value;return true;
}
inline bool ReadResizeChain(ReadMemory read,void* user,uint64_t base,uint64_t cet,ResizeChain& output,uint32_t* failure=nullptr){
    return ReadHookChain<ResizeHook>(read,user,base,cet,output,failure);
}
inline bool ReadShutdownChain(ReadMemory read,void* user,uint64_t base,uint64_t cet,ResizeChain& output,uint32_t* failure=nullptr){
    return ReadHookChain<ShutdownHook>(read,user,base,cet,output,failure);
}
// The sole table handle writer (873DE4) uses a fresh texture-pool allocation.
// The held native source context prevents our handle retiring and being reused
// as such a fresh allocation during the borrowed epoch.
inline bool ReadSwapchains(ReadMemory read,void* user,const Key& key,Swapchains& output){
    output={};
    if(!key.registry || key.registry>UINT64_MAX-SwapchainTableOffset-0x1650)return false;
    Swapchains value;value.table=key.registry+SwapchainTableOffset;
    uint8_t locked{};
    if(!Field(read,user,value.table,0,value.lock) || value.lock!=key.registry+RegistryLockOffset ||
        !Field(read,user,value.lock,0,locked) || locked ||
        !Field(read,user,value.table,8,value.freeCount) || value.freeCount>SwapchainCount)return false;
    uint32_t free{};
    for(uint32_t i=0;i<SwapchainCount;++i){
        if(!Field(read,user,value.table,0x10+uint64_t(i)*0xB0,value.references[i]) ||
            value.references[i]<-1 || value.references[i]==INT32_MAX ||
            !Field(read,user,value.table,0x40+uint64_t(i)*0xB0,value.handles[i]) ||
            value.handles[i]>32768 || value.handles[i]==key.handle)return false;
        free+=value.references[i]==-1;
    }
    uint64_t lockAgain{};uint32_t countAgain{};
    if(free!=value.freeCount || !Field(read,user,value.table,0,lockAgain) || lockAgain!=value.lock ||
        !Field(read,user,value.table,8,countAgain) || countAgain!=value.freeCount ||
        !Field(read,user,value.lock,0,locked) || locked)return false;
    output=value;return true;
}
inline bool ReadEpoch(ReadMemory read,void* user,uint64_t base,uint64_t registry,Epoch& output){
    output={};if(!base || base>UINT64_MAX-RetirementRva-0x20CD1)return false;
    uint64_t actual{};uint8_t stopped{},lock{},writers{};Epoch next;next.address=base+RetirementRva;
    if(!Field(read,user,base,RegistryRva,actual) || actual!=registry ||
        !Field(read,user,registry,1,stopped) || stopped ||
        !Field(read,user,registry,0x13BC734,lock) || lock ||
        !Field(read,user,registry,0x13BC730,next.outstanding) || !next.outstanding || next.outstanding>=INT32_MAX ||
        !Field(read,user,next.address,0x20CD0,writers) || writers ||
        !Field(read,user,next.address,0x20CC0,next.index) || next.index>=128)return false;
    next.bucket=next.address+0x200C0+24ull*next.index;
    if(!Field(read,user,next.bucket,0,next.fence) || next.fence ||
        !Field(read,user,next.bucket,8,next.value) || next.value ||
        !Field(read,user,next.bucket,16,next.count) || next.count>=RingSize ||
        !Field(read,user,next.address,0,next.reserved) ||
        !Field(read,user,next.address,0x20040,next.head) ||
        !Field(read,user,next.address,0x20080,next.committed) ||
        next.reserved!=next.committed || next.head>next.committed || next.committed-next.head>=RingSize ||
        next.count>next.committed-next.head)return false;
    if(!Field(read,user,registry,0x13BC734,lock) || lock ||
        !Field(read,user,next.address,0x20CD0,writers) || writers)return false;
    output=next;return true;
}
// A zero reference is accepted only when this exact handle is in the current,
// unfenced retirement bucket, not just somewhere in the deferred-free queue.
inline bool CurrentMember(ReadMemory read,void* user,const Epoch& epoch,uint32_t handle,uint64_t& slot){
    slot=UINT64_MAX;
    for(uint32_t i=0;i<epoch.count;++i){
        const auto sequence=epoch.committed-epoch.count+i;uint32_t actual{};
        if(!Field(read,user,epoch.address,8+4*(sequence%RingSize),actual))return false;
        if(actual==handle){if(slot!=UINT64_MAX)return false;slot=sequence;}
    }
    return slot!=UINT64_MAX;
}
// Detail failures: 1 key, 2 epoch, 3 context, 4 native record, 5 refs,
// 6 control, 7 allocation, 8 allocator, 9 queue membership, 10 changed epoch,
// 11 native swapchain provenance (table/lock/slot inconsistency or membership).
inline bool ReadSnapshot(ReadMemory read,void* user,uint64_t base,const Key& key,bool source,
                         Snapshot& output,uint32_t* failure=nullptr){
    output={};if(failure)*failure=0;
    Snapshot value;value.base=base;value.key=key;
    const auto fail=[&](uint32_t code){if(failure)*failure=code;output=value;return false;};
    if(!key.registry || !key.context || !key.list || !key.resource || !key.heap || !key.bytes ||
        key.bytes==UINT64_MAX || !key.alignment || !key.handle || key.handle>32768)return fail(1);
    if(!ReadEpoch(read,user,base,key.registry,value.epoch))return fail(2);
    if(!ReadSwapchains(read,user,key,value.swapchains))return fail(11);
    uint64_t list{};uint32_t kind{};
    if(!Field(read,user,key.context,0x30,list) || list!=key.list ||
        !Field(read,user,key.context,0x68,kind) || (kind!=1 && kind!=2))return fail(3);
    const auto offset=0x2F1D8+uint64_t(key.handle-1)*0xB0;
    if(key.registry>UINT64_MAX-offset-0xB0)return fail(4);
    value.record=key.registry+offset;uint64_t resource{};uint8_t tag{};
    if(!Field(read,user,value.record,0,resource) || resource!=key.resource ||
        !Field(read,user,value.record,8,tag) || tag!=2 ||
        !Field(read,user,value.record,0x10,value.owner) || !value.owner)return fail(4);
    if(!queue_residency::detail::Read(read,user,value.record-8,&value.handleReferences,4) ||
        value.handleReferences<0 || value.handleReferences==INT32_MAX || (source && !value.handleReferences))return fail(5);
    if(!queue_residency::detail::Read(read,user,value.owner,value.control.data(),value.control.size()))return fail(6);
    value.ownerReferences=source_cbv_details::detail::Field<int32_t>(value.control,0);
    const auto allocation=source_cbv_details::detail::Field<uint64_t>(value.control,8);
    if(value.ownerReferences<=0 || value.ownerReferences==INT32_MAX || !allocation ||
        source_cbv_details::detail::Field<uint64_t>(value.control,0x18)!=key.alignment)return fail(6);
    value.allocation=source_cbv_details::detail::ReadAllocation(read,user,base,allocation);
    if(value.allocation.observation!=source_cbv_details::Observation::Known || value.allocation.kind!=2 || value.allocation.resource ||
        value.allocation.heap!=key.heap || value.allocation.bytes<key.bytes ||
        source_cbv_details::detail::Field<uint64_t>(value.control,0x20)!=value.allocation.bytes)return fail(7);
    value.allocator=source_cbv_details::detail::ReadAllocator(read,user,base,key.registry,value.allocation);
    if(value.allocator.observation!=source_cbv_details::Observation::Known || !value.allocator.associationKnown || !value.allocator.pimplMatches)return fail(8);
    if(!value.handleReferences && !CurrentMember(read,user,value.epoch,key.handle,value.queueSlot))return fail(9);
    Epoch after;if(!ReadEpoch(read,user,base,key.registry,after) || after!=value.epoch)return fail(10);
    Swapchains swapchainsAfter;
    if(!ReadSwapchains(read,user,key,swapchainsAfter) || swapchainsAfter!=value.swapchains)return fail(11);
    output=std::move(value);return true;
}
template<size_t N> bool SameExceptRef(const std::array<uint8_t,N>& a,const std::array<uint8_t,N>& b,size_t ref){
    for(size_t i=0;i<N;++i)if((i<ref || i>=ref+4) && a[i]!=b[i])return false;return true;
}
inline bool SameIdentity(const Snapshot& a,const Snapshot& b){
    return a.base==b.base && a.key==b.key && a.record==b.record && a.owner==b.owner &&
        a.swapchains==b.swapchains &&
        a.epoch.address==b.epoch.address && a.epoch.bucket==b.epoch.bucket && a.epoch.index==b.epoch.index &&
        !a.epoch.fence && !a.epoch.value && !b.epoch.fence && !b.epoch.value &&
        SameExceptRef(a.control,b.control,0) && a.allocation.address==b.allocation.address &&
        SameExceptRef(a.allocation.raw,b.allocation.raw,8) && a.allocator.address==b.allocator.address &&
        SameExceptRef(a.allocator.raw,b.allocator.raw,8);
}
struct CaptureProof;
struct BorrowProof;
// Both operands must separately pass their private WritableTargetIdentity
// authentication. Cross-capture matching excludes unrelated mutable record bytes.
inline bool SameBorrowedIdentity(const texture_read::WritableTargetIdentity& a,const texture_read::WritableTargetIdentity& b){
    return a.creation.resource && a.creation.heap && a.recording.token &&
        texture_read::detail::SameCreationMetadata(a.creation,b.creation) &&
        a.native.registry==b.native.registry && a.native.resource==b.native.resource &&
        a.native.handle==b.native.handle && a.native.reserved==b.native.reserved &&
        a.recording.token==b.recording.token && a.recording.generation==b.recording.generation &&
        a.recording.context==b.recording.context && a.recording.list==b.recording.list && a.recording.registry==b.recording.registry &&
        a.originalContext==b.originalContext && a.expectedWritableState==b.expectedWritableState &&
        texture_read::detail::SameResourceDescription(a.resourceDesc,b.resourceDesc) &&
        texture_read::detail::SameHeapDescription(a.heapDesc,b.heapDesc) &&
        texture_read::detail::SameHeapProperties(a.resourceHeapProperties,b.resourceHeapProperties) &&
        a.resourceHeapFlags==b.resourceHeapFlags && a.allocatorOwnerObservation==b.allocatorOwnerObservation &&
        texture_read::detail::WritableRecord(a.nativeRecord)==texture_read::detail::WritableRecord(b.nativeRecord);
}
}
struct BorrowedPageable;
class Lease {
    std::shared_ptr<const detail::CaptureProof> proof_;
    friend bool Capture(const texture_read::WritableTargetIdentity&,Lease&);
    friend bool Bind(const Lease&,ID3D12CommandQueue*,const queue_residency::Scope&,class BorrowedScope&);
public:explicit operator bool()const{return bool(proof_);}
};
class BorrowedScope {
    std::shared_ptr<const detail::BorrowProof> proof_;
    friend bool Bind(const Lease&,ID3D12CommandQueue*,const queue_residency::Scope&,BorrowedScope&);
    friend bool Validate(const BorrowedScope&);
    friend bool GetBorrowedPageable(ID3D12Device*,const texture_read::WritableTargetIdentity&,const BorrowedScope&,BorrowedPageable&);
public:explicit operator bool()const{return bool(proof_);}
};
struct BorrowedPageable {
    texture_read::WritableTargetIdentity identity;
    BorrowedScope lifetime;
    Microsoft::WRL::ComPtr<ID3D12Pageable> pageable;
};
// Capture only at the original draw; Bind while its submission token is pending.
// This borrows a native lifetime through the SAME synchronous ExecuteSubset
// callback. It expires on return. No allocation pin or cross-frame permission;
// access ordering, alias exclusion, residency and export admission are separate.
bool Compatible();
bool Capture(const texture_read::WritableTargetIdentity&,Lease&);
bool Bind(const Lease&,ID3D12CommandQueue*,const queue_residency::Scope&,BorrowedScope&);
bool Validate(const BorrowedScope&);
// Same synchronous borrowed scope only. Returns the exact authenticated placed
// heap, with resource/heap COM retention in identity.creation. No native owner
// pin is added and no ImmutableLease or cross-frame lifetime is manufactured.
bool GetBorrowedPageable(ID3D12Device*,const texture_read::WritableTargetIdentity& expected,
                         const BorrowedScope&,BorrowedPageable&);
// 1 arguments, 2 contract, 3 identity, 4 recording, 5 snapshot, 6 changed,
// 7 submission, 8 synchronous scope, 9 exception, 10 target pairing,
// 11 device/physical shape. Details: stage, reader code,
// record,owner,allocation,heap,handleRefs,ownerRefs,bucket,index,count,queueSlot.
// At stage 2, details[1..3] instead contain the cached contract RVA, expected
// hash and actual hash. RVA 0/1 identify native-recording/texture prerequisites;
// expected is 1 and actual is that prerequisite's LastFailure (possibly zero).
// Dynamic hook rejection uses RVA 87466C (resize) or 10E02B4 (shutdown),
// expected 0, actual ReadHookChain rejection. Details[4..7] contain
// relay,CET,singleton,trampoline. Both chains must pass on every validation.
uint32_t LastFailure();
std::array<uint64_t,12> LastDetails();
std::array<uint64_t,3> LastContractFailure();
}
