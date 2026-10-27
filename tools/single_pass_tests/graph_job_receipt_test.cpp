#include "Render/NativeGraphJobReceipt.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>

using namespace cvr::stereo;
using namespace cvr::stereo::graph_jobs;
namespace {
constexpr uint64_t Base=0x140000000,Payload=0x10000,Frame=0x18000,Graph=0x20000,Nodes=0x30000,Order=0x40000,Header=0x50000,Groups=0x60000;
constexpr uint64_t Epoch=13,Generation=47;
std::map<uint64_t,uint8_t> memory;
std::map<uint64_t,int32_t> groupRefs,groupPending;
int32_t frameRefs{};uint32_t reads{},ownerRetains{},ownerReleases{},jobRetains{},jobReleases{},changeAfterJobRetain{};
bool readsForbidden{};
void Require(bool value){if(!value)throw std::runtime_error("Deferred graph job receipt assertion failed");}
template<class T>void Put(uint64_t address,const T& value){const auto* data=reinterpret_cast<const uint8_t*>(&value);for(size_t i=0;i<sizeof(T);++i)memory[address+i]=data[i];}
template<class T,size_t N>void Field(std::array<uint8_t,N>& bytes,size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
bool Read(uint64_t address,void* out,size_t bytes){
    Require(!readsForbidden);++reads;if(address>UINT64_MAX-bytes)return false;
    auto* data=static_cast<uint8_t*>(out);for(size_t i=0;i<bytes;++i){const auto found=memory.find(address+i);if(found==memory.end())return false;data[i]=found->second;}return true;
}
void RetainOwner(void* value){Require(reinterpret_cast<uintptr_t>(value)==Frame);++frameRefs;++ownerRetains;Put(Frame+8,frameRefs);}
void ReleaseOwner(void* value){Require(reinterpret_cast<uintptr_t>(value)==Frame && frameRefs>1 && jobReleases==jobRetains);--frameRefs;++ownerReleases;Put(Frame+8,frameRefs);}
void RetainJob(void* value){const auto group=reinterpret_cast<uintptr_t>(value);Require(groupRefs.contains(group));++groupRefs[group];++jobRetains;Put(group+0x1C,groupRefs[group]);
    if(jobRetains==changeAfterJobRetain)Put(Header,NativeArray{Groups,2,1});}
void ReleaseJob(void** value){Require(value && *value);const auto group=reinterpret_cast<uintptr_t>(*value);Require(groupRefs.contains(group) && groupRefs[group]>1);
    --groupRefs[group];++jobReleases;Put(group+0x1C,groupRefs[group]);*value=nullptr;}
bool CompleteJob(void* value){return groupPending.at(reinterpret_cast<uintptr_t>(value))==0;}
constexpr JobLeaseApi Jobs{RetainJob,ReleaseJob,CompleteJob};
constexpr FrameOwnerApi Owners{RetainOwner,ReleaseOwner};
graph_snapshot::Snapshot Fresh(){
    memory.clear();groupRefs.clear();groupPending.clear();frameRefs=1;reads=ownerRetains=ownerReleases=jobRetains=jobReleases=changeAfterJobRetain=0;readsForbidden=false;
    Put(Payload,Frame);Put(Frame+8,int32_t(1));std::array<uint8_t,0x70> header{};Field(header,0x10,uint32_t(2));Field(header,0x18,Nodes);Field(header,0x28,Order);Put(Graph,header);
    Put(Header,NativeArray{Groups,2,2});graph_snapshot::Snapshot source;
    for(uint32_t i=0;i<2;++i){graph_snapshot::Node node;node.handle=17+i*4;node.order=i;Field(node.bytes,0,i);Field(node.bytes,8,uint32_t(4));Field(node.bytes,24,i);
        source.nodes.push_back(node);Put(Order+4*i,node.handle);Put(Nodes+uint64_t(node.handle)*48,node.bytes);
        const uint64_t begin=0x80000+i*0x100,end=i?begin:begin+0x40;Put(Groups+24*i,NodeGroups{begin,end,i,1-i});
        groupRefs[begin]=1;groupRefs[end]=1;groupPending[begin]=0;groupPending[end]=i?0:1;Put(begin+0x1C,int32_t(1));Put(end+0x1C,int32_t(1));}
    return source;
}
CapturePlan Plan(const graph_snapshot::Snapshot& source){CapturePlan plan;Require(plan.Capture(Payload,Graph,Header,Base+QueueReturn,Base,source,Read));return plan;}
bool Match(const DeferredReceipt& receipt,const graph_snapshot::Snapshot& source){return receipt.Match(Epoch,Generation,Base,Graph,Frame,source);}
void Balanced(){Require(frameRefs==1 && ownerRetains==ownerReleases && jobRetains==jobReleases);for(const auto& [_,refs]:groupRefs)Require(refs==1);}
}
int main()try{
    Require(MaxPendingReceipts==4);auto source=Fresh();auto plan=Plan(source);std::shared_ptr<const graph_snapshot::Snapshot> held;
    {DeferredReceipt receipt(Jobs,Owners);Require(!receipt.Captured() && !receipt.Selected() && !receipt.Complete() && !Match(receipt,source));
        Require(receipt.Capture(plan,Epoch,Generation,Base,Read) && receipt.Captured() && !receipt.Selected() && receipt.NodeCount()==2);
        Require(frameRefs==2 && ownerRetains==1 && jobRetains==3 && !receipt.Complete());
        Require(receipt.Epoch()==Epoch && receipt.Generation()==Generation && receipt.ImageBase()==Base && receipt.GraphAddress()==Graph && receipt.FrameOwner()==Frame);
        held=receipt.Source();Require(held && held->nodes==source.nodes);
        // Queue callback payload and group-array header have expired. All late
        // matching/polling uses only immutable copied metadata and held refs.
        memory.clear();readsForbidden=true;const auto readCount=reads;plan=CapturePlan{};
        groupPending[0x80040]=0;Require(receipt.Complete() && !receipt.Selected() && Match(receipt,source));
        Require(!receipt.Match(Epoch+1,Generation,Base,Graph,Frame,source));
        Require(!receipt.Match(Epoch,Generation+1,Base,Graph,Frame,source));
        Require(!receipt.Match(Epoch,Generation,Base+0x10000,Graph,Frame,source));
        Require(!receipt.Match(Epoch,Generation,Base,Graph+0x10000,Frame,source));
        Require(!receipt.Match(Epoch,Generation,Base,Graph,Frame+0x10000,source));
        auto changed=source;changed.nodes[0].bytes[12]^=1;Require(!Match(receipt,changed));
        changed=source;changed.nodes[0].definition[8]^=1;Require(!Match(receipt,changed));
        changed=source;changed.failure=1;Require(!Match(receipt,changed));
        changed=source;changed.edges.emplace_back();Require(!Match(receipt,changed));
        // Same numeric source/owner addresses from a later generation are not
        // an identity match, even if its graph bytes happen to be identical.
        Require(!receipt.Select(Epoch,Generation+1,Base,Graph,Frame,source));
        Require(receipt.Select(Epoch,Generation,Base,Graph,Frame,source) && receipt.Selected() && receipt.Complete());
        Require(!receipt.Select(Epoch,Generation,Base,Graph,Frame,source) && Match(receipt,source));
        Require(reads==readCount);
    }
    Balanced();Require(held && held->nodes==source.nodes);readsForbidden=false;
    source=Fresh();plan=Plan(source);changeAfterJobRetain=1;
    {DeferredReceipt receipt(Jobs,Owners);Require(!receipt.Capture(plan,Epoch,Generation,Base,Read) && receipt.Rejection()==ReceiptFailure::JobCapture);
        Require(!receipt.Captured() && !receipt.Selected() && !receipt.Complete() && !receipt.Source());}
    Balanced();Require(ownerRetains==1 && jobRetains==3);
    source=Fresh();plan=Plan(source);Put(Payload,uint64_t(Frame+0x10000));
    {DeferredReceipt receipt(Jobs,Owners);Require(!receipt.Capture(plan,Epoch,Generation,Base,Read) && receipt.Rejection()==ReceiptFailure::PlanChanged);}
    Balanced();Require(ownerRetains==0 && jobRetains==0);
    source=Fresh();plan=Plan(source);Put(Frame+8,int32_t(0));
    {DeferredReceipt receipt(Jobs,Owners);Require(!receipt.Capture(plan,Epoch,Generation,Base,Read) && receipt.Rejection()==ReceiptFailure::OwnerReference);}
    Balanced();Require(ownerRetains==0 && jobRetains==0);
    source=Fresh();plan=Plan(source);
    {DeferredReceipt receipt(Jobs,Owners);Require(!receipt.Capture(plan,0,Generation,Base,Read) && receipt.Rejection()==ReceiptFailure::Arguments);
        Require(!receipt.Capture(plan,Epoch,0,Base,Read));Require(!receipt.Capture(plan,Epoch,Generation,0,Read));
        Require(!receipt.Capture(plan,Epoch,Generation,Base+0x10000,Read));
        Require(!receipt.Capture(plan,Epoch,Generation,Base,nullptr));Require(receipt.Capture(plan,Epoch,Generation,Base,Read));
        Require(!receipt.Capture(plan,Epoch,Generation,Base,Read) && receipt.Rejection()==ReceiptFailure::AlreadyCaptured);}
    Balanced();
    source=Fresh();plan=Plan(source);
    {DeferredReceipt ownerMissing(Jobs,{nullptr,ReleaseOwner});Require(!ownerMissing.Capture(plan,Epoch,Generation,Base,Read) && ownerMissing.Rejection()==ReceiptFailure::MissingApi);}
    {DeferredReceipt jobMissing({RetainJob,nullptr,CompleteJob},Owners);Require(!jobMissing.Capture(plan,Epoch,Generation,Base,Read) && jobMissing.Rejection()==ReceiptFailure::MissingApi);}
    Balanced();Require(ownerRetains==0 && jobRetains==0);
    std::puts("PASS deferred graph-job receipt: completion-before-selection, expired payload independence, epoch/generation ABA checks and balanced owner/group RAII");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
