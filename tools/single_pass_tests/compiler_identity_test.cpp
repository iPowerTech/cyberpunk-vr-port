#include "Render/NativeGraphCompilerIdentity.hpp"
#include <cstdio>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <type_traits>

using namespace cvr::stereo;
using namespace cvr::stereo::graph_jobs;
namespace {
constexpr uint64_t Base=0x140000000,Context=0x10000,Argument=0x11000,Frame=0x20000,Graph=0x30000;
constexpr uint64_t Group=0x40000,ForeignGroup=0x41000,Renderer=0x50000,Wrapper=0x60000,Pool=0x70000;
constexpr uint64_t CompilerJob=0x80000,ParentHandle=0x90000,ForeignHandle=0x91000;
std::map<uint64_t,uint8_t> memory;
uint64_t reads{},failAt{},throwAt{},changeAt{},changeAddress{},changeValue{};
int32_t refs{},baselineRefs{},pending{};
uint32_t retains{},releases{},completeCalls{};
bool changeOnRetain{},expiredProducer{},forbidReads{};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>void Put(uint64_t address,T value){
    const auto* bytes=reinterpret_cast<const uint8_t*>(&value);
    for(size_t i=0;i<sizeof(T);++i)memory[address+i]=bytes[i];
}
bool Read(uint64_t address,void* output,size_t bytes){
    Require(!forbidReads,"Compiler lease reread expired native storage");
    Require(!expiredProducer || !((address>=Context && address<Context+0x40) || address==Argument),
        "Compiler lease reread expired producer context/R8 stack argument");
    ++reads;if(reads==throwAt)throw std::runtime_error("Injected compiler reader failure");
    if(reads==failAt)return false;if(reads==changeAt)Put(changeAddress,changeValue);
    auto* out=static_cast<uint8_t*>(output);
    for(size_t i=0;i<bytes;++i){const auto found=memory.find(address+i);if(found==memory.end())return false;out[i]=found->second;}
    return true;
}
void Retain(void* group){
    Require(reinterpret_cast<uintptr_t>(group)==Group && refs>0 && refs<INT32_MAX,"Invalid compiler D1 retain");
    ++refs;++retains;Put(Group+0x1c,refs);if(changeOnRetain)Put(Context+0x18,ForeignGroup);
}
void Release(void** group){
    Require(group && reinterpret_cast<uintptr_t>(*group)==Group && refs>0 && releases<retains,"Invalid compiler D1 release");
    --refs;++releases;Put(Group+0x1c,refs);*group=nullptr;
}
bool Complete(void* group){
    Require(reinterpret_cast<uintptr_t>(group)==Group,"Polled a rotated/foreign compiler group");++completeCalls;return pending==0;
}
constexpr JobLeaseApi Api{Retain,Release,Complete};
CompilerBinding Binding(){return {13,11,12,Renderer,Wrapper,Pool,Frame,Graph,Context};}
void SetPending(int32_t value){pending=value;Put(Group+0x18,pending);}
void Reset(){
    memory.clear();reads=failAt=throwAt=changeAt=changeAddress=changeValue=0;
    refs=baselineRefs=1;pending=0;retains=releases=completeCalls=0;changeOnRetain=expiredProducer=forbidReads=false;
    Put(Context+0x18,Group);Put(Context+0x38,Frame);Put(Argument,Renderer);
    Put(Renderer+0x98,Wrapper);Put(Wrapper,Pool);Put(CompilerJob+0x20,ParentHandle);Put(ParentHandle,Group);
    Put(ForeignHandle,ForeignGroup);Put<int32_t>(ForeignGroup+0x1c,1);Put<int32_t>(ForeignGroup+0x18,1);
    Put(Group+0x1c,refs);Put(Group+0x18,pending);
    // +38 is deliberately absent: this local JobQueue is not renderer payload.
}
bool Capture(CompilerIdentityLease& lease){
    return lease.CaptureBeforeDispatch(Binding(),Context,Argument,Base+CompilerDispatchReturn,Base,Read,Api);
}
bool Match(CompilerIdentityLease& lease){return lease.MatchBeforeCompiler(Pool,2,CompilerJob,Base+CompilerCallReturn,Read);}
void Balanced(){Require(refs==baselineRefs && retains==releases,"Compiler D1 pin imbalance");}
void Finish(CompilerIdentityLease& lease){
    Require(lease.MarkOriginalCompilerReturned() && lease.MarkOriginalDispatchReturned() && lease.MarkOriginalRootReturned(),
        "Compiler normal-return fixture failed");
}
void Lifecycle(){
    for(uint64_t cycle=0;cycle<2048;++cycle){Reset();
        {CompilerIdentityLease lease;Require(!lease.Captured() && !lease.Complete() && !lease.MatchesBinding(Binding()),"New compiler lease completed");
            Require(Capture(lease) && refs==2 && lease.Group()==Group && lease.Binding()==Binding(),"Valid zero-pending D1 capture rejected");
            Require(lease.ProducerContextAddress()==Context && lease.RendererArgumentAddress()==Argument,"Dispatch identity labels missing");
            Require(!lease.Complete() && !completeCalls,"Pre-enqueue D1 zero became completion");
            SetPending(1);Require(lease.MatchesCompilerParent(CompilerJob,Read) && Match(lease),"Actual compiler parent chain rejected");
            Require(lease.CompilerParentHandle()==ParentHandle && lease.CompilerJobAddress()==CompilerJob && lease.Matched(),"Compiler identity not recorded");
            Require(!lease.Complete() && !completeCalls,"Compiler match completed before original return");
            Require(lease.MarkOriginalCompilerReturned() && !lease.CompilerJobAddress() && !lease.Complete(),"Compiler return closed producer early");
            Require(lease.MarkOriginalDispatchReturned() && !lease.RendererArgumentAddress() && !lease.Complete(),"Dispatch return closed root early");
            Put(Context+0x18,ForeignGroup);Require(lease.MarkOriginalRootReturned() && !lease.ProducerContextAddress(),"Root close kept producer address");
            --refs;baselineRefs=0;SetPending(2);expiredProducer=true;
            Require(!lease.Complete(),"Compiler wrapper/child work was ignored");
            SetPending(0);memory.clear();forbidReads=true;
            Require(lease.Complete() && lease.MatchesBinding(Binding()),"Pinned D1 did not complete after producer storage expired");
        }Balanced();
    }
    Reset();{CompilerIdentityLease lease;Require(Capture(lease),"Late callback capture failed");
        Require(lease.MarkOriginalDispatchReturned() && lease.MarkOriginalRootReturned(),"Late callback producer close failed");
        expiredProducer=true;SetPending(1);
        Require(lease.MatchesCompilerParent(CompilerJob,Read) && Match(lease),"Late compiler read expired producer context");
        Require(lease.MarkOriginalCompilerReturned() && !lease.Complete(),"Compiler return ignored the still-pending wrapper token");
        SetPending(0);forbidReads=true;Require(lease.Complete(),"Late compiler aggregate did not complete");
    }Balanced();
    // A non-rotated compiler end may be the same group already pinned by D0.
    Reset();{CompilerIdentityLease lease;const uint64_t d0=Group;
        Require(Capture(lease) && lease.Group()==d0,"Legitimate D1==D0 reuse rejected");
    }Balanced();
}
void InvalidCapture(){
    for(unsigned invalid=0;invalid<23;++invalid){Reset();
        {CompilerIdentityLease lease;auto binding=Binding();uint64_t job=Context,argument=Argument,caller=Base+CompilerDispatchReturn,base=Base;
            ReadMemory reader=Read;auto api=Api;
            switch(invalid){
            case 0:binding.consumerId=0;break;case 1:binding.epoch=0;break;case 2:binding.generation=0;break;
            case 3:binding.renderer=0;break;case 4:binding.wrapper=0;break;case 5:binding.pool=0;break;
            case 6:binding.frameOwner=0;break;case 7:binding.executionGraph=0;break;case 8:binding.producerContext+=8;break;
            case 9:job=0;break;case 10:job+=1;break;case 11:argument=0;break;case 12:argument+=1;break;
            case 13:caller+=1;break;case 14:base=UINT64_MAX;break;case 15:reader=nullptr;break;
            case 16:api.retain=nullptr;break;case 17:api.release=nullptr;break;case 18:api.complete=nullptr;break;
            case 19:binding.renderer=UINT64_MAX-7;break;case 20:binding.executionGraph+=1;break;
            case 21:argument=UINT64_MAX-7;break;case 22:job=UINT64_MAX-7;break;
            }
            Require(!lease.CaptureBeforeDispatch(binding,job,argument,caller,base,reader,api) && lease.Failed() && !lease.Captured() &&
                !lease.Complete(),"Invalid compiler capture arguments accepted");
        }Balanced();
    }
    for(const auto group:{uint64_t(0),Group+1,Frame,UINT64_MAX-7}){Reset();Put(Context+0x18,group);
        {CompilerIdentityLease lease;Require(!Capture(lease) && lease.Failed(),"Invalid dispatch D1 identity accepted");}Balanced();}
    for(const auto address:{Argument,Context+0x38,Renderer+0x98,Wrapper}){Reset();
        Put(address,address==Argument?Renderer+0x100:address==Context+0x38?Frame+0x100:address==Wrapper?Pool+0x100:Wrapper+0x100);
        {CompilerIdentityLease lease;Require(!Capture(lease) && lease.Failed(),"Mismatched dispatch renderer/frame/wrapper/pool accepted");}Balanced();}
    for(const int32_t count:{0,-1,INT32_MAX}){Reset();Put(Group+0x1c,count);
        {CompilerIdentityLease lease;Require(!Capture(lease) && lease.Rejection()==CompilerIdentityFailure::References,
            "Dead or overflowing D1 retain accepted");}Balanced();}
    Reset();SetPending(-1);{CompilerIdentityLease lease;Require(!Capture(lease) && lease.Rejection()==CompilerIdentityFailure::Pending,
        "Negative D1 pending accepted");}Balanced();
    Reset();refs=baselineRefs=INT32_MAX-1;Put(Group+0x1c,refs);
    {CompilerIdentityLease lease;Require(Capture(lease) && refs==INT32_MAX,"Last safe D1 retain rejected");}Balanced();
}
void PartialReadsAndTornFields(){
    Reset();uint64_t captureReads{};{CompilerIdentityLease lease;Require(Capture(lease),"Capture read baseline failed");captureReads=reads;}Balanced();
    for(uint64_t at=1;at<=captureReads;++at)for(const bool throws:{false,true}){Reset();if(throws)throwAt=at;else failAt=at;
        {CompilerIdentityLease lease;Require(!Capture(lease) && lease.Failed() && !lease.Captured() && !lease.Complete(),
            "Partial or throwing dispatch capture accepted");}Balanced();}
    Reset();changeOnRetain=true;
    {CompilerIdentityLease lease;Require(!Capture(lease) && lease.Failed(),"Retain-time D1 rotation accepted");}Balanced();
    Require(retains==1 && releases==1,"Post-retain rollback was not exercised");
    for(const auto address:{Argument,Context+0x18,Context+0x38,Renderer+0x98,Wrapper}){Reset();
        changeAt=8;changeAddress=address;
        changeValue=address==Argument?Renderer+0x100:address==Context+0x18?ForeignGroup:
            address==Context+0x38?Frame+0x100:address==Wrapper?Pool+0x100:Wrapper+0x100;
        {CompilerIdentityLease lease;Require(!Capture(lease) && lease.Failed(),"Torn dispatch identity accepted");}Balanced();
    }
    Reset();uint64_t matchReads{};
    {CompilerIdentityLease lease;Require(Capture(lease),"Match read baseline capture failed");SetPending(1);reads=0;
        Require(Match(lease),"Compiler match read baseline failed");matchReads=reads;
    }Balanced();
    for(uint64_t at=1;at<=matchReads;++at)for(const bool throws:{false,true}){Reset();
        {CompilerIdentityLease lease;Require(Capture(lease),"Failed match capture fixture failed");SetPending(1);reads=0;
            if(throws)throwAt=at;else failAt=at;
            Require(!Match(lease) && lease.Failed() && !lease.Complete(),"Partial or throwing compiler match accepted");
            Require(!lease.MarkOriginalRootReturned() && !lease.ProducerContextAddress() && !lease.RendererArgumentAddress(),
                "Failed match retained producer stack addresses");
        }Balanced();
    }
    for(const auto address:{CompilerJob+0x20,ParentHandle,Renderer+0x98,Wrapper}){Reset();
        {CompilerIdentityLease lease;Require(Capture(lease),"Torn match capture failed");SetPending(1);
            changeAt=reads+7;changeAddress=address;
            changeValue=address==CompilerJob+0x20?ForeignHandle:address==ParentHandle?ForeignGroup:
                address==Wrapper?Pool+0x100:Wrapper+0x100;
            Require(!Match(lease) && lease.Failed() && !lease.Complete(),"Torn compiler parent/renderer identity accepted");
        }Balanced();
    }
}
void ForeignAndInvalidCompiler(){
    Reset();{CompilerIdentityLease lease;Require(Capture(lease),"Foreign filter capture failed");SetPending(1);
        Put(CompilerJob+0x20,ForeignHandle);
        Require(!lease.MatchesCompilerParent(CompilerJob,Read) && !lease.Failed(),"Pool-only foreign compiler poisoned or matched the receipt");
        Put(CompilerJob+0x20,ParentHandle);
        Require(lease.MatchesCompilerParent(CompilerJob,Read) && Match(lease),"Actual compiler could not match after an unrelated same-pool callback");
        Finish(lease);SetPending(0);Require(lease.Complete(),"Filtered foreign callback caused false incomplete receipt");
    }Balanced();
    for(unsigned invalid=0;invalid<13;++invalid){Reset();
        {CompilerIdentityLease lease;Require(Capture(lease),"Invalid compiler fixture capture failed");SetPending(1);
            uint64_t pool=Pool,job=CompilerJob,caller=Base+CompilerCallReturn;uint8_t mode=2;ReadMemory reader=Read;
            switch(invalid){
            case 0:Put(CompilerJob+0x20,ForeignHandle);break;case 1:Put(ParentHandle,ForeignGroup);break;
            case 2:job=0;break;case 3:job+=1;break;case 4:pool+=8;break;case 5:mode=1;break;
            case 6:caller+=1;break;case 7:reader=nullptr;break;case 8:Put(Renderer+0x98,Wrapper+8);break;
            case 9:Put(Wrapper,Pool+8);break;case 10:Put(Group+0x1c,int32_t(0));break;
            case 11:SetPending(0);break;case 12:Put(CompilerJob+0x20,ParentHandle+1);break;
            }
            Require(!lease.MatchBeforeCompiler(pool,mode,job,caller,reader) && lease.Failed() && !lease.Complete(),
                "Foreign/invalid compiler callback accepted");
        }Balanced();
    }
    Reset();{CompilerIdentityLease lease;Require(Capture(lease),"Early-zero compiler fixture capture failed");
        Require(!lease.MatchesCompilerParent(CompilerJob,Read) && !Match(lease) &&
            lease.Rejection()==CompilerIdentityFailure::NoCallbackToken && !completeCalls,
            "Pre-enqueue pending zero claimed a compiler callback or completion");
    }Balanced();
    Reset();{CompilerIdentityLease lease;Require(Capture(lease),"Binding mismatch capture failed");
        for(unsigned field=0;field<9;++field){auto other=Binding();
            switch(field){case 0:++other.consumerId;break;case 1:++other.epoch;break;case 2:++other.generation;break;
            case 3:other.renderer+=8;break;case 4:other.wrapper+=8;break;case 5:other.pool+=8;break;
            case 6:other.frameOwner+=8;break;case 7:other.executionGraph+=8;break;case 8:other.producerContext+=8;break;}
            Require(!lease.MatchesBinding(other) && !lease.Failed(),"Mismatched frozen consumer/renderer/owned graph provenance matched");
        }
        Require(lease.Binding()==Binding(),"Provenance comparisons changed the frozen binding");
    }Balanced();
}
void ReturnsAndRearm(){
    for(unsigned missing=0;missing<4;++missing){Reset();
        {CompilerIdentityLease lease;Require(Capture(lease),"Missing-return capture failed");SetPending(1);
            if(missing!=0)Require(Match(lease),"Missing-return match fixture failed");
            if(missing!=0 && missing!=1)Require(lease.MarkOriginalCompilerReturned(),"Missing-return compiler fixture failed");
            if(missing!=2)Require(lease.MarkOriginalDispatchReturned(),"Missing-return dispatch fixture failed");
            if(missing!=3)Require(lease.MarkOriginalRootReturned(),"Missing-return root fixture failed");
            SetPending(0);Require(!lease.Complete() && !completeCalls,"Terminal zero inferred a missing compiler match/return/producer close");
        }Balanced();
    }
    for(unsigned duplicate=0;duplicate<4;++duplicate){Reset();
        {CompilerIdentityLease lease;Require(Capture(lease),"Duplicate fixture capture failed");SetPending(1);
            Require(Match(lease),"Duplicate fixture compiler match failed");Finish(lease);SetPending(0);
            Require(lease.Complete(),"Duplicate baseline did not complete");
            const bool accepted=duplicate==0?Match(lease):duplicate==1?lease.MarkOriginalCompilerReturned():
                duplicate==2?lease.MarkOriginalDispatchReturned():lease.MarkOriginalRootReturned();
            Require(!accepted && lease.Failed() && !lease.Complete(),"Duplicate callback/normal return remained complete");
        }Balanced();
    }
    Reset();{CompilerIdentityLease lease;Require(Capture(lease),"Premature compiler return capture failed");
        Require(!lease.MarkOriginalCompilerReturned() && lease.Failed() && !lease.Complete(),"Compiler return without actual match accepted");
    }Balanced();
    Reset();{CompilerIdentityLease lease;Require(!lease.MarkOriginalRootReturned(),"Root return before capture accepted");forbidReads=true;
        Require(!Capture(lease) && !lease.Captured() && !retains,"Closed failed receipt rearmed through expired context");
    }Balanced();
    Reset();{CompilerIdentityLease lease;Require(Capture(lease),"Rearm capture failed");auto other=Binding();other.executionGraph+=0x100;
        Require(!lease.CaptureBeforeDispatch(other,Context,Argument,Base+CompilerDispatchReturn,Base,Read,Api) &&
            lease.Failed() && lease.Binding()==Binding() && lease.Group()==Group,"Rearm rewrote the frozen owned graph receipt");
        lease.MarkOriginalRootReturned();forbidReads=true;
        Require(!lease.Complete() && !lease.ProducerContextAddress() && !lease.RendererArgumentAddress(),"Failed rearm left producer storage usable");
    }Balanced();
}
}

int main()try{
    static_assert(!std::is_copy_constructible_v<CompilerIdentityLease> && !std::is_move_constructible_v<CompilerIdentityLease>);
    Lifecycle();InvalidCapture();PartialReadsAndTornFields();ForeignAndInvalidCompiler();ReturnsAndRearm();
    std::puts("PASS graph compiler identity: pinned D1, exact producer/R8 renderer and local-job parent, same-pool foreign rejection, late callbacks, terminal gates, immutable provenance and balanced failures");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
