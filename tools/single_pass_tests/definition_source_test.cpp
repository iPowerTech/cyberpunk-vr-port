#include "Render/NativeGraphDefinitionSource.hpp"
#include <cstdio>
#include <stdexcept>

namespace graph=cvr::stereo::graph_snapshot;
namespace definition=cvr::stereo::graph_definition;
using cvr::stereo::definition_owner::NativeVector;
namespace {
constexpr uint64_t Base=0x140000000,MemoryBase=0x10000,Cache=0x10000,Slot=Cache+2*0x90,Composite=Slot+0x10;
constexpr uint64_t OwnerPointers=0x20000,NodeOrder=0x24000,NodeData=0x25000,EdgeOrder=0x26000,EdgeData=0x27000;
constexpr uint64_t Owner0=0x30000,Owner1=0x31000,Definitions0=0x40000,Definitions1=0x60000;
constexpr uint64_t Object0=0x80000,Object1=0x81000,Vtable0=0x82000,Vtable1=0x83000,Allocator=0x90000;
std::array<uint8_t,0x90000> memory;
uint64_t reads{},failAt{},triggerAddress{},triggerCount{},triggerSeen{},mutationAddress{},mutationValue{};
uint64_t failAddress{},failAddressCount{},failAddressSeen{};
size_t mutationBytes{};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>void Put(uint64_t address,T value){
    Require(address>=MemoryBase && sizeof(T)<=memory.size() && address-MemoryBase<=memory.size()-sizeof(T),"Fixture write out of bounds");
    std::memcpy(memory.data()+(address-MemoryBase),&value,sizeof(T));
}
bool Read(uint64_t address,void* output,size_t bytes){
    if(++reads==failAt)return false;
    if(address==failAddress && ++failAddressSeen==failAddressCount)return false;
    if(address==triggerAddress && ++triggerSeen==triggerCount){
        Require(mutationBytes<=8 && mutationAddress>=MemoryBase && mutationAddress-MemoryBase<=memory.size()-mutationBytes,"Invalid mutation");
        std::memcpy(memory.data()+(mutationAddress-MemoryBase),&mutationValue,mutationBytes);
    }
    if(address<MemoryBase || bytes>memory.size() || address-MemoryBase>memory.size()-bytes)return false;
    std::memcpy(output,memory.data()+(address-MemoryBase),bytes);return true;
}
void Controls(){reads=failAt=triggerAddress=triggerCount=triggerSeen=mutationAddress=mutationValue=0;
    failAddress=failAddressCount=failAddressSeen=0;mutationBytes=0;}
graph::Snapshot Reset(){
    memory={};Controls();Put<uint32_t>(Slot,42);Put<uint64_t>(Slot+8,0x12345678abcdef00ull);
    Put(Composite+0x70,NativeVector{OwnerPointers,2,2});Put(OwnerPointers,Owner0);Put(OwnerPointers+8,Owner1);Put(OwnerPointers+16,Allocator);
    Put(Owner0+0x70,NativeVector{Definitions0,4,3});Put(Definitions0,Object0);Put<uint64_t>(Definitions0+8,0);
    Put<uint64_t>(Definitions0+16,0);Put(Definitions0+32,Allocator);
    Put(Owner1+0x70,NativeVector{Definitions1,1,1});Put(Definitions1,Object1);Put(Definitions1+8,Allocator);
    Put(Object0,Vtable0);Put(Object1,Vtable1);Put(Vtable0+8,Base+0x1ec1d0);Put(Vtable1+8,Base+0x58da9c);
    Put<uint64_t>(Object0+24,0x987654321);Put<uint32_t>(Object1+24,2);
    Put<uint32_t>(Composite+0x10,4);Put(Composite+0x18,NodeData);Put(Composite+0x28,NodeOrder);
    Put<uint32_t>(Composite+0x40,2);Put(Composite+0x48,EdgeData);Put(Composite+0x58,EdgeOrder);
    constexpr std::array<uint32_t,4> ids{5,17,39,48};
    for(uint32_t i=0;i<ids.size();++i){const auto node=NodeData+uint64_t(ids[i])*48;
        Put(NodeOrder+i*4,ids[i]|0x20000000u);Put(node,i);Put<uint32_t>(node+8,4);
        Put(node+16,i<2?Object0:i==2?Object1:uint64_t(0));Put<uint32_t>(node+24,i==0?0:1);
        Put<uint32_t>(node+28,i|(i<<8));
    }
    for(uint32_t i=0;i<2;++i){const auto edge=EdgeData+i*24;
        Put(EdgeOrder+i*4,i|0x40000000u);Put(edge,i);Put(edge+4,i);
        Put(edge+8,ids[i]|0x20000000u);Put(edge+12,ids[i+2]|0x20000000u);
    }
    graph::Snapshot source;Require(source.Capture(Composite,Read),"Fixture graph capture failed");Controls();return source;
}
definition::SourceReceipt Capture(const graph::Snapshot& source){
    definition::SourceReceipt receipt;
    Require(receipt.Capture(Cache,Composite,source,Base,Read),"Valid definition source rejected");return receipt;
}
void Positive(){
    const auto source=Reset();const auto before=memory;auto receipt=Capture(source);
    Require(memory==before && receipt.Ready() && receipt.Rejection()==definition::Failure::None,"Capture changed native memory");
    Require(!receipt.DefinitionOwnershipProved() && !receipt.NativeExclusiveProved(),"Read receipt claimed native ownership");
    Require(receipt.CacheBase()==Cache && receipt.CacheSlot()==Slot && receipt.CompositeGraph()==Composite &&
        receipt.LastUse()==42 && receipt.CacheKey()==0x12345678abcdef00ull && receipt.ImageBase()==Base,"Cache identity differs");
    Require(receipt.OwnerVector()==NativeVector{OwnerPointers,2,2} && receipt.OwnerVectorAllocator()==Allocator &&
        receipt.Owners().size()==2 && receipt.DefinitionSlots()==4,"Owner allocation metadata differs");
    Require(receipt.Owners()[0].move.Definitions()==std::vector<uint64_t>{Object0,0,0} &&
        receipt.Owners()[0].move.OwnerAddress()==Owner0+0x70 && receipt.Owners()[1].move.OwnerAddress()==Owner1+0x70,
        "Null unique-owner slots or owner headers lost");
    Require(receipt.Mappings()==std::vector<definition::Mapping>{{5|0x20000000u,0,0,Object0},
        {17|0x20000000u,0,0,Object0},{39|0x20000000u,1,0,Object1}},"Cross-eye borrowed definitions did not map to unique owners");
    Require(receipt.Source() && receipt.Source()->nodes==source.nodes && receipt.Source()->edges==source.edges &&
        receipt.SourceFingerprint()==cvr::stereo::paired_frontier::detail::Fingerprint(source,Base),"Deep source identity differs");
    Require(receipt.MatchesBefore(Read),"Stable receipt no longer matches");
    auto held=receipt.Source();const auto fingerprint=receipt.SourceFingerprint();receipt={};
    Require(held->nodes==source.nodes && fingerprint==cvr::stereo::paired_frontier::detail::Fingerprint(*held,Base),"Source receipt lifetime escaped");
    receipt=Capture(source);const auto* alias=receipt.Source().get();
    Require(receipt.Capture(Cache,Composite,*alias,Base,Read),"Recapture of own immutable source invalidated input");
}
template<class T>void Reject(uint64_t address,T value){
    const auto source=Reset();Put(address,value);definition::SourceReceipt receipt;
    Require(!receipt.Capture(Cache,Composite,source,Base,Read) && !receipt.Ready(),"Invalid owner source accepted");
}
void Negative(){
    const auto source=Reset();definition::SourceReceipt receipt;
    Require(!receipt.Capture(0,Composite,source,Base,Read) && !receipt.Capture(Cache,Composite,source,0,Read) &&
        !receipt.Capture(Cache,Composite,source,Base,nullptr),"Missing capture arguments accepted");
    Require(!receipt.Capture(Cache,Composite+8,source,Base,Read) && !receipt.Capture(Cache,Cache+4*0x90+0x10,source,Base,Read),"Graph not embedded in four cache slots accepted");
    Require(!receipt.Capture(UINT64_MAX-0x10,Composite,source,Base,Read),"Cache address overflow accepted");
    auto invalid=source;invalid.failure=1;Require(!receipt.Capture(Cache,Composite,invalid,Base,Read),"Failed snapshot accepted");
    invalid=source;invalid.nodes[0].definition[24]^=1;Require(!receipt.Capture(Cache,Composite,invalid,Base,Read),"Different source definition accepted");
    invalid=source;invalid.edges[0].bytes[4]^=1;Require(!receipt.Capture(Cache,Composite,invalid,Base,Read),"Different source edges accepted");
    Reject(Composite+0x70,NativeVector{OwnerPointers,0,0});Reject(Composite+0x70,NativeVector{OwnerPointers,129,2});
    Reject(Composite+0x70,NativeVector{OwnerPointers,1,2});Reject(Composite+0x70,NativeVector{UINT64_MAX-7,2,2});
    Reject(Composite+0x70,NativeVector{Cache,2,2});Reject<uint64_t>(OwnerPointers+16,0);Reject<uint64_t>(OwnerPointers+16,OwnerPointers);
    Reject<uint64_t>(OwnerPointers,0);Reject<uint64_t>(OwnerPointers,Owner0+1);Reject<uint64_t>(OwnerPointers+8,Owner0);
    Reject<uint64_t>(OwnerPointers,Composite);Reject<uint64_t>(OwnerPointers+8,Owner0+8);
    Reject(Owner1+0x70,NativeVector{Definitions0,4,3});Reject<uint64_t>(Definitions1,Object0);
    Reject<uint64_t>(Definitions1,0);Reject<uint64_t>(Definitions0,Object1);
    Reject(Owner0+0x70,NativeVector{Definitions0,4,5});Reject<uint64_t>(Definitions0+32,0);
    Reject<uint64_t>(Definitions0+8,Owner1);Reject<uint64_t>(Definitions0+8,Definitions1);
    // Total active slots, including null synthetic entries, share one global budget.
    auto budgetSource=Reset();Put(Owner0+0x70,NativeVector{Definitions0,8192,8192});Put(Definitions0+8192*8,Allocator);
    Put(Owner1+0x70,NativeVector{Definitions1,1,1});
    Require(!receipt.Capture(Cache,Composite,budgetSource,Base,Read) && receipt.Rejection()==definition::Failure::DefinitionBudget,
        "Global definition budget exceeded");
}
void ChangesAndReadFailures(){
    auto source=Reset();auto receipt=Capture(source);const auto fullReads=reads;
    for(uint64_t at=1;at<=fullReads;++at){source=Reset();failAt=at;definition::SourceReceipt failed;
        Require(!failed.Capture(Cache,Composite,source,Base,Read),"Partial memory read accepted");}
    struct Change{uint64_t address,value;size_t bytes;};
    constexpr std::array<Change,6> changes{{{Slot,43,4},{Slot+8,0x11,8},{OwnerPointers+16,Allocator+8,8},
        {Definitions0+32,Allocator+8,8},{Definitions0+8,Object1,8},{Object0+24,0x4567,8}}};
    for(const auto& change:changes){
        source=Reset();receipt=Capture(source);
        std::memcpy(memory.data()+(change.address-MemoryBase),&change.value,change.bytes);
        Require(!receipt.MatchesBefore(Read),"Changed generation fields were ignored");
        source=Reset();triggerAddress=Composite+0x70;triggerCount=2;mutationAddress=change.address;
        mutationValue=change.value;mutationBytes=change.bytes;definition::SourceReceipt torn;
        Require(!torn.Capture(Cache,Composite,source,Base,Read),"Generation changed during transaction was accepted");
    }
    source=Reset();Put(Owner1+0x70,NativeVector{Allocator,0,0});Put<uint64_t>(NodeData+39*48+16,0);
    graph::Snapshot withoutUnused;Require(withoutUnused.Capture(Composite,Read),"Synthetic replacement graph invalid");
    const auto emptyOwner=Capture(withoutUnused);
    Require(emptyOwner.Owners().size()==2 && !emptyOwner.Owners()[1].move.OwnsAllocation() && emptyOwner.Mappings().size()==2,
        "Unused per-view owner sentinel rejected");
}
void ConcurrentCacheHits(){
    using Policy=definition::CacheUsePolicy;
    auto source=Reset();triggerAddress=Composite+0x70;triggerCount=2;mutationAddress=Slot;mutationValue=43;mutationBytes=4;
    definition::SourceReceipt receipt;
    Require(receipt.Capture(Cache,Composite,source,Base,Read,Policy::AllowConcurrentHits),"Cache hit counter invalidated identical ownership");
    Controls();Put<uint32_t>(Slot,44);
    Require(receipt.MatchesBefore(Read),"Ownership receipt rejected a later cache hit");
    Require(!receipt.DefinitionOwnershipProved() && !receipt.NativeExclusiveProved(),"Concurrent hit policy claimed native ownership");
    struct Change{uint64_t address,value;size_t bytes;};
    constexpr std::array<Change,5> changes{{{Slot+8,0x11,8},{OwnerPointers+16,Allocator+8,8},
        {Definitions0+32,Allocator+8,8},{Definitions0+8,Object1,8},{Object0+24,0x4567,8}}};
    for(const auto& change:changes){
        source=Reset();Require(receipt.Capture(Cache,Composite,source,Base,Read,Policy::AllowConcurrentHits),"Stable concurrent-hit receipt failed");
        std::memcpy(memory.data()+(change.address-MemoryBase),&change.value,change.bytes);
        Require(!receipt.MatchesBefore(Read),"Concurrent-hit policy ignored a changed owner/source");
        source=Reset();triggerAddress=Composite+0x70;triggerCount=2;mutationAddress=change.address;mutationValue=change.value;mutationBytes=change.bytes;
        Require(!receipt.Capture(Cache,Composite,source,Base,Read,Policy::AllowConcurrentHits),"Torn owner/source accepted by concurrent-hit policy");
    }
    source=Reset();Require(!receipt.Capture(Cache,Composite,source,Base,Read,static_cast<Policy>(99)),"Unknown cache-use policy accepted");
}
void ChangedDiagnostics(){
    using Detail=definition::ChangedDetail;
    using Policy=definition::CacheUsePolicy;
    const auto bit=[](Detail detail){return uint32_t(detail);};
    auto source=Reset();definition::SourceReceipt receipt;
    Require(receipt.Capture(Cache,Composite,source,Base,Read) && !receipt.ChangedDetails(),"Stable capture reported changed fields");
    uint32_t matched=UINT32_MAX;
    Require(receipt.MatchesBefore(Read,&matched) && !matched,"Stable match retained old diagnostics");
    const auto rejectExpected=[&](graph::Snapshot expected,uint32_t details){
        Require(!receipt.Capture(Cache,Composite,expected,Base,Read) && receipt.Rejection()==definition::Failure::Source &&
            receipt.ChangedDetails()==details,"Source snapshot difference was misclassified");
    };
    auto different=source;different.nodes[0].bytes[24]^=1;rejectExpected(different,bit(Detail::NodeRaw));
    different=source;different.nodes[0].handle^=1;rejectExpected(different,bit(Detail::NodeRaw));
    different=source;different.nodes[0].order^=1;rejectExpected(different,bit(Detail::NodeRaw));
    different=source;different.edges[0].bytes[4]^=1;rejectExpected(different,bit(Detail::EdgeRaw));
    different=source;different.nodes[0].work^=1;rejectExpected(different,bit(Detail::VtableWork));
    different=source;different.nodes[0].vtable^=8;rejectExpected(different,bit(Detail::VtableWork));
    different=source;different.nodes[0].definition[24]^=1;rejectExpected(different,bit(Detail::DefinitionPrefix));
    different=source;different.nodes[0].bytes[24]^=1;different.nodes[0].definition[24]^=1;
    different.edges[0].bytes[4]^=1;
    rejectExpected(different,bit(Detail::NodeRaw)|bit(Detail::DefinitionPrefix)|bit(Detail::EdgeRaw));
    struct Change{uint64_t address,value;size_t bytes;uint32_t detail;};
    const std::array<Change,10> changes{{
        {Slot,43,4,bit(Detail::CacheUse)},
        {Slot+8,0x11,8,bit(Detail::CacheKey)},
        {Composite+0x7c,1,4,bit(Detail::OwnerVector)},
        {OwnerPointers,Owner0+8,8,bit(Detail::OwnerPointers)},
        {OwnerPointers+16,Allocator+8,8,bit(Detail::Allocator)},
        {Owner0+0x7c,2,4,bit(Detail::OwnerHeader)|bit(Detail::Entries)},
        {Definitions0+32,Allocator+8,8,bit(Detail::Allocator)},
        {Definitions0+8,Object1,8,bit(Detail::Entries)},
        {Vtable0+8,Base+0x1ec1d8,8,bit(Detail::VtableWork)},
        {Object0+24,0x4567,8,bit(Detail::DefinitionPrefix)}
    }};
    for(const auto& change:changes){
        source=Reset();triggerAddress=Slot;triggerCount=2;
        mutationAddress=change.address;mutationValue=change.value;mutationBytes=change.bytes;
        Require(!receipt.Capture(Cache,Composite,source,Base,Read) && receipt.Rejection()==definition::Failure::Changed &&
            receipt.ChangedDetails()==change.detail,"Post-read changed field was misclassified");
    }
    for(const auto& change:std::array<Change,2>{{
        {NodeData+5*48+24,2,4,bit(Detail::NodeRaw)},
        {EdgeData+4,7,4,bit(Detail::EdgeRaw)}}}){
        source=Reset();triggerAddress=Composite+0x70;triggerCount=2;
        mutationAddress=change.address;mutationValue=change.value;mutationBytes=change.bytes;
        Require(!receipt.Capture(Cache,Composite,source,Base,Read) && receipt.ChangedDetails()==change.detail,
            "Raw topology drift was confused with external definition data");
    }
    // Change only between complete ReadOnce transactions, not inside either.
    source=Reset();triggerAddress=Composite;triggerCount=5;mutationAddress=Slot+8;mutationValue=0x11;mutationBytes=8;
    Require(!receipt.Capture(Cache,Composite,source,Base,Read) && receipt.Rejection()==definition::Failure::Changed &&
        receipt.ChangedDetails()==(bit(Detail::DoubleSnapshot)|bit(Detail::CacheKey)),"Double-snapshot drift lost its field or phase");
    // The nested MovePlan parser exposes only Changed, not which private field.
    source=Reset();triggerAddress=Owner0+0x70;triggerCount=2;mutationAddress=Definitions0+8;mutationValue=Object1;mutationBytes=8;
    Require(!receipt.Capture(Cache,Composite,source,Base,Read) && receipt.Rejection()==definition::Failure::OwnerPlan &&
        receipt.ChangedDetails()==(bit(Detail::OwnerCapture)|bit(Detail::DoubleSnapshot)),"Nested owner capture failure was guessed as a field change");
    source=Reset();failAt=1;
    Require(!receipt.Capture(Cache,Composite,source,Base,Read) && receipt.Rejection()==definition::Failure::Source &&
        receipt.ChangedDetails()==bit(Detail::GraphCapture),"Graph parser failure was mislabeled as prefix drift");
    source=Reset();failAddress=Slot;failAddressCount=2;
    Require(!receipt.Capture(Cache,Composite,source,Base,Read) && receipt.Rejection()==definition::Failure::Changed &&
        receipt.ChangedDetails()==(bit(Detail::Read)|bit(Detail::CacheUse)),"Failed post-read was reported as observed counter drift");
    source=Reset();Require(receipt.Capture(Cache,Composite,source,Base,Read),"Match diagnostic setup failed");
    Put<uint64_t>(Object0+24,0x4567);matched=0;
    Require(!receipt.MatchesBefore(Read,&matched) && matched==bit(Detail::DefinitionPrefix) && !receipt.ChangedDetails(),
        "Const match failed to report prefix drift independently of capture diagnostics");
    source=Reset();triggerAddress=Composite+0x70;triggerCount=2;mutationAddress=Slot;mutationValue=43;mutationBytes=4;
    Require(receipt.Capture(Cache,Composite,source,Base,Read,Policy::AllowConcurrentHits) && !receipt.ChangedDetails(),
        "Ignored cache hit produced a changed diagnostic");
    Controls();Put<uint32_t>(Slot,44);matched=UINT32_MAX;
    Require(receipt.MatchesBefore(Read,&matched) && !matched,"Ignored subsequent cache hit reported a changed field");
    source=Reset();Require(receipt.Capture(Cache,Composite,source,Base,Read) && !receipt.ChangedDetails(),
        "Successful recapture retained failed-capture diagnostics");
}
void OwnerMembershipPolicy(){
    using CachePolicy=definition::CacheUsePolicy;
    using ContentPolicy=definition::DefinitionContentPolicy;
    const auto prefix=uint32_t(definition::ChangedDetail::DefinitionPrefix);
    const auto raw=uint32_t(definition::ChangedDetail::NodeRaw);
    auto source=Reset();Put<uint64_t>(Object0+24,0x4567);definition::SourceReceipt receipt;
    Require(!receipt.Capture(Cache,Composite,source,Base,Read) && receipt.ChangedDetails()==prefix,
        "Default policy stopped requiring immutable definition prefix");
    Require(!receipt.Capture(Cache,Composite,source,Base,Read,CachePolicy::AllowConcurrentHits,ContentPolicy::ImmutablePrefix) &&
        receipt.ChangedDetails()==prefix,"Concurrent cache hits relaxed explicit immutable prefix policy");
    Require(receipt.Capture(Cache,Composite,source,Base,Read,CachePolicy::StableCounter,ContentPolicy::OwnerMembership) &&
        !receipt.ChangedDetails(),"Owner membership rejected prefix-only difference");
    Require(!receipt.DefinitionOwnershipProved() && !receipt.NativeExclusiveProved(),"Membership policy claimed ownership or exclusivity");
    Put<uint64_t>(Object0+24,0x8765);uint32_t detail=UINT32_MAX;
    Require(receipt.MatchesBefore(Read,&detail) && !detail,"MatchesBefore lost the membership policy");
    // Prefix differences must not hide raw topology differences or enter their diagnostic mask.
    Put<uint32_t>(NodeData+5*48+24,2);
    Require(!receipt.Capture(Cache,Composite,source,Base,Read,CachePolicy::StableCounter,ContentPolicy::OwnerMembership) &&
        receipt.ChangedDetails()==raw,"Prefix plus raw topology drift was accepted or misclassified");
    for(const bool order:{false,true}){
        source=Reset();auto changed=source;changed.nodes[0].definition[24]^=1;
        if(order)changed.nodes[0].order^=1;else changed.nodes[0].handle^=1;
        Require(!receipt.Capture(Cache,Composite,changed,Base,Read,CachePolicy::StableCounter,ContentPolicy::OwnerMembership) &&
            receipt.ChangedDetails()==raw,"Membership ignored node handle/order identity");
    }
    struct Change{uint64_t address,value;size_t bytes;};
    constexpr std::array<Change,9> changes{{
        {EdgeData+4,7,4},{Vtable0+8,Base+0x1ec1d8,8},{Object0,Vtable1,8},
        {Slot+8,0x11,8},{Owner0+0x7c,2,4},{Definitions0+8,Object1,8},
        {Definitions0+32,Allocator+8,8},{OwnerPointers+16,Allocator+8,8},{Slot,43,4}
    }};
    for(const auto& change:changes){
        source=Reset();Require(receipt.Capture(Cache,Composite,source,Base,Read,CachePolicy::StableCounter,ContentPolicy::OwnerMembership),
            "Stable membership fixture failed");
        Put<uint64_t>(Object0+24,0x4567);
        std::memcpy(memory.data()+(change.address-MemoryBase),&change.value,change.bytes);detail=0;
        Require(!receipt.MatchesBefore(Read,&detail) && !(detail&prefix),"Membership ignored non-prefix fields or reported ignored prefix");
    }
    source=Reset();triggerAddress=Slot;triggerCount=2;mutationAddress=Object0+24;mutationValue=0x4567;mutationBytes=8;
    Require(receipt.Capture(Cache,Composite,source,Base,Read,CachePolicy::AllowConcurrentHits,ContentPolicy::OwnerMembership) &&
        !receipt.ChangedDetails(),"Membership rejected a prefix change during capture");
    Controls();Put<uint32_t>(Slot,43);Put<uint64_t>(Object0+24,0x8765);detail=UINT32_MAX;
    Require(receipt.MatchesBefore(Read,&detail) && !detail,"Independent permissive policies were not retained");
    source=Reset();Require(!receipt.Capture(Cache,Composite,source,Base,Read,CachePolicy::StableCounter,static_cast<ContentPolicy>(99)) &&
        receipt.Rejection()==definition::Failure::Arguments && !receipt.Ready(),"Unknown content policy accepted");
    Require(receipt.Capture(Cache,Composite,source,Base,Read),"Default strict recapture failed");
    Put<uint64_t>(Object0+24,0x4567);detail=0;
    Require(!receipt.MatchesBefore(Read,&detail) && detail==prefix,"Membership policy leaked into default strict recapture");
}
}
int main()try{
    Positive();Negative();ChangesAndReadFailures();ConcurrentCacheHits();ChangedDiagnostics();OwnerMembershipPolicy();
    std::puts("PASS native definition source mapping: cache slots, unique owner allocations, shared node references, null slots and source revalidation");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
