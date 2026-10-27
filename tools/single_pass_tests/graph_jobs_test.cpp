#include "Render/NativeGraphJobs.hpp"
#include "Render/NativeDefinitionOwner.hpp"
#include <cstring>
#include <cstdio>
#include <map>
#include <stdexcept>

using namespace cvr::stereo;
using namespace cvr::stereo::graph_jobs;
namespace {
constexpr uint64_t Base=0x140000000,Owner=0x10000,Frame=0x11000,Graph=0x20000,Nodes=0x30000,NodeOrder=0x40000,GroupHeader=0x50000,GroupData=0x60000;
std::map<uint64_t,uint8_t> memory;
std::map<uint64_t,int32_t> references,pending;
uint32_t reads{},retainCount{},releaseCount{},changeOnRetain{};
uint64_t mutateAfterRead{},mutateAddress{};uint8_t mutateByte{};
void Require(bool value){if(!value)throw std::runtime_error("Native graph jobs assertion failed");}
template<class T>void Put(uint64_t address,const T& value){const auto* bytes=reinterpret_cast<const uint8_t*>(&value);for(size_t i=0;i<sizeof(T);++i)memory[address+i]=bytes[i];}
template<class T,size_t N>void Field(std::array<uint8_t,N>& bytes,size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
bool Read(uint64_t address,void* output,size_t bytes){
    if(address>UINT64_MAX-bytes)return false;auto* out=static_cast<uint8_t*>(output);
    for(size_t i=0;i<bytes;++i){const auto found=memory.find(address+i);if(found==memory.end())return false;out[i]=found->second;}
    if(++reads==mutateAfterRead)memory[mutateAddress]=mutateByte;return true;
}
void Retain(void* value){const auto group=reinterpret_cast<uintptr_t>(value);Require(references.contains(group));
    ++references[group];++retainCount;Put(group+0x1C,references[group]);if(retainCount==changeOnRetain)Put(GroupHeader,NativeArray{GroupData,8,1});}
void Release(void** value){Require(value && *value);const auto group=reinterpret_cast<uintptr_t>(*value);Require(references.contains(group) && references[group]>1);
    --references[group];++releaseCount;Put(group+0x1C,references[group]);*value=nullptr;}
bool Complete(void* value){return pending.at(reinterpret_cast<uintptr_t>(value))==0;}
const JobLeaseApi Api{Retain,Release,Complete};
graph_snapshot::Snapshot Fresh(uint32_t count=3){
    memory.clear();references.clear();pending.clear();reads=retainCount=releaseCount=changeOnRetain=0;mutateAfterRead=mutateAddress=0;mutateByte=0;
    Put(Owner,Frame);std::array<uint8_t,0x70> header{};Field(header,0x10,count);Field(header,0x18,Nodes);Field(header,0x28,NodeOrder);Put(Graph,header);
    Put(GroupHeader,NativeArray{GroupData,count+2,count});graph_snapshot::Snapshot source;
    for(uint32_t i=0;i<count;++i){graph_snapshot::Node node;node.handle=7+i*3;node.order=i;
        Field(node.bytes,0,i);Field(node.bytes,8,uint32_t(4));Field(node.bytes,24,i%2);
        source.nodes.push_back(node);Put(NodeOrder+i*4,node.handle);Put(Nodes+uint64_t(node.handle)*48,node.bytes);
        const uint64_t begin=0x80000+uint64_t(i)*0x100,end=i==1?begin:begin+0x40;
        Put(GroupData+uint64_t(i)*24,NodeGroups{begin,end,i,count-1-i});
        references[begin]=1;references[end]=1;pending[begin]=0;pending[end]=i==0?1:0;
        Put(begin+0x1C,int32_t(1));Put(end+0x1C,int32_t(1));
    }
    return source;
}
bool Capture(CapturePlan& plan,const graph_snapshot::Snapshot& source){return plan.Capture(Owner,Graph,GroupHeader,Base+QueueReturn,Base,source,Read);}
void Reject(const graph_snapshot::Snapshot& source,Failure expected){CapturePlan plan;Require(!Capture(plan,source) && !plan.Ready() && plan.Rejection()==expected && !plan.Matches(Read));}
}
int main()try{
    auto source=Fresh();CapturePlan plan;Require(Capture(plan,source) && plan.Ready() && plan.Nodes().size()==3 && plan.Matches(Read));
    Require(plan.FrameOwner()==Frame && plan.OwnerAddress()==Owner && plan.GraphAddress()==Graph && plan.GroupsAddress()==GroupHeader && plan.Array()==NativeArray{GroupData,5,3});
    for(uint32_t i=0;i<3;++i)Require(plan.Nodes()[i].node==7+i*3 && plan.Nodes()[i].ordinal==i);
    const auto kept=plan.Source();Require(kept && kept->nodes==source.nodes);
    {LeaseSet leases(Api);Require(!leases.Complete() && !leases.Complete(0,7) && leases.Capture(plan,Read) && leases.Sealed() && leases.Nodes()==3 && retainCount==5 && !leases.Complete());
        Require(!leases.Complete(0,7) && leases.Complete(1,10) && leases.Complete(2,13));
        Require(!leases.Complete(0,10) && !leases.Complete(1,7) && !leases.Complete(3,16) && !leases.Complete(UINT32_MAX,7));
        Require(!leases.Complete(0,7|0x20000000u));
        Require(!leases.Capture(plan,Read));pending[0x80040]=0;Require(leases.Complete() && leases.Complete(0,7));
        definition_owner::RetireState retirement(1,true);Require(retirement.AddConsumer(1,22) && retirement.VerifyDetach(1,true) && retirement.CloseSourceUses(1));
        Require(!retirement.ReadyToRetire());if(leases.Complete())Require(retirement.CompleteConsumer(1,22));Require(retirement.ReadyToRetire());}
    Require(releaseCount==retainCount && releaseCount==5);for(const auto& [_,refs]:references)Require(refs==1);
    source=Fresh();Require(Capture(plan,source));
    {LeaseSet selected(Api);Require(selected.Capture(plan,Read));const auto readCount=reads;
        memory.clear();source={};plan=CapturePlan{};
        Require(!selected.Complete(0,7) && selected.Complete(1,10) && selected.Complete(2,13) && reads==readCount);
        pending[0x80200]=1;Require(!selected.Complete(2,13) && selected.Complete(1,10));
        pending[0x80200]=0;pending[0x80040]=0;Require(selected.Complete(0,7) && selected.Complete());}
    Require(retainCount==releaseCount && releaseCount==5);for(const auto& [_,refs]:references)Require(refs==1);
    source=Fresh();Require(Capture(plan,source));changeOnRetain=2;
    {LeaseSet invalid(Api);Require(!invalid.Capture(plan,Read) && !invalid.Sealed() && !invalid.Complete() && !invalid.Complete(0,7));}
    Require(retainCount==releaseCount && retainCount==5);for(const auto& [_,refs]:references)Require(refs==1);
    source=Fresh();Require(Capture(plan,source));{LeaseSet missing({nullptr,Release,Complete});Require(!missing.Capture(plan,Read) && !missing.Complete());}
    Require(retainCount==0);
    source=Fresh();Require(!plan.Capture(Owner,Graph,GroupHeader,Base+QueueReturn+1,Base,source,Read) && plan.Rejection()==Failure::Caller);
    Require(!plan.Capture(Owner,Graph,GroupHeader,Base+QueueReturn,Base,source,nullptr) && plan.Rejection()==Failure::Arguments);
    Require(!plan.Capture(Owner+1,Graph,GroupHeader,Base+QueueReturn,Base,source,Read) && plan.Rejection()==Failure::Arguments);
    Require(!plan.Capture(Owner,UINT64_MAX-7,GroupHeader,Base+QueueReturn,Base,source,Read) && plan.Rejection()==Failure::Arguments);
    source=Fresh();source.failure=1;Reject(source,Failure::Source);
    source=Fresh();source.nodes[1].order=0;Reject(source,Failure::Source);
    source=Fresh();source.nodes[1].handle=source.nodes[0].handle;Reject(source,Failure::Source);
    source=Fresh();Put(Owner,uint64_t(0));Reject(source,Failure::Owner);
    source=Fresh();memory.erase(GroupHeader);Reject(source,Failure::Header);
    source=Fresh();Put(GroupHeader,NativeArray{GroupData,2,3});Reject(source,Failure::Count);
    source=Fresh();Put(GroupHeader,NativeArray{GroupData,3,2});Reject(source,Failure::Count);
    source=Fresh();Put(GroupHeader,NativeArray{GroupData,8193,3});Reject(source,Failure::Count);
    source=Fresh();Put(GroupHeader,NativeArray{UINT64_MAX-7,3,3});Reject(source,Failure::Count);
    source=Fresh();Put(Graph+0x10,uint32_t(2));Reject(source,Failure::Count);
    source=Fresh();memory[Nodes+7*48+12]^=1;Reject(source,Failure::Source);
    source=Fresh();Put(GroupData,NodeGroups{0,0x80040,0,2});Reject(source,Failure::Groups);
    source=Fresh();Put(GroupData,NodeGroups{0x80001,0x80040,0,2});Reject(source,Failure::Groups);
    source=Fresh();Put(0x80000+0x1C,int32_t(0));Reject(source,Failure::Groups);
    source=Fresh();Put(GroupData,NodeGroups{0x80000,0x80040,65537,0});Reject(source,Failure::Groups);
    source=Fresh();Require(Capture(plan,source));Put(Owner,uint64_t(Frame+0x100));Require(!plan.Matches(Read));
    source=Fresh();Require(Capture(plan,source));Put(GroupData+8,uint64_t(0x80100));Require(!plan.Matches(Read));
    // Mutate a stable pointer field during the first whole receipt read.
    source=Fresh();mutateAfterRead=1;mutateAddress=Owner+1;mutateByte=0x20;Reject(source,Failure::Changed);
    source=Fresh(MaxNodes);Require(Capture(plan,source) && plan.Nodes().size()==MaxNodes);
    source=Fresh(MaxNodes+1);Reject(source,Failure::Source);
    std::puts("PASS native graph jobs: actual ordinal-to-group mapping, immutable source receipt, balanced retained group leases and all-node completion retirement gate");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
