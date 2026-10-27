#include "Render/NativeDefinitionOwner.hpp"
#include <cstring>
#include <cstdio>
#include <map>
#include <stdexcept>

using namespace cvr::stereo::definition_owner;
namespace {
constexpr uint64_t Graph=0x10000,Data=0x20000,Allocator=0x900000,Epoch=37;
std::map<uint64_t,uint8_t> memory;
uint32_t reads{};uint64_t mutateAddress{};uint8_t mutateByte{};uint32_t mutateAfter{};
void Require(bool value){if(!value)throw std::runtime_error("Native definition owner assertion failed");}
template<class T>void Put(uint64_t address,const T& value){const auto* bytes=reinterpret_cast<const uint8_t*>(&value);for(size_t i=0;i<sizeof(T);++i)memory[address+i]=bytes[i];}
bool Read(uint64_t address,void* destination,size_t bytes){
    if(address>UINT64_MAX-bytes)return false;auto* out=static_cast<uint8_t*>(destination);
    for(size_t i=0;i<bytes;++i){const auto found=memory.find(address+i);if(found==memory.end())return false;out[i]=found->second;}
    if(++reads==mutateAfter)memory[mutateAddress]=mutateByte;return true;
}
void Fresh(uint32_t capacity=4,uint32_t count=2){
    memory.clear();reads=mutateAfter=0;mutateAddress=0;mutateByte=0;
    Put(Graph+0x70,NativeVector{capacity?Data:Allocator,capacity,count});
    if(capacity){for(uint32_t i=0;i<count;++i)Put(Data+uint64_t(i)*8,uint64_t(0x40000+i*0x100));Put(Data+uint64_t(capacity)*8,Allocator);}
}
void Rejected(Failure reason){MovePlan plan;Require(!plan.Capture(Graph,Read) && !plan.Ready() && plan.Rejection()==reason && !plan.MatchesBefore(Read) && !plan.VerifyDetached(Read));}
}
int main()try{
    Fresh();MovePlan plan;Require(plan.Capture(Graph,Read) && plan.Ready() && plan.OwnsAllocation());
    Require(plan.OwnerAddress()==Graph+0x70 && plan.TrailerAddress()==Data+32 && plan.SavedHeader()==NativeVector{Data,4,2} && plan.EmptyHeader()==NativeVector{Allocator,0,0});
    Require(plan.Definitions()==std::vector<uint64_t>{0x40000,0x40100} && plan.MatchesBefore(Read) && !plan.VerifyDetached(Read));
    Put(Graph+0x70,plan.EmptyHeader());Require(plan.VerifyDetached(Read) && !plan.MatchesBefore(Read));
    Require(plan.SavedHeader()==NativeVector{Data,4,2} && plan.Definitions().size()==2);
    Fresh(0,0);Require(plan.Capture(Graph,Read) && !plan.OwnsAllocation() && !plan.TrailerAddress() && plan.SavedHeader()==plan.EmptyHeader() && plan.Definitions().empty());
    Require(plan.MatchesBefore(Read) && plan.VerifyDetached(Read));
    Fresh(4,0);Require(plan.Capture(Graph,Read) && plan.OwnsAllocation() && plan.Definitions().empty());
    Fresh(MaxDefinitions,MaxDefinitions);Require(plan.Capture(Graph,Read) && plan.Definitions().size()==MaxDefinitions);
    Fresh();Require(!plan.Capture(0,Read) && plan.Rejection()==Failure::Arguments);
    Require(!plan.Capture(Graph+1,Read) && plan.Rejection()==Failure::Arguments);
    Require(!plan.Capture(UINT64_MAX-7,Read) && plan.Rejection()==Failure::Arguments);
    Require(!plan.Capture(Graph,nullptr) && plan.Rejection()==Failure::Arguments);
    Fresh();memory.erase(Graph+0x70);Rejected(Failure::Header);
    Fresh();Put(Graph+0x70,NativeVector{0,4,2});Rejected(Failure::Bounds);
    Fresh();Put(Graph+0x70,NativeVector{Data+1,4,2});Rejected(Failure::Bounds);
    Fresh();Put(Graph+0x70,NativeVector{Data,MaxDefinitions+1,0});Rejected(Failure::Bounds);
    Fresh();Put(Graph+0x70,NativeVector{Data,1,2});Rejected(Failure::Bounds);
    Fresh();Put(Graph+0x70,NativeVector{Allocator,0,1});Rejected(Failure::Bounds);
    Fresh();Put(Graph+0x70,NativeVector{UINT64_MAX-7,4,0});Rejected(Failure::Bounds);
    Fresh();Put(Data,uint64_t(0));Require(plan.Capture(Graph,Read) && plan.Definitions()==std::vector<uint64_t>{0,0x40100});
    Put(Data+8,uint64_t(0));Require(plan.Capture(Graph,Read) && plan.Definitions()==std::vector<uint64_t>{0,0});
    Fresh();Put(Data,uint64_t(0x40001));Rejected(Failure::Entries);
    Fresh();Put(Data+8,uint64_t(0x40000));Rejected(Failure::Entries);
    Fresh();Put(Data,Data+8);Rejected(Failure::Entries);
    Fresh();Put(Data,Graph+0x70);Rejected(Failure::Entries);
    Fresh();memory.erase(Data+8);Rejected(Failure::Entries);
    Fresh();Put(Data+32,uint64_t(0));Rejected(Failure::Allocator);
    Fresh();Put(Data+32,Allocator+1);Rejected(Failure::Allocator);
    Fresh();Put(Data+32,Data);Rejected(Failure::Allocator);
    Fresh();memory.erase(Data+32);Rejected(Failure::Allocator);
    Fresh();mutateAfter=3;mutateAddress=Graph+0x70+12;mutateByte=1;Rejected(Failure::Changed);
    Fresh();mutateAfter=3;mutateAddress=Data+1;mutateByte=0x80;Rejected(Failure::Changed);
    Fresh();mutateAfter=3;mutateAddress=Data+32+1;mutateByte=0x80;Rejected(Failure::Changed);
    Fresh();Require(plan.Capture(Graph,Read));Put(Graph+0x70,plan.EmptyHeader());reads=0;mutateAfter=1;mutateAddress=Graph+0x70+12;mutateByte=1;Require(!plan.VerifyDetached(Read));

    RetireState state(Epoch,true);Require(!state.ReadyToRetire() && !state.BeginRetire(Epoch));
    Require(state.AddConsumer(Epoch,10) && state.AddConsumer(Epoch,11) && state.PendingConsumers()==2);
    Require(state.VerifyDetach(Epoch,true) && !state.ReadyToRetire() && state.CloseSourceUses(Epoch));
    Require(!state.BeginRetire(Epoch) && state.CompleteConsumer(Epoch,10) && !state.ReadyToRetire());
    Require(state.CompleteConsumer(Epoch,11) && state.ReadyToRetire());
    Require(state.BeginRetire(Epoch) && state.Retiring() && !state.BeginRetire(Epoch) && !state.ReadyToRetire());
    Require(state.FinishRetire(Epoch) && state.Retired() && !state.BeginRetire(Epoch) && !state.FinishRetire(Epoch));
    Require(state.FailureReason()==RetireFailure::Repeated);
    RetireState unknown(Epoch,false);Require(!unknown.AddConsumer(Epoch,1) && !unknown.VerifyDetach(Epoch,true) && !unknown.ReadyToRetire() && unknown.FailureReason()==RetireFailure::UnaccountedPriorUse);
    RetireState badEpoch(0,true);Require(badEpoch.FailureReason()==RetireFailure::Epoch);
    RetireState mismatch(Epoch,true);Require(!mismatch.AddConsumer(Epoch+1,1) && mismatch.FailureReason()==RetireFailure::Epoch);
    RetireState duplicate(Epoch,true);Require(duplicate.AddConsumer(Epoch,1) && !duplicate.AddConsumer(Epoch,1) && !duplicate.ReadyToRetire());
    RetireState completion(Epoch,true);Require(completion.AddConsumer(Epoch,1) && completion.CompleteConsumer(Epoch,1) && !completion.CompleteConsumer(Epoch,1));
    RetireState unregistered(Epoch,true);Require(!unregistered.CompleteConsumer(Epoch,1));
    RetireState unclaimed(Epoch,true);Require(unclaimed.AddConsumer(Epoch,1));unclaimed.UnclaimedNativeMutation();Require(!unclaimed.VerifyDetach(Epoch,true) && !unclaimed.CompleteConsumer(Epoch,1) && !unclaimed.ReadyToRetire());
    RetireState notDetached(Epoch,true);Require(!notDetached.CloseSourceUses(Epoch));
    RetireState failedDetach(Epoch,true);Require(!failedDetach.VerifyDetach(Epoch,false) && !failedDetach.ReadyToRetire());
    RetireState lateUse(Epoch,true);Require(lateUse.VerifyDetach(Epoch,true) && lateUse.CloseSourceUses(Epoch) && !lateUse.AddConsumer(Epoch,1) && !lateUse.ReadyToRetire());
    RetireState noUsers(Epoch,true);Require(noUsers.VerifyDetach(Epoch,true) && noUsers.CloseSourceUses(Epoch) && noUsers.ReadyToRetire());
    Require(noUsers.BeginRetire(Epoch) && !noUsers.BeginRetire(Epoch));
    RetireState capacity(Epoch,true);for(uint64_t i=1;i<=MaxConsumers;++i)Require(capacity.AddConsumer(Epoch,i));Require(!capacity.AddConsumer(Epoch,MaxConsumers+1ull));
    std::puts("PASS native definition owner move proposal: double-read ABI, allocator sentinel, unique entries and exactly-once retirement gates; no native writes/destructors");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
