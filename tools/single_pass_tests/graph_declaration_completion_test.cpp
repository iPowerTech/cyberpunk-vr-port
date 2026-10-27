#include "Render/NativeGraphDeclarationCompletion.hpp"
#include <cstdio>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <type_traits>

using namespace cvr::stereo;
using namespace cvr::stereo::graph_jobs;
namespace {
constexpr uint64_t Base=0x140000000,Context=0x10000,OwnerAddress=Context+0x38,Frame=0x20000,Graph=0x30000,Group=0x40000,Rotated=0x50000;
constexpr uint64_t Epoch=11,Generation=12,Consumer=13;
std::map<uint64_t,uint8_t> memory;
uint64_t reads{},failAt{},throwAt{},changeAt{},changeAddress{},changeValue{};
int32_t refs{},baselineRefs{},pending{};
uint32_t retains{},releases{},completeCalls{};
bool changeOnRetain{},forbidReads{};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>void Put(uint64_t address,T value){
    const auto* bytes=reinterpret_cast<const uint8_t*>(&value);
    for(size_t i=0;i<sizeof(T);++i)memory[address+i]=bytes[i];
}
bool Read(uint64_t address,void* output,size_t bytes){
    Require(!forbidReads,"Declaration lease reread expired context");++reads;
    if(reads==throwAt)throw std::runtime_error("Injected declaration reader failure");
    if(reads==failAt)return false;if(reads==changeAt)Put(changeAddress,changeValue);
    auto* out=static_cast<uint8_t*>(output);
    for(size_t i=0;i<bytes;++i){const auto found=memory.find(address+i);if(found==memory.end())return false;out[i]=found->second;}
    return true;
}
void Retain(void* group){
    Require(reinterpret_cast<uintptr_t>(group)==Group && refs>0 && refs<INT32_MAX,"Invalid declaration group retain");
    ++refs;++retains;Put(Group+0x1c,refs);if(changeOnRetain)Put(Context+0x18,Rotated);
}
void Release(void** group){
    Require(group && reinterpret_cast<uintptr_t>(*group)==Group && refs>0 && releases<retains,"Invalid declaration group release");
    --refs;++releases;Put(Group+0x1c,refs);*group=nullptr;
}
bool Complete(void* group){Require(reinterpret_cast<uintptr_t>(group)==Group,"Polled rotated declaration group");++completeCalls;return pending==0;}
constexpr JobLeaseApi Api{Retain,Release,Complete};
void Reset(){
    memory.clear();reads=failAt=throwAt=changeAt=changeAddress=changeValue=0;
    refs=baselineRefs=1;pending=0;retains=releases=completeCalls=0;changeOnRetain=forbidReads=false;
    Put(OwnerAddress,Frame);Put(Context+0x18,Group);Put(Group+0x1c,refs);Put(Group+0x18,pending);
    Put<int32_t>(Rotated+0x1c,1);Put<int32_t>(Rotated+0x18,0);
}
bool Capture(DeclarationCompletionLease& lease,uint64_t epoch=Epoch,uint64_t generation=Generation,uint64_t consumer=Consumer,uint64_t graph=Graph){
    return lease.CaptureBeforeConstruct(OwnerAddress,graph,Base+DeclarationConstructReturn,Base,epoch,generation,consumer,Read,Api);
}
bool Match(DeclarationCompletionLease& lease){return lease.MatchBeforeDeclare(Context,lease.ExecutionGraph(),Base+DeclarationReturn,Read);}
void Balanced(){Require(refs==baselineRefs && retains==releases,"Declaration group pin imbalance");}
void Lifecycle(){
    for(uint64_t cycle=0;cycle<2048;++cycle){Reset();
        {DeclarationCompletionLease lease;Require(!lease.Captured() && !lease.Complete(),"New declaration lease completed");
            Require(Capture(lease) && lease.ContextAddress()==Context && lease.Group()==Group && refs==2,"Valid zero-pending capture rejected");
            Require(!lease.Complete() && !completeCalls,"Early zero invoked terminal poll before enqueue");
            Require(Match(lease) && lease.Matched(),"Before-declare identity rejected");
            Require(!lease.Complete() && !completeCalls,"Before-declare zero became completion");
            Require(lease.MarkOriginalDeclareReturned() && !lease.Complete() && !completeCalls,"Declare return completed open producer");
            Put(Context+0x18,Rotated);pending=2;
            Require(lease.MarkOriginalRootReturned() && !lease.ContextAddress(),"Producer close retained context address");
            --refs;baselineRefs=0;memory.clear();forbidReads=true;
            Require(!lease.Complete() && completeCalls==1,"Scoped children were ignored");
            pending=0;Require(lease.Complete() && completeCalls==2,"Frozen D0 did not complete after context rotation/destruction");
        }Balanced();
    }
}
void InvalidCapture(){
    const auto reject=[](uint64_t owner,uint64_t graph,uint64_t caller,uint64_t base,uint64_t epoch,uint64_t generation,uint64_t consumer,ReadMemory reader,JobLeaseApi api){
        Reset();{DeclarationCompletionLease lease;Require(!lease.CaptureBeforeConstruct(owner,graph,caller,base,epoch,generation,consumer,reader,api) &&
            !lease.Captured() && !lease.Complete(),"Invalid capture accepted");}Balanced();
    };
    reject(0,Graph,Base+DeclarationConstructReturn,Base,Epoch,Generation,Consumer,Read,Api);
    reject(0x30,Graph,Base+DeclarationConstructReturn,Base,Epoch,Generation,Consumer,Read,Api);
    reject(OwnerAddress+1,Graph,Base+DeclarationConstructReturn,Base,Epoch,Generation,Consumer,Read,Api);
    reject(UINT64_MAX-7,Graph,Base+DeclarationConstructReturn,Base,Epoch,Generation,Consumer,Read,Api);
    reject(OwnerAddress,Graph+1,Base+DeclarationConstructReturn,Base,Epoch,Generation,Consumer,Read,Api);
    reject(OwnerAddress,UINT64_MAX-7,Base+DeclarationConstructReturn,Base,Epoch,Generation,Consumer,Read,Api);
    reject(OwnerAddress,Graph,Base+DeclarationConstructReturn+1,Base,Epoch,Generation,Consumer,Read,Api);
    reject(OwnerAddress,Graph,Base+DeclarationConstructReturn,UINT64_MAX,Epoch,Generation,Consumer,Read,Api);
    reject(OwnerAddress,Graph,Base+DeclarationConstructReturn,Base,0,Generation,Consumer,Read,Api);
    reject(OwnerAddress,Graph,Base+DeclarationConstructReturn,Base,Epoch,0,Consumer,Read,Api);
    reject(OwnerAddress,Graph,Base+DeclarationConstructReturn,Base,Epoch,Generation,0,Read,Api);
    reject(OwnerAddress,Graph,Base+DeclarationConstructReturn,Base,Epoch,Generation,Consumer,nullptr,Api);
    for(unsigned missing=0;missing<3;++missing){auto api=Api;
        if(missing==0)api.retain=nullptr;if(missing==1)api.release=nullptr;if(missing==2)api.complete=nullptr;
        reject(OwnerAddress,Graph,Base+DeclarationConstructReturn,Base,Epoch,Generation,Consumer,Read,api);
    }
    for(const uint64_t group:{uint64_t(0),Group+1,Frame,UINT64_MAX-7}){Reset();Put(Context+0x18,group);
        {DeclarationCompletionLease lease;Require(!Capture(lease) && !lease.Complete(),"Invalid D0 identity accepted");}Balanced();}
    for(const uint64_t owner:{uint64_t(0),Frame+1,UINT64_MAX-7}){Reset();Put(OwnerAddress,owner);
        {DeclarationCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==DeclarationCompletionFailure::FrameOwner,
            "Invalid frame identity accepted");}Balanced();}
    for(const int32_t count:{0,-1,INT32_MAX}){Reset();Put(Group+0x1c,count);
        {DeclarationCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==DeclarationCompletionFailure::References,"Dead/overflowing D0 accepted");}Balanced();}
    Reset();Put<int32_t>(Group+0x18,-1);
    {DeclarationCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==DeclarationCompletionFailure::Pending,"Negative pending accepted");}Balanced();
    Reset();refs=baselineRefs=INT32_MAX-1;Put(Group+0x1c,refs);
    {DeclarationCompletionLease lease;Require(Capture(lease) && refs==INT32_MAX,"Last nonoverflowing D0 retain rejected");}Balanced();
}
void PartialReadsAndChanges(){
    Reset();uint64_t captureReads{};{DeclarationCompletionLease lease;Require(Capture(lease),"Capture read baseline failed");captureReads=reads;}Balanced();
    for(uint64_t at=1;at<=captureReads;++at)for(const bool throws:{false,true}){Reset();if(throws)throwAt=at;else failAt=at;
        {DeclarationCompletionLease lease;Require(!Capture(lease) && !lease.Captured() && !lease.Complete(),"Partial/throwing capture accepted");}Balanced();}
    Reset();changeOnRetain=true;
    {DeclarationCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==DeclarationCompletionFailure::Changed,"Retain-time D0 rotation accepted");}Balanced();
    Require(retains==1 && releases==1,"Post-retain rollback was not exercised");
    for(const uint64_t address:{OwnerAddress,Context+0x18}){Reset();changeAt=5;changeAddress=address;changeValue=address==OwnerAddress?Frame+0x100:Rotated;
        {DeclarationCompletionLease lease;Require(!Capture(lease) && lease.Rejection()==DeclarationCompletionFailure::Changed,"Torn capture identity accepted");}Balanced();}
    Reset();uint64_t matchReads{};{DeclarationCompletionLease lease;Require(Capture(lease),"Match baseline capture failed");reads=0;
        Require(Match(lease),"Match read baseline failed");matchReads=reads;}Balanced();
    for(uint64_t at=1;at<=matchReads;++at)for(const bool throws:{false,true}){Reset();
        {DeclarationCompletionLease lease;Require(Capture(lease),"Match failure capture failed");reads=0;if(throws)throwAt=at;else failAt=at;
            Require(!Match(lease) && lease.Failed() && !lease.Complete(),"Partial/throwing match accepted");
            Require(!lease.MarkOriginalRootReturned() && !lease.ContextAddress(),"Failed match retained producer address");
        }Balanced();}
}
void MatchFailures(){
    for(unsigned failure=0;failure<9;++failure){Reset();
        {DeclarationCompletionLease lease;Require(Capture(lease),"Match fixture capture failed");uint64_t job=Context,graph=Graph,caller=Base+DeclarationReturn;ReadMemory reader=Read;
            if(failure==0)++job;if(failure==1)graph+=8;if(failure==2)++caller;if(failure==3)--caller;
            if(failure==4)reader=nullptr;if(failure==5)Put(Context+0x18,Rotated);if(failure==6)Put(OwnerAddress,Frame+0x100);
            if(failure==7){changeAt=reads+5;changeAddress=Context+0x18;changeValue=Rotated;}
            if(failure==8)Put<int32_t>(Group+0x1c,0);
            Require(!lease.MatchBeforeDeclare(job,graph,caller,reader) && lease.Failed() && !lease.Complete(),"Bad declaration identity accepted");
            lease.MarkOriginalDeclareReturned();lease.MarkOriginalRootReturned();forbidReads=true;
            Require(!lease.Complete() && !lease.ContextAddress() && !completeCalls,"Failed match recovered into completion");
        }Balanced();
    }
    Reset();{DeclarationCompletionLease lease;Require(Capture(lease) && Match(lease),"Duplicate match fixture failed");
        Require(!Match(lease) && lease.Failed() && !lease.Complete(),"Second declaration match accepted");}Balanced();
    Reset();{DeclarationCompletionLease lease;Require(Capture(lease) && lease.MarkOriginalRootReturned(),"Producer expiry fixture failed");forbidReads=true;
        Require(!Match(lease) && lease.Rejection()==DeclarationCompletionFailure::ProducerClosed && !lease.Complete(),"Late match touched expired context");}Balanced();
}
void MissingReturnsAndRearm(){
    for(unsigned missing=0;missing<3;++missing){Reset();
        {DeclarationCompletionLease lease;Require(Capture(lease),"Missing-return capture failed");
            if(missing!=0)Require(Match(lease),"Missing-return match failed");
            if(missing!=1 && missing!=0)Require(lease.MarkOriginalDeclareReturned(),"Missing-return declaration fixture failed");
            if(missing!=2)Require(lease.MarkOriginalRootReturned(),"Missing-return producer fixture failed");
            Require(!lease.Complete() && !completeCalls,"Zero inferred a missing return/match");
        }Balanced();
    }
    Reset();{DeclarationCompletionLease lease;Require(Capture(lease),"Premature declaration return capture failed");
        Require(!lease.MarkOriginalDeclareReturned() && lease.Failed() && !lease.Complete(),"Declaration return without matching queue accepted");}Balanced();
    Reset();{DeclarationCompletionLease lease;Require(!lease.MarkOriginalRootReturned(),"Root return without capture accepted");forbidReads=true;
        Require(!Capture(lease) && !lease.Captured() && !retains && !lease.ContextAddress(),"Failed/closed receipt armed through expired context");}Balanced();
    Reset();{DeclarationCompletionLease first,second;
        Require(Capture(first) && Match(first) && first.MarkOriginalDeclareReturned() && first.MarkOriginalRootReturned(),"Old epoch fixture failed");
        Require(Capture(second,Epoch+1,Generation+1,Consumer+1,Graph+0x100) && Match(second) &&
            second.MarkOriginalDeclareReturned() && second.MarkOriginalRootReturned(),"Rearmed receipt fixture failed");
        memory.clear();forbidReads=true;
        Require(first.Complete() && second.Complete() && first.Epoch()==Epoch && first.Generation()==Generation && first.Id()==Consumer &&
            first.ExecutionGraph()==Graph && second.ExecutionGraph()==Graph+0x100 && refs==3,"Rearm changed frozen old receipt");
    }Balanced();
    Reset();{DeclarationCompletionLease lease;Require(Capture(lease),"Recapture fixture failed");
        Require(!Capture(lease,Epoch+1,Generation+1,Consumer+1,Graph+0x100) && lease.Id()==Consumer && lease.Epoch()==Epoch &&
            lease.ExecutionGraph()==Graph && lease.Group()==Group && lease.Failed() && !lease.Complete(),"Same receipt was rewritten on rearm");
    }Balanced();
    for(const bool duplicateRoot:{false,true}){Reset();
        {DeclarationCompletionLease lease;Require(Capture(lease) && Match(lease) && lease.MarkOriginalDeclareReturned() &&
            lease.MarkOriginalRootReturned() && lease.Complete(),"Duplicate return fixture failed");
            Require(!(duplicateRoot?lease.MarkOriginalRootReturned():lease.MarkOriginalDeclareReturned()) && lease.Failed() &&
                !lease.Complete(),"Duplicate original return remained complete");
        }Balanced();
    }
}
}
int main()try{
    static_assert(!std::is_copy_constructible_v<DeclarationCompletionLease> && !std::is_move_constructible_v<DeclarationCompletionLease>);
    Lifecycle();InvalidCapture();PartialReadsAndChanges();MatchFailures();MissingReturnsAndRearm();
    std::puts("PASS graph declaration completion: D0 pin, exact callers, rotation, producer/return gates, immutable rearm and balanced failures");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
