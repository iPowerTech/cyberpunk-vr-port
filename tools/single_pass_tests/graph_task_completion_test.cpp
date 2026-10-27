#include "Render/NativeGraphTaskCompletion.hpp"
#include <cstdio>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <type_traits>

using namespace cvr::stereo;
using namespace cvr::stereo::graph_jobs;
namespace {
constexpr uint64_t Base=0x140000000,Worker=0x10000,Group=0x20000,Prerequisite=0x30000;
uint64_t Handle=Worker+0x68;
uint32_t CallbackReturn=ExecutionCallbackReturn;
std::map<uint64_t,uint8_t> memory;
uint64_t reads{},failAt{},throwAt{},changeAt{},changeAddress{},changeValue{};
uint32_t retained{},released{},completionReads{};
int32_t references{},initialReferences{},pending{};
bool changeOnRetain{},forbidReads{};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>void Put(uint64_t address,T value){
    const auto* bytes=reinterpret_cast<const uint8_t*>(&value);
    for(size_t i=0;i<sizeof(T);++i)memory[address+i]=bytes[i];
}
bool Read(uint64_t address,void* output,size_t size){
    Require(!forbidReads,"Completion reread expired worker context");++reads;
    if(reads==throwAt)throw std::runtime_error("Injected reader exception");
    if(reads==failAt)return false;if(reads==changeAt)Put(changeAddress,changeValue);
    auto* bytes=static_cast<uint8_t*>(output);
    for(size_t i=0;i<size;++i){const auto found=memory.find(address+i);if(found==memory.end())return false;bytes[i]=found->second;}
    return true;
}
void Retain(void* value){
    Require(reinterpret_cast<uintptr_t>(value)==Group && references>0 && references<INT32_MAX,"Invalid terminal retain");
    ++references;++retained;Put(Group+0x1c,references);
    if(changeOnRetain)Put(Handle,Group+0x100);
}
void Release(void** value){
    Require(value && reinterpret_cast<uintptr_t>(*value)==Group && references>initialReferences,"Invalid terminal release");
    --references;++released;Put(Group+0x1c,references);*value=nullptr;
}
bool Complete(void* value){Require(reinterpret_cast<uintptr_t>(value)==Group,"Wrong polled terminal group");++completionReads;return pending==0;}
constexpr JobLeaseApi Api{Retain,Release,Complete};
void Reset(){
    memory.clear();reads=failAt=throwAt=changeAt=changeAddress=changeValue=0;
    retained=released=completionReads=0;references=initialReferences=1;pending=1;changeOnRetain=forbidReads=false;
    Put(Worker+0x20,Handle);Put(Handle,Group);Put(Group+0x1c,references);Put(Group+0x18,pending);
    Put<int32_t>(Group+0x100+0x1c,1);Put<int32_t>(Group+0x100+0x18,1);
}
bool Capture(TaskCompletionLease& lease){return lease.CaptureBeforeCallback(Worker,Base+CallbackReturn,Base,Prerequisite,Read,Api);}
void Balanced(){Require(retained==released && references==initialReferences,"Terminal retain/release imbalance");}
void Lifecycle(){
    Reset();{TaskCompletionLease lease;Require(!lease.Captured() && !lease.OriginalReturned() && !lease.Complete() &&
        !lease.MarkOriginalReturned(),"New lease claimed callback completion");
        Require(Capture(lease) && lease.Group()==Group && lease.HandleAddress()==Handle && references==2,"Valid worker terminal rejected");
        Require(!lease.Complete() && completionReads==0,"Polled terminal before callback returned");
        pending=0;Require(!lease.Complete() && completionReads==0,"Early zero certified completion");
        pending=3;Require(lease.MarkOriginalReturned() && !lease.MarkOriginalReturned(),"Callback return must seal exactly once");
        memory.clear();forbidReads=true;
        Require(!lease.Complete() && completionReads==1,"Pending scoped children were ignored");
        pending=0;Require(lease.Complete() && completionReads==2,"Returned callback's completed group not recognized");
        Require(!Capture(lease) && lease.Rejection()==TaskCompletionFailure::AlreadyCaptured && lease.Complete(),"Recapture changed pinned terminal");
    }Balanced();
    Reset();{TaskCompletionLease cancelled;Require(Capture(cancelled),"Cancellation fixture capture failed");pending=0;
        Require(!cancelled.Complete() && !cancelled.OriginalReturned(),"Cancellation/zero implied original callback return");}Balanced();
    // A terminal group can conservatively aggregate more than one native consumer.
    Reset();{TaskCompletionLease first,second;Require(Capture(first) && Capture(second) && references==3,"Shared terminal group could not be pinned independently");
        Require(first.MarkOriginalReturned() && second.MarkOriginalReturned(),"Shared callback return failed");
        pending=2;Require(!first.Complete() && !second.Complete(),"Shared pending work lost");pending=0;
        Require(first.Complete() && second.Complete(),"Shared terminal completion lost");}Balanced();
}
void InvalidInputs(){
    Reset();
    const auto reject=[&](uint64_t worker,uint64_t caller,uint64_t base,uint64_t prerequisite,ReadMemory read,JobLeaseApi api){
        TaskCompletionLease lease;Require(!lease.CaptureBeforeCallback(worker,caller,base,prerequisite,read,api) && !lease.Captured(),"Invalid terminal arguments accepted");Balanced();
    };
    reject(0,Base+CallbackReturn,Base,Prerequisite,Read,Api);
    reject(Worker+1,Base+CallbackReturn,Base,Prerequisite,Read,Api);
    reject(UINT64_MAX-7,Base+CallbackReturn,Base,Prerequisite,Read,Api);
    reject(Worker,Base+CallbackReturn+1,Base,Prerequisite,Read,Api);
    reject(Worker,Base+CallbackReturn-1,Base,Prerequisite,Read,Api);
    reject(Worker,Base+CallbackReturn,0,Prerequisite,Read,Api);
    reject(Worker,Base+CallbackReturn,UINT64_MAX,Prerequisite,Read,Api);
    reject(Worker,Base+CallbackReturn,Base,0,Read,Api);
    reject(Worker,Base+CallbackReturn,Base,Prerequisite+1,Read,Api);
    reject(Worker,Base+CallbackReturn,Base,Prerequisite,nullptr,Api);
    for(unsigned missing=0;missing<3;++missing){auto api=Api;
        if(missing==0)api.retain=nullptr;if(missing==1)api.release=nullptr;if(missing==2)api.complete=nullptr;
        reject(Worker,Base+CallbackReturn,Base,Prerequisite,Read,api);
    }
    Reset();{TaskCompletionLease lease;
        const auto otherCaller=CallbackReturn==ExecutionCallbackReturn?AggregateExecutionCallbackReturn:ExecutionCallbackReturn;
        Require(!lease.CaptureBeforeCallback(Worker,Base+otherCaller,Base,Prerequisite,Read,Api) &&
            lease.Rejection()==TaskCompletionFailure::HandleAddress,"Known caller accepted the other worker layout");}Balanced();
    Reset();{TaskCompletionLease lease;const uint64_t handleOffset=Handle-Worker;
        Require(!lease.CaptureBeforeCallback(UINT64_MAX-handleOffset-7,Base+CallbackReturn,Base,Prerequisite,Read,Api) &&
            lease.Rejection()==TaskCompletionFailure::Arguments && !reads,"Worker range overflow reached memory reader");}Balanced();
    for(const uint64_t handle:{uint64_t(0),Group,Handle+8,UINT64_MAX-7}){
        Reset();Put(Worker+0x20,handle);TaskCompletionLease lease;
        Require(!Capture(lease) && lease.Rejection()==TaskCompletionFailure::HandleAddress,"Worker group mistaken for handle address");Balanced();
    }
    for(const uint64_t group:{uint64_t(0),Group+1,Prerequisite,UINT64_MAX-7}){
        Reset();Put(Handle,group);TaskCompletionLease lease;
        Require(!Capture(lease) && !lease.Captured(),"Invalid or prerequisite-aliased terminal group accepted");Balanced();
    }
    for(const int32_t count:{0,-1,INT32_MAX}){
        Reset();Put(Group+0x1c,count);TaskCompletionLease lease;
        Require(!Capture(lease) && lease.Rejection()==TaskCompletionFailure::References,"Dead/overflowing terminal reference accepted");Balanced();
    }
    for(const int32_t count:{0,-1}){
        Reset();Put(Group+0x18,count);TaskCompletionLease lease;
        Require(!Capture(lease) && lease.Rejection()==TaskCompletionFailure::NoCallbackToken,"Fallback/invalid pending group accepted");Balanced();
    }
    Reset();references=initialReferences=INT32_MAX-1;Put(Group+0x1c,references);
    {TaskCompletionLease lease;Require(Capture(lease) && references==INT32_MAX,"Final nonoverflowing retain rejected");}Balanced();
}
void FailedReadsAndChanges(){
    Reset();uint64_t total{};{TaskCompletionLease lease;Require(Capture(lease),"Baseline capture failed");total=reads;}Balanced();
    for(uint64_t at=1;at<=total;++at)for(const bool throws:{false,true}){
        Reset();if(throws)throwAt=at;else failAt=at;
        {TaskCompletionLease lease;Require(!Capture(lease) && !lease.Captured() && !lease.Complete(),"Partial/throwing memory capture accepted");}Balanced();
    }
    Reset();changeOnRetain=true;
    {TaskCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==TaskCompletionFailure::Changed,"Post-retain changed identity accepted");}Balanced();
    Require(retained==1 && released==1,"Post-retain rollback was not exercised");
    for(const uint64_t at:{uint64_t(5),uint64_t(13)}){
        Reset();changeAt=at;changeAddress=Handle;changeValue=Group+0x100;
        {TaskCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==TaskCompletionFailure::Changed,"Torn group identity accepted");}Balanced();
    }
    Reset();changeAt=13;changeAddress=Worker+0x20;changeValue=Handle+8;
    {TaskCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==TaskCompletionFailure::HandleAddress,"Changed post-retain handle address accepted");}Balanced();
    // The callback token must remain present throughout entry capture, even after the extra retain.
    Reset();changeAt=13;changeAddress=Group+0x18;changeValue=uint64_t(2)<<32;
    {TaskCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==TaskCompletionFailure::NoCallbackToken,"Post-retain early zero accepted");}Balanced();
}
}
int main()try{
    static_assert(!std::is_copy_constructible_v<TaskCompletionLease> && !std::is_move_constructible_v<TaskCompletionLease>);
    for(const uint32_t caller:{ExecutionCallbackReturn,AggregateExecutionCallbackReturn}){
        CallbackReturn=caller;Handle=Worker+(caller==ExecutionCallbackReturn?0x68:0x150);
        Lifecycle();InvalidInputs();FailedReadsAndChanges();
    }
    std::puts("PASS graph task completion: two exact scheduler layouts, callback token, retained terminal, one-shot return gate and rollback");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
