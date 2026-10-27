#include "Render/NativeRetirementDeferral.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include <type_traits>

using namespace cvr::stereo::retirement_deferral;
namespace {bool failNextAllocation{};}
void* operator new(size_t bytes){
    if(failNextAllocation){failNextAllocation=false;throw std::bad_alloc();}
    if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();
}
void operator delete(void* memory)noexcept{std::free(memory);}
void operator delete(void* memory,size_t)noexcept{std::free(memory);}
namespace {
constexpr ReservationFacts Reservation{true,true,true};
constexpr NativeIntentFacts Intent{true,true,true,true};
constexpr AllocatorProof Allocator{{0x10000,1},true,true};
constexpr ReservationAllocatorObservation Observed{Allocator.identity,true};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
HeaderIdentity Header(uint64_t generation=1,uint64_t owner=0x20000){return {owner,generation,generation,{0x100000+owner*0x100+generation*0x1000,8,2},{Allocator.identity.address,0,0}};}
ExtraReaderIdentity Reader(uint64_t lease=1,uint64_t consumer=1){return {lease,consumer,11,12,0x40000,0x50000+consumer*0x100};}
OwnedGraphCompletionProof Completed(ExtraReaderIdentity reader){return {reader,true,true,true,true,true,true,true,true,true};}
UnescapedCancellationProof Unescaped(ExtraReaderIdentity reader){return {reader,true,true,true};}
State& Reserve(Ledger& ledger,const HeaderIdentity& header=Header()){
    auto* state=ledger.Reserve(header,Reservation,Observed);Require(state,"Valid pre-reservation rejected");return *state;
}
void Transfer(State& state,IntentKind kind=IntentKind::Reset){const auto header=state.FrozenHeader();
    Require(state.ObserveNativeIntent(kind,header,Intent,Allocator),"Exact native intent rejected");
    Require(state.RecordHeaderTransfer({header,header.savedNative16B,header.emptyAllocator16B,true}),"Exact header transfer rejected");
}
void DestroyAndRetire(Ledger& ledger,State& state){const auto header=state.FrozenHeader();
    Require(state.ReadyToDestroy(Allocator),"Completed exact intent not destroyable");
    auto* saved=state.BeginDestroy(Allocator);Require(saved && *saved==header.savedNative16B && state.DestroyAttemptConsumed(),"Destruction attempt/storage not sealed");
    // Model the caller's original986E74 touching only the stable saved header.
    *saved=header.emptyAllocator16B;
    Require(state.FinishDestroy(true) && state.CanRetireMetadata(),"Normal native destruction not recorded");
    Require(state.FrozenHeader()==header && ledger.RetireMetadata(header.ownerHeader,header.generation,header.bornEpoch),"Native call changed immutable identity or metadata retirement failed");
}
void Lifetimes(){
    Ledger ledger;
    for(uint64_t generation=1;generation<=12544;++generation){const auto header=Header(generation);auto& state=Reserve(ledger,header);
        const auto first=Reader(1,generation*2),second=Reader(2,generation*2+1);
        Require(state.AdmitExtraReader(first) && state.AdmitExtraReader(second),"Two extra readers not admitted");
        Transfer(state,(generation&1)?IntentKind::Reset:IntentKind::Destroy);
        Require(!state.AcquisitionsOpen() && state.PendingReaders()==2 && !state.ReadyToDestroy(Allocator),"Intent did not close acquisitions/preserve readers");
        Require(!state.AdmitExtraReader(Reader(3,generation*2+2)) && state.Rejection()==Failure::Closed && !state.Quarantined(),"Closed source admitted new extra reader");
        Require(state.CompleteExtraReader(Completed(second)) && !state.ReadyToDestroy(Allocator),"One reader freed the other reader");
        Require(state.CompleteExtraReader(Completed(first)) && state.PendingReaders()==0 && state.CompletedReaders()==2,"Actual completion did not release both reader slots");
        DestroyAndRetire(ledger,state);Require(!ledger.Size() && ledger.OwnerCount()==1,"Successful cycles grew completed history");
        Require(!ledger.Reserve(header,Reservation,Observed) && ledger.Rejection()==Failure::GenerationReplay,"Retired native generation replayed");
    }
}
void ReservationFailuresAndBounds(){
    {Ledger allocation;failNextAllocation=true;
        Require(!allocation.Reserve(Header(),Reservation,Observed) && allocation.Rejection()==Failure::Allocation &&
            !allocation.Size() && !allocation.OwnerCount(),"Allocation failure committed partial pre-reservation");
        auto& state=Reserve(allocation);const auto reader=Reader();failNextAllocation=true;
        const bool admitted=state.AdmitExtraReader(reader);const bool allocationUnused=failNextAllocation;failNextAllocation=false;
        Require(admitted && allocationUnused,"Extra admission allocated storage after reservation");
        Transfer(state);Require(state.CompleteExtraReader(Completed(reader)),"Allocation retry reader did not complete");DestroyAndRetire(allocation,state);}
    Ledger ledger;
    for(unsigned missing=0;missing<4;++missing){auto facts=Reservation;auto allocator=Observed;
        if(missing==0)facts.retirementPathAudited=false;if(missing==1)facts.layoutAudited=false;if(missing==2)facts.destructorTreeSafe=false;
        if(missing==3)allocator.infrastructureMatches=false;
        Require(!ledger.Reserve(Header(),facts,allocator) && !ledger.Size() && !ledger.OwnerCount(),"Failed pre-reservation created reader/storage authority");
    }
    for(unsigned invalid=0;invalid<9;++invalid){auto header=Header(2);
        if(invalid==0)header.ownerHeader=0;if(invalid==1)++header.ownerHeader;if(invalid==2)header.generation=0;
        if(invalid==3)header.savedNative16B.count=9;if(invalid==4)header.emptyAllocator16B.count=1;
        if(invalid==5)header.savedNative16B.data=UINT64_MAX-7;if(invalid==6)header.savedNative16B.data=header.ownerHeader;
        if(invalid==7)header.savedNative16B.data=header.emptyAllocator16B.data-8;if(invalid==8)header.ownerHeader=header.emptyAllocator16B.data;
        Require(!ledger.Reserve(header,Reservation,Observed) && !ledger.Size(),"Malformed frozen header admitted");
    }
    {Ledger owners;for(size_t i=0;i<MaxOwners;++i){auto& state=Reserve(owners,Header(1,0x20000+i*0x100));Transfer(state);DestroyAndRetire(owners,state);}
        Require(!owners.Size() && owners.OwnerCount()==MaxOwners && !owners.Reserve(Header(1,0x80000),Reservation,Observed) &&
            owners.Rejection()==Failure::Capacity,"Bounded owner watermarks overflowed/evicted");}
    {Ledger states;for(size_t i=0;i<MaxStates;++i){auto& state=Reserve(states,Header(i+1));Transfer(state);}
        Require(states.Size()==MaxStates && !states.Reserve(Header(MaxStates+1),Reservation,Observed) &&
            states.Rejection()==Failure::Capacity,"Detached storage budget overflowed/evicted");}
    {Ledger readers;auto& state=Reserve(readers);
        for(size_t i=0;i<MaxExtraReaders;++i)Require(state.AdmitExtraReader(Reader(i+1,i+1)),"Reserved fixed reader slots unavailable");
        Require(!state.AdmitExtraReader(Reader(MaxExtraReaders+1,MaxExtraReaders+1)) && state.PendingReaders()==MaxExtraReaders,
            "Extra reader budget overflowed/dropped prior readers");}
    {Ledger retry;auto& retained=Reserve(retry);Require(retained.AdmitExtraReader(Reader()),"Allocation retry retained reader failed");Transfer(retained);
        failNextAllocation=true;Require(!retry.Reserve(Header(2),Reservation,Observed) && retry.Rejection()==Failure::Allocation &&
            retry.Size()==1 && retained.PendingReaders()==1,"Reservation allocation failure dropped detached reader");
        auto& next=Reserve(retry,Header(2));Require(next.AcquisitionsOpen() && retained.PendingReaders()==1,"Same identity allocation retry failed");}
}
void SavedAllocationAliasing(){
    for(unsigned alias=0;alias<5;++alias){Ledger ledger;auto& retained=Reserve(ledger);Require(retained.AdmitExtraReader(Reader()),"Overlap reader fixture failed");Transfer(retained);
        if(alias>=3){Require(retained.CompleteExtraReader(Completed(Reader())) && retained.BeginDestroy(Allocator),"Overlap destroy fixture failed");
            if(alias==4)Require(!retained.FinishDestroy(false),"Failed native destructor recorded success");}
        auto header=Header(2,alias==1?0x21000:0x20000);
        header.savedNative16B.data=retained.FrozenHeader().savedNative16B.data+(alias==2?8:0);
        Require(!ledger.Reserve(header,Reservation,Observed) && ledger.Rejection()==Failure::StorageOverlap && retained.Quarantined() &&
            ledger.Size()==1 && !retained.BeginDestroy(Allocator),"Live/in-flight/failed saved allocation alias admitted");
    }
    {Ledger ledger;auto& old=Reserve(ledger);Transfer(old);Require(old.BeginDestroy(Allocator) && old.FinishDestroy(true),"Successful allocation reuse fixture failed");
        auto header=Header(2);header.savedNative16B.data=old.FrozenHeader().savedNative16B.data;
        auto& next=Reserve(ledger,header);Require(next.AcquisitionsOpen(),"Normally destroyed allocation could not be reused");}
    for(const bool emptyOld:{false,true}){Ledger ledger;auto header=Header();if(emptyOld)header.savedNative16B=header.emptyAllocator16B;
        auto& retained=Reserve(ledger,header);Require(retained.AdmitExtraReader(Reader()),"Reverse header overlap reader fixture failed");
        auto incoming=Header(1,0x21000);incoming.savedNative16B.data=header.ownerHeader;
        Require(!ledger.Reserve(incoming,Reservation,Observed) && ledger.Rejection()==Failure::StorageOverlap && retained.Quarantined() &&
            retained.PendingReaders()==1,"Incoming allocation covered attached source header");}
    for(const bool emptyOld:{false,true}){Ledger ledger;auto header=Header();if(emptyOld)header.savedNative16B=header.emptyAllocator16B;
        auto& retained=Reserve(ledger,header);auto incoming=Header(1,0x21000);auto allocator=Allocator;
        allocator.identity={0x90000,2};incoming.emptyAllocator16B.data=allocator.identity.address;
        incoming.savedNative16B.data=header.emptyAllocator16B.data;
        Require(!ledger.Reserve(incoming,Reservation,{allocator.identity,allocator.infrastructureMatches}) && ledger.Rejection()==Failure::StorageOverlap && retained.Quarantined(),
            "Incoming allocation covered retained allocator infrastructure");}
}
void CompletionAndClosure(){
    for(unsigned missing=0;missing<9;++missing){Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();
        Require(state.AdmitExtraReader(reader),"Incomplete proof fixture reader failed");Transfer(state);auto proof=Completed(reader);
        if(missing==0)proof.ownedIdentityVerified=false;if(missing==1)proof.producerClosed=false;if(missing==2)proof.originalProducerReturned=false;
        if(missing==3)proof.declarationQueued=false;if(missing==4)proof.originalDeclarationReturned=false;if(missing==5)proof.declarationComplete=false;
        if(missing==6)proof.executionQueued=false;if(missing==7)proof.originalExecutionReturned=false;if(missing==8)proof.executionAggregateComplete=false;
        Require(!state.CompleteExtraReader(proof) && state.PendingReaders()==1 && !state.ReadyToDestroy(Allocator) &&
            !state.BeginDestroy(Allocator) && !state.DestroyAttemptConsumed(),"Early/zero completion consumed a reader/destruction attempt");
        Require(state.CompleteExtraReader(Completed(reader)),"Actual completion after incomplete observation rejected");DestroyAndRetire(ledger,state);
    }
    {Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();Require(state.AdmitExtraReader(reader),"Identity fixture reader failed");Transfer(state);
        auto proof=Completed(reader);++proof.reader.executionGraph;
        Require(!state.CompleteExtraReader(proof) && state.Quarantined() && state.PendingReaders()==1 && !state.ReadyToDestroy(Allocator),"Foreign graph released source definitions");}
    {Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();Require(state.AdmitExtraReader(reader),"Unsupported fixture reader failed");
        state.UnsupportedExtraCompletion(reader.leaseId);Require(state.Quarantined() && state.PendingReaders()==1,"Missing completion dropped reader");
        Transfer(state);Require(state.ExactHeaderDetached() && state.Quarantined() && state.CompleteExtraReader(Completed(reader)) &&
            !state.ReadyToDestroy(Allocator) && !state.BeginDestroy(Allocator),"Exact preservation cleared quarantine");}
    {Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();Require(state.AdmitExtraReader(reader) && state.CompleteExtraReader(Completed(reader)),"Pre-intent completion fixture failed");
        Require(!state.ReadyToDestroy(Allocator) && !state.BeginDestroy(Allocator) && !state.DestroyAttemptConsumed(),"Reader completion synthesized native retirement intent");
        Transfer(state);DestroyAndRetire(ledger,state);}
    {Ledger ledger;auto& state=Reserve(ledger);Transfer(state);DestroyAndRetire(ledger,state);}
}
void IntentsAndTransferFailures(){
    for(unsigned bad=0;bad<5;++bad){Ledger ledger;auto& state=Reserve(ledger);Require(state.AdmitExtraReader(Reader()),"Intent failure reader fixture failed");
        auto facts=Intent;auto header=state.FrozenHeader();IntentKind kind=IntentKind::Reset;
        if(bad==0)facts.exactNativeRetirementPoint=false;if(bad==1)facts.uniqueWriter=false;if(bad==2)facts.destructorTreeSafe=false;
        if(bad==3)++header.savedNative16B.count;if(bad==4)kind=IntentKind::None;
        Require(!state.ObserveNativeIntent(kind,header,facts,Allocator) && state.Quarantined() && !state.AcquisitionsOpen() &&
            !state.ReadyToDestroy(Allocator) && !state.AdmitExtraReader(Reader(2,2)),"Unsupported native intent did not close/quarantine");}
    for(unsigned bad=0;bad<4;++bad){Ledger ledger;auto& state=Reserve(ledger);Require(state.AdmitExtraReader(Reader()),"Transfer failure reader fixture failed");
        const auto header=state.FrozenHeader();Require(state.ObserveNativeIntent(IntentKind::Destroy,header,Intent,Allocator),"Transfer fixture intent failed");
        HeaderTransferProof proof{header,header.savedNative16B,header.emptyAllocator16B,true};
        if(bad==0)proof.successfulExactTransfer=false;if(bad==1)++proof.savedCopy16B.count;if(bad==2)++proof.sourceAfter16B.capacity;
        if(bad==3)++proof.sourceBefore.bornEpoch;
        Require(!state.RecordHeaderTransfer(proof) && state.Quarantined() && !state.ExactHeaderDetached() && state.PendingReaders()==1,
            "Failed transfer after admission permitted native fallback");
        Require(!state.RecordHeaderTransfer({header,header.savedNative16B,header.emptyAllocator16B,true}) &&
            !state.BeginDestroy(Allocator),"Failed/partial transfer retried into destruction");}
    {Ledger ledger;auto& state=Reserve(ledger);Transfer(state);
        Require(!state.ObserveNativeIntent(IntentKind::Destroy,state.FrozenHeader(),Intent,Allocator) &&
            state.NativeIntentKind()==IntentKind::Reset && state.Quarantined(),"Nested same-generation intent replaced/reset frozen intent");}
    {Ledger ledger;auto& first=Reserve(ledger);auto& nested=Reserve(ledger,Header(1,0x21000));
        Transfer(first,IntentKind::Reset);Transfer(nested,IntentKind::Destroy);
        DestroyAndRetire(ledger,nested);DestroyAndRetire(ledger,first);Require(!ledger.Size(),"Independent nested intents interfered");}
    {Ledger ledger;auto& state=Reserve(ledger);Transfer(state);const auto header=state.FrozenHeader();
        Require(!state.RecordHeaderTransfer({header,header.savedNative16B,header.emptyAllocator16B,true}) && state.Quarantined(),"Header transferred twice");}
}
void AllocatorAndShutdown(){
    {Ledger metadata;auto* state=metadata.Reserve(Header(),Reservation,Observed);auto noNativeUse=Allocator;noNativeUse.alive=false;
        Require(state && !state->NativeIntentSeen() && !state->ReadyToDestroy(noNativeUse),"Metadata reservation claimed native-use liveness");
        Require(!state->ObserveNativeIntent(IntentKind::Reset,Header(),Intent,noNativeUse) && state->Quarantined(),
            "Native retirement intent accepted metadata observation as liveness");}
    {Ledger metadata;auto& state=Reserve(metadata);Transfer(state);auto noNativeUse=Allocator;noNativeUse.alive=false;
        Require(!state.BeginDestroy(noNativeUse) && !state.DestroyAttemptConsumed() && state.Quarantined(),
            "Saved native destruction accepted metadata-only allocator evidence");}
    for(unsigned changed=0;changed<4;++changed){Ledger ledger;auto& state=Reserve(ledger);Require(state.AdmitExtraReader(Reader()),"Allocator fixture reader failed");Transfer(state);
        auto allocator=Allocator;if(changed==0)allocator.identity.address+=0x100;if(changed==1)++allocator.identity.generation;
        if(changed==2)allocator.alive=false;if(changed==3)allocator.infrastructureMatches=false;
        Require(!state.ReadyToDestroy(allocator) && !state.ObserveAllocator(allocator) && state.Quarantined(),"Changed/dead allocator accepted");
        Require(state.CompleteExtraReader(Completed(Reader())) && !state.ReadyToDestroy(Allocator) && !state.BeginDestroy(Allocator),"Allocator resurrection cleared quarantine");}
    {Ledger ledger;auto& state=Reserve(ledger);Require(state.AdmitExtraReader(Reader()),"Shutdown fixture reader failed");Transfer(state);
        ledger.Shutdown();Require(state.Quarantined() && state.PendingReaders()==1 && ledger.Size()==1 &&
            !ledger.Reserve(Header(2),Reservation,Observed),"Shutdown dropped/rearmed detached storage");
        Require(state.CompleteExtraReader(Completed(Reader())) && !state.BeginDestroy(Allocator) &&
            !ledger.RetireMetadata(0x20000,1,1),"Shutdown released saved native header");}
}
void ExactlyOnceCancellationAndAba(){
    {Ledger ledger;auto& state=Reserve(ledger);Transfer(state);Require(state.BeginDestroy(Allocator),"Exactly-once attempt failed");
        Require(!state.BeginDestroy(Allocator) && state.DestroyAttemptConsumed() && state.Quarantined() && !state.FinishDestroy(true) &&
            !ledger.RetireMetadata(0x20000,1,1),"Repeated attempt allowed retry/release");}
    {Ledger ledger;auto& state=Reserve(ledger);Transfer(state);Require(state.BeginDestroy(Allocator),"Failed native destruction fixture failed");
        Require(!state.FinishDestroy(false) && !state.BeginDestroy(Allocator) && state.Quarantined(),"Native destruction failure retried");}
    {Ledger ledger;auto& state=Reserve(ledger);Transfer(state);Require(state.BeginDestroy(Allocator),"Closing scope in-flight fixture failed");
        ledger.Shutdown();Require(!state.FinishDestroy(true) && state.DestroyReturned() && state.DestroyAttemptConsumed() &&
            !state.BeginDestroy(Allocator) && !state.CanRetireMetadata(),"Closing scope retried/interrupted an already-started native destructor");}
    {Ledger ledger;auto& state=Reserve(ledger);Require(!state.FinishDestroy(true) && state.Quarantined(),"Return without native attempt accepted");}
    {Ledger ledger;auto& first=Reserve(ledger);const auto reader=Reader();Require(first.AdmitExtraReader(reader),"Cancel fixture reader failed");
        ledger.CancelSelection();Require(!first.Selected() && !first.AcquisitionsOpen() && first.PendingReaders()==1 && !first.CanRetireMetadata(),"Cancellation dropped attached reader/storage");
        Transfer(first);auto& next=Reserve(ledger,Header(2));Require(next.AcquisitionsOpen() && first.PendingReaders()==1 && ledger.Size()==2,"Rearm lost detached generation");
        Require(first.CompleteExtraReader(Completed(reader)),"Unselected reader lost completion binding");DestroyAndRetire(ledger,first);
        Transfer(next);DestroyAndRetire(ledger,next);
        Require(!ledger.Reserve(Header(1),Reservation,Observed) && !ledger.Reserve(Header(2),Reservation,Observed),"Owner watermark admitted allocator/header ABA");}
    {Ledger ledger;auto& source=Reserve(ledger);Require(source.AdmitExtraReader(Reader()),"Open generation fixture failed");
        Require(!ledger.Reserve(Header(2),Reservation,Observed) && ledger.Rejection()==Failure::UnresolvedSource && source.PendingReaders()==1,
            "New generation bypassed unresolved attached source");}
    {Ledger ledger;auto header=Header();auto& state=Reserve(ledger,header);header.savedNative16B={};
        Require(state.FrozenHeader()==Header() && state.SavedHeader()==Header().savedNative16B,"External header mutation changed frozen storage");
        Require(state.AdmitExtraReader(Reader(1,1)) && !state.AdmitExtraReader(Reader(1,2)) && !state.AdmitExtraReader(Reader(2,1)),"Reader sequence/duplicate consumer admitted");}
    {Ledger ledger;auto header=Header();header.generation=header.bornEpoch=UINT64_MAX;auto& state=Reserve(ledger,header);Transfer(state);DestroyAndRetire(ledger,state);
        Require(!ledger.Reserve(header,Reservation,Observed) && !ledger.Reserve(Header(),Reservation,Observed),"Owner generation watermark wrapped/replayed");}
    {Ledger ledger;auto& state=Reserve(ledger);auto reader=Reader(UINT64_MAX,1);
        Require(state.AdmitExtraReader(reader) && state.CompleteExtraReader(Completed(reader)) &&
            !state.AdmitExtraReader(reader) && !state.AdmitExtraReader(Reader()),"Reader lease watermark wrapped/replayed");}
    {Ledger ledger;auto& state=Reserve(ledger);
        for(uint64_t i=1;i<=MaxExtraReaders*2;++i){const auto reader=Reader(i,i);
            Require(state.AdmitExtraReader(reader) && state.CompleteExtraReader(Completed(reader)),"Fixed reader slot could not refill");}
        const auto current=Reader(MaxExtraReaders*2+1,MaxExtraReaders*2+1);Require(state.AdmitExtraReader(current),"Reused slot fixture failed");
        Require(!state.CompleteExtraReader(Completed(Reader(1,1))) && state.PendingReaders()==1,"Old completion released a reused reader slot");
        auto wrong=Completed(current);++wrong.reader.epoch;
        Require(!state.CompleteExtraReader(wrong) && state.Quarantined() && state.PendingReaders()==1,"Wrong identity released a reused slot");}
}
void UnescapedCancellationAndAbandonment(){
    Ledger cycles;
    for(uint64_t i=1;i<=12544;++i){auto& state=Reserve(cycles);const auto reader=Reader(i,i);
        Require(state.AdmitExtraReader(reader),"Cancellation cycle admission failed");cycles.CancelSelection();
        Require(state.PendingReaders()==1 && !state.CanAbandonUnusedReservation(),"Generic selection cancel released reader");
        Require(state.CancelUnescapedExtraReader(Unescaped(reader)) && !state.PendingReaders() && state.CanceledReaders()==1 &&
            !state.CompletedReaders() && !state.NativeIntentSeen() && !state.ReadyToDestroy(Allocator),"Verified unescaped cancellation became completion/destruction");
        Require(cycles.AbandonUnusedReservation(0x20000,1,1) && !cycles.Size() && cycles.OwnerCount()==1,"Canceled attempts exhausted reserved storage");
    }
    {auto& retried=Reserve(cycles);Require(!retried.AdmitExtraReader(Reader(1,1)) &&
        retried.AdmitExtraReader(Reader(12545,12545)),"Unused retry lost reader nonce watermark");}
    {Ledger unused;auto& state=Reserve(unused);Require(state.CanAbandonUnusedReservation() &&
        unused.AbandonUnusedReservation(0x20000,1,1) && !unused.Size(),"Reader-free reservation could not abandon");
        auto changed=Header();++changed.savedNative16B.count;
        Require(!unused.Reserve(changed,Reservation,Observed) && unused.Rejection()==Failure::GenerationReplay,"Unused retry changed frozen header bytes");
        auto changedAllocator=Allocator;++changedAllocator.identity.generation;
        Require(!unused.Reserve(Header(),Reservation,{changedAllocator.identity,changedAllocator.infrastructureMatches}),"Unused retry changed frozen allocator identity");
        Require(unused.Reserve(Header(),Reservation,Observed),"Exact reader-free retry rejected");}
    for(unsigned denied=0;denied<7;++denied){Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();
        Require(state.AdmitExtraReader(reader),"Denied cancellation fixture failed");auto proof=Unescaped(reader);
        if(denied==0){Require(state.MarkExtraReaderEscaped(reader),"Escape fixture did not latch");}
        if(denied==1)proof.nativeOriginalNeverInvoked=false;if(denied==2)proof.verifiedOriginalSlotRestored=false;
        if(denied==3)proof.noNativeEscape=false;if(denied==4)state.UnsupportedExtraCompletion(reader.leaseId);
        if(denied==5)Transfer(state);if(denied==6)ledger.Shutdown();
        Require(!state.CancelUnescapedExtraReader(proof) && state.PendingReaders()==1 && !state.CanceledReaders() &&
            !ledger.AbandonUnusedReservation(0x20000,1,1),"Escaped/uncertain/retiring/shutdown reader canceled");
        if(denied==0 || denied==1 || denied==3)Require(!state.CancelUnescapedExtraReader(Unescaped(reader)) && state.PendingReaders()==1,
            "Known native escape was rewritten into unescaped cancellation");
    }
    {Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();Require(state.AdmitExtraReader(reader),"Intent close cancellation fixture failed");
        Require(state.ObserveNativeIntent(IntentKind::Reset,state.FrozenHeader(),Intent,Allocator) &&
            !state.CancelUnescapedExtraReader(Unescaped(reader)),"Native intent without transfer gave cancellation authority");}
    {Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();Require(state.AdmitExtraReader(reader),"Canceled/completed counters fixture failed");
        Require(state.CompleteExtraReader(Completed(reader)) && !ledger.AbandonUnusedReservation(0x20000,1,1),"Completed escaped use became unused reservation");}
}
void ExactPendingReaderQuery(){
    {Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();const State& observed=state;
        Require(!observed.HasPendingExtraReader(reader),"Unadmitted reader appeared active");
        Require(state.AdmitExtraReader(reader) && observed.HasPendingExtraReader(reader),"Admitted exact reader not found");
        const auto rejection=state.Rejection();const auto phase=state.CurrentPhase();
        const std::array<uint64_t ExtraReaderIdentity::*,6> fields{&ExtraReaderIdentity::leaseId,&ExtraReaderIdentity::consumerId,
            &ExtraReaderIdentity::epoch,&ExtraReaderIdentity::pairGeneration,&ExtraReaderIdentity::sourceGraph,&ExtraReaderIdentity::executionGraph};
        for(const auto field:fields){auto other=reader;++(other.*field);
            Require(!observed.HasPendingExtraReader(other),"Partial reader identity matched");}
        Require(state.Rejection()==rejection && state.CurrentPhase()==phase && state.PendingReaders()==1,
            "Read-only pending query mutated retirement state");
        Require(state.MarkExtraReaderEscaped(reader) && observed.HasPendingExtraReader(reader),"Escape lost protected reader");
        ledger.CancelSelection();Require(observed.HasPendingExtraReader(reader),"Selection cancellation removed active protection");
        const auto header=state.FrozenHeader();
        Require(state.ObserveNativeIntent(IntentKind::Reset,header,Intent,Allocator) && observed.HasPendingExtraReader(reader),
            "Closed native intent lost protected reader");
        Require(state.RecordHeaderTransfer({header,header.savedNative16B,header.emptyAllocator16B,true}) && observed.HasPendingExtraReader(reader),
            "Detached saved owner lost protected reader");
        Require(!state.BeginDestroy(Allocator) && !state.DestroyAttemptConsumed() && observed.HasPendingExtraReader(reader),
            "Incomplete destroy probe consumed pending query permission");
        Require(state.CompleteExtraReader(Completed(reader)) && !observed.HasPendingExtraReader(reader),"Completed reader still active");
        Require(state.BeginDestroy(Allocator) && !observed.HasPendingExtraReader(reader),"Destroy attempt revived reader");
        Require(state.FinishDestroy(true) && !observed.HasPendingExtraReader(reader),"Destroyed owner revived reader");
    }
    {Ledger ledger;auto& state=Reserve(ledger);const auto old=Reader();
        Require(state.AdmitExtraReader(old) && state.CancelUnescapedExtraReader(Unescaped(old)) && !state.HasPendingExtraReader(old),
            "Canceled extraReader still qualified");
        const auto next=Reader(2,2);Require(state.AdmitExtraReader(next) && state.HasPendingExtraReader(next) && !state.HasPendingExtraReader(old),
            "Reused reader slot accepted old identity");
        state.UnsupportedExtraCompletion(next.leaseId);Require(state.PendingReaders()==1 && !state.HasPendingExtraReader(next),
            "Quarantined retained reader qualified for new use");
    }
    {Ledger ledger;auto& state=Reserve(ledger);const auto reader=Reader();Require(state.AdmitExtraReader(reader),"Shutdown query fixture failed");
        ledger.Shutdown();Require(state.PendingReaders()==1 && !state.HasPendingExtraReader(reader),"Shutdown reader qualified for new use");}
}
}
int main()try{
    static_assert(!std::is_copy_constructible_v<State> && !std::is_move_constructible_v<State>);
    static_assert(!std::is_copy_constructible_v<Ledger> && !std::is_move_constructible_v<Ledger>);
    Lifetimes();ReservationFailuresAndBounds();SavedAllocationAliasing();CompletionAndClosure();IntentsAndTransferFailures();AllocatorAndShutdown();ExactlyOnceCancellationAndAba();UnescapedCancellationAndAbandonment();ExactPendingReaderQuery();
    std::puts("PASS native retirement deferral: 12544 retirement and 12544 unescaped cancellation cycles, bounded storage and one destruction attempt");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
