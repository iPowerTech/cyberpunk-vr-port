#include "Render/NativeDefinitionGenerations.hpp"
#include <cstdio>
#include <stdexcept>

using namespace cvr::stereo::definition_generations;
namespace {
constexpr uint64_t Header=0x10000,Epoch=37,Request=11,Pair=4;
void Require(bool value){if(!value)throw std::runtime_error("Native definition generation assertion failed");}
HeaderObservation Observation(uint64_t header=Header,uint64_t epoch=Epoch){return {header,epoch,epoch,epoch,false};}
AggregateTerminalProof Terminal(uint64_t id,uint64_t request=Request,uint64_t pair=Pair){return {id,request,pair,true,true};}
uint64_t Birth(Ledger& ledger,uint64_t header=Header,uint64_t epoch=Epoch,bool prior=false){
    const auto id=ledger.BirthGeneration(Observation(header,epoch),prior);Require(id!=0);return id;
}
void SourceAndRetirement(){
    Ledger ledger;const auto generation=Birth(ledger);
    Require(generation==1 && ledger.FindGeneration(Header,Epoch)==generation);
    Require(ledger.ActiveGenerations()==1 && ledger.TrackedOwners()==1 && !ledger.CanAdopt(generation,true));
    const auto* state=ledger.GetGeneration(generation);
    Require(state && !state->allPriorConsumersAccounted && !state->unsupported && !state->detached && !state->sourceClosed);
    Require(!ledger.OwnershipEligible(generation) && !ledger.ReadyToRetire(generation));
    Require(ledger.RegisterConsumer(1,Request,Pair));
    Require(ledger.BindConsumer(1,Request,Pair,generation,Epoch));
    Require(ledger.ConsumerBound(1,generation) && ledger.GetGeneration(generation)->pendingConsumers==1);
    Require(ledger.CloseSource(generation,Epoch,Epoch+1));
    Require(!ledger.FindGeneration(Header,Epoch) && !ledger.CanAdopt(generation,true));
    Require(ledger.VerifyPriorConsumers(generation,Epoch,true));
    Require(!ledger.CanAdopt(generation) && ledger.CanAdopt(generation,true));
    Require(!ledger.OwnershipEligible(generation) && !ledger.ReadyToRetire(generation));
    Require(ledger.VerifyDetach(generation,Epoch,true));
    Require(!ledger.CanAdopt(generation,true) && ledger.OwnershipEligible(generation) && !ledger.ReadyToRetire(generation));
    Require(!ledger.BeginRetire(generation,Epoch));
    Require(ledger.CompleteConsumer(Terminal(1)) && !ledger.HasConsumer(1) && ledger.ActiveConsumers()==0);
    state=ledger.GetGeneration(generation);
    Require(state->boundConsumers==1 && state->completedConsumers==1 && !state->pendingConsumers && !state->unsupported);
    Require(ledger.ReadyToRetire(generation) && ledger.BeginRetire(generation,Epoch));
    Require(!ledger.BeginRetire(generation,Epoch) && !ledger.ReadyToRetire(generation));
    Require(ledger.GetGeneration(generation)->retiring && ledger.FinishRetire(generation,Epoch));
    Require(!ledger.GetGeneration(generation) && ledger.ActiveGenerations()==0 && ledger.RetiredGenerations()==1);
    Require(!ledger.FinishRetire(generation,Epoch) && ledger.Rejection()==Failure::Generation);
    Require(!ledger.CompleteConsumer(Terminal(1)) && ledger.CompletedConsumers()==1);
}
void DelayedExecutionAndRearm(){
    Ledger ledger;const auto old=Birth(ledger,Header,Epoch,true);
    Require(ledger.RegisterConsumer(1,Request,Pair) && ledger.RegisterConsumer(2,0,0));
    Require(ledger.BindConsumer(1,Request,Pair,old,Epoch));
    Require(ledger.CloseSource(old,Epoch,Epoch+1) && ledger.VerifyDetach(old,Epoch,true));
    Require(ledger.OwnershipEligible(old) && !ledger.ReadyToRetire(old));
    const auto replacement=Birth(ledger,Header,Epoch+1,true);
    Require(replacement>old && ledger.FindGeneration(Header,Epoch+1)==replacement);
    Require(ledger.RegisterConsumer(3,Request+1,Pair+1));
    Require(ledger.BindConsumer(3,Request+1,Pair+1,replacement,Epoch+1));
    // A root admitted before replacement may observe its new generation only at
    // actual execution; cancelling/rearming a selected capture changes no IDs.
    Require(ledger.BindConsumer(1,Request,Pair,replacement,Epoch+1,Admission::Execution));
    Require(ledger.ConsumerBound(1,old) && ledger.ConsumerBound(1,replacement));
    Require(ledger.CompleteConsumer(Terminal(1)));
    Require(!ledger.ReadyToRetire(old));
    Require(ledger.BindConsumer(2,0,0,old,Epoch,Admission::Execution));
    Require(!ledger.ReadyToRetire(old) && ledger.GetGeneration(old)->pendingConsumers==1);
    Require(ledger.CompleteConsumer(Terminal(2,0,0)) && ledger.ReadyToRetire(old));
    Require(ledger.GetGeneration(old)->completedConsumers==2);
    Require(ledger.BeginRetire(old,Epoch) && ledger.FinishRetire(old,Epoch));
    Require(ledger.HasConsumer(3) && ledger.GetGeneration(replacement)->pendingConsumers==1);
    Require(ledger.CloseSource(replacement,Epoch+1,Epoch+2) && ledger.VerifyDetach(replacement,Epoch+1,true));
    Require(!ledger.ReadyToRetire(replacement) && ledger.CompleteConsumer(Terminal(3,Request+1,Pair+1)));
    Require(ledger.ReadyToRetire(replacement));
}
void BoundedLongProcess(){
    Ledger ledger;const auto generation=Birth(ledger);
    constexpr uint64_t Completed=12050;
    for(uint64_t id=1;id<=Completed;++id){
        Require(ledger.RegisterConsumer(id,Request,Pair));
        Require(ledger.BindConsumer(id,Request,Pair,generation,Epoch));
        Require(ledger.CompleteConsumer(Terminal(id)));
        Require(!ledger.ActiveConsumers() && ledger.ActiveGenerations()==1 && ledger.TrackedOwners()==1);
    }
    const auto* state=ledger.GetGeneration(generation);
    Require(state->boundConsumers==Completed && state->completedConsumers==Completed && !state->pendingConsumers);
    Require(ledger.CompletedConsumers()==Completed && ledger.LastConsumerId()==Completed);
    Require(ledger.CloseSource(generation,Epoch,Epoch+1) && ledger.VerifyDetach(generation,Epoch,true));
    Require(!ledger.CanAdopt(generation,true) && !ledger.OwnershipEligible(generation) && !ledger.ReadyToRetire(generation));
    Require(ledger.VerifyPriorConsumers(generation,Epoch,true) && ledger.ReadyToRetire(generation));
    Require(!ledger.RegisterConsumer(Completed,Request,Pair) && ledger.Rejection()==Failure::Duplicate);
    Ledger reused;
    for(uint64_t i=0;i<Completed;++i){
        const auto born=100+i*2;const auto id=Birth(reused,Header,born,true);
        Require(id==i+1 && reused.CloseSource(id,born,born+1));
        Require(reused.CanAdopt(id,true) && reused.VerifyDetach(id,born,true));
        Require(reused.ReadyToRetire(id) && reused.BeginRetire(id,born) && reused.FinishRetire(id,born));
        Require(!reused.ActiveGenerations() && reused.TrackedOwners()==1);
        Require(!reused.BirthGeneration(Observation(Header,born),true) && reused.Rejection()==Failure::Duplicate);
    }
    Require(reused.LastGenerationId()==Completed && reused.RetiredGenerations()==Completed);
}
void RejectedObservations(){
    Ledger ledger;
    Require(!ledger.BirthGeneration(Observation(0)) && ledger.Rejection()==Failure::Arguments);
    Require(!ledger.BirthGeneration(Observation(Header+1)) && ledger.Rejection()==Failure::Arguments);
    Require(!ledger.BirthGeneration(Observation(Header,0)) && ledger.Rejection()==Failure::Arguments);
    auto torn=Observation();torn.sequenceAfter++;
    Require(!ledger.BirthGeneration(torn) && ledger.Rejection()==Failure::Sequence);
    torn=Observation();torn.sequenceBefore=torn.sequenceAfter=0;
    Require(!ledger.BirthGeneration(torn) && ledger.Rejection()==Failure::Sequence);
    torn=Observation();torn.mutationInFlight=true;
    Require(!ledger.BirthGeneration(torn) && ledger.Rejection()==Failure::Mutation);
    const auto generation=Birth(ledger);
    Require(!ledger.BirthGeneration(Observation()) && ledger.Rejection()==Failure::Duplicate);
    Require(!ledger.BirthGeneration(Observation(Header,Epoch+1)) && ledger.Rejection()==Failure::Mutation);
    Require(ledger.GetGeneration(generation)->unsupported && ledger.GetGeneration(generation)->unsupportedReason==Failure::Mutation);
    Require(ledger.CloseSource(generation,Epoch,Epoch+3));
    Require(!ledger.BirthGeneration(Observation(Header,Epoch+2)) && ledger.Rejection()==Failure::Epoch);
    const auto next=Birth(ledger,Header,Epoch+5);
    Require(next>generation && ledger.FindGeneration(Header,Epoch+5)==next);
    Require(!ledger.FindGeneration(Header,Epoch) && ledger.GetGeneration(generation)->sourceClosed);
    Require(!ledger.CloseSource(generation,Epoch,Epoch+3) && ledger.Rejection()==Failure::Duplicate);
}
void FailedConsumersRemainUnresolved(){
    Ledger ledger;const auto a=Birth(ledger,Header,Epoch,true),b=Birth(ledger,Header+0x100,Epoch+1,true);
    Require(ledger.RegisterConsumer(1,Request,Pair));
    Require(ledger.BindConsumer(1,Request,Pair,a,Epoch) && ledger.BindConsumer(1,Request,Pair,b,Epoch+1));
    Require(!ledger.BindConsumer(1,Request,Pair,a,Epoch) && ledger.Rejection()==Failure::Duplicate);
    Require(ledger.GetGeneration(a)->pendingConsumers==1 && !ledger.GetGeneration(a)->unsupported);
    auto proof=Terminal(1);proof.originalReturned=false;
    Require(!ledger.CompleteConsumer(proof) && ledger.Rejection()==Failure::TerminalProof);
    Require(ledger.GetGeneration(a)->unsupported && ledger.GetGeneration(b)->unsupported);
    Require(!ledger.CompleteConsumer(Terminal(1)) && !ledger.CompletedConsumers());
    Require(ledger.UnsupportedConsumer(1,Request,Pair) && !ledger.ActiveConsumers());
    Require(ledger.GetGeneration(a)->pendingConsumers==1 && ledger.GetGeneration(a)->unsupportedConsumers==1);
    Require(ledger.GetGeneration(b)->pendingConsumers==1 && !ledger.GetGeneration(b)->completedConsumers);
    Require(ledger.CloseSource(a,Epoch,Epoch+2) && ledger.VerifyDetach(a,Epoch,true));
    Require(!ledger.CanAdopt(a,true) && !ledger.OwnershipEligible(a) && !ledger.ReadyToRetire(a));
    Ledger pending;const auto unresolved=Birth(pending,Header,Epoch,true);
    Require(pending.RegisterConsumer(1,Request,Pair) && pending.BindConsumer(1,Request,Pair,unresolved,Epoch));
    proof=Terminal(1);proof.aggregateComplete=false;
    Require(!pending.CompleteConsumer(proof) && pending.GetGeneration(unresolved)->unsupported);
    Require(!pending.GetGeneration(unresolved)->completedConsumers && pending.GetGeneration(unresolved)->pendingConsumers==1);
    Ledger unknown;const auto uncovered=Birth(unknown,Header,Epoch,true);
    Require(!unknown.BindConsumer(99,Request,Pair,uncovered,Epoch));
    Require(unknown.Rejection()==Failure::Consumer && unknown.GetGeneration(uncovered)->unsupported);
    Ledger unbound;const auto delayed=Birth(unbound,Header,Epoch,true);
    Require(unbound.RegisterConsumer(1,Request,Pair));
    Require(unbound.CloseSource(delayed,Epoch,Epoch+1) && unbound.VerifyDetach(delayed,Epoch,true));
    Require(!unbound.ReadyToRetire(delayed));
    Require(unbound.UnsupportedConsumer(1,Request,Pair));
    Require(!unbound.ActiveConsumers() && unbound.GetGeneration(delayed)->unsupported && !unbound.ReadyToRetire(delayed));
}
void WrongEpochAndLateAdmission(){
    Ledger wrong;const auto a=Birth(wrong,Header,Epoch,true);
    Require(wrong.RegisterConsumer(1,Request,Pair));
    Require(!wrong.BindConsumer(1,Request+1,Pair,a,Epoch) && wrong.Rejection()==Failure::Epoch);
    Require(wrong.GetGeneration(a)->unsupported && !wrong.GetGeneration(a)->boundConsumers);
    Require(!wrong.CompleteConsumer(Terminal(1)) && !wrong.CompletedConsumers());
    Ledger wrongProof;const auto b=Birth(wrongProof,Header,Epoch,true);
    Require(wrongProof.RegisterConsumer(1,Request,Pair) && wrongProof.BindConsumer(1,Request,Pair,b,Epoch));
    Require(!wrongProof.CompleteConsumer(Terminal(1,Request,Pair+1)) && wrongProof.Rejection()==Failure::Epoch);
    Require(wrongProof.GetGeneration(b)->pendingConsumers==1 && wrongProof.GetGeneration(b)->unsupported);
    Ledger wrongBirth;const auto c=Birth(wrongBirth,Header,Epoch,true);
    Require(!wrongBirth.CloseSource(c,Epoch+1,Epoch+2) && wrongBirth.Rejection()==Failure::Epoch);
    Require(wrongBirth.GetGeneration(c)->unsupported && !wrongBirth.GetGeneration(c)->sourceClosed);
    Ledger late;const auto d=Birth(late,Header,Epoch,true);
    Require(late.RegisterConsumer(1,Request,Pair) && late.CloseSource(d,Epoch,Epoch+1));
    Require(!late.BindConsumer(1,Request,Pair,d,Epoch,Admission::Source) && late.Rejection()==Failure::SourceClosed);
    Require(late.GetGeneration(d)->unsupported);
    Require(!late.CompleteConsumer(Terminal(1)) && !late.CompletedConsumers());
    Ledger tooLate;const auto e=Birth(tooLate,Header,Epoch,true);
    Require(tooLate.CloseSource(e,Epoch,Epoch+1) && tooLate.RegisterConsumer(1,Request,Pair));
    Require(!tooLate.BindConsumer(1,Request,Pair,e,Epoch,Admission::Execution) && tooLate.Rejection()==Failure::SourceClosed);
    Require(tooLate.GetGeneration(e)->unsupported);
    Require(!tooLate.CompleteConsumer(Terminal(1)) && !tooLate.CompletedConsumers());
    Ledger noDetach;const auto f=Birth(noDetach,Header,Epoch,true);
    Require(noDetach.CloseSource(f,Epoch,Epoch+1) && noDetach.CanAdopt(f,true));
    Require(!noDetach.VerifyDetach(f,Epoch,false) && noDetach.Rejection()==Failure::Detach);
    Require(!noDetach.OwnershipEligible(f) && !noDetach.ReadyToRetire(f));
    Ledger coverage;const auto g=Birth(coverage);
    Require(!coverage.VerifyPriorConsumers(g,Epoch,false) && coverage.Rejection()==Failure::PriorCoverage);
    Require(coverage.GetGeneration(g)->unsupported && !coverage.CanAdopt(g,true));
}
void CapacityAndRetirementGuards(){
    Ledger consumers;const auto generation=Birth(consumers,Header,Epoch,true);
    for(uint64_t id=1;id<=MaxConsumers;++id)Require(consumers.RegisterConsumer(id,Request,Pair));
    Require(consumers.ActiveConsumers()==MaxConsumers);
    Require(!consumers.RegisterConsumer(MaxConsumers+1,Request,Pair) && consumers.Rejection()==Failure::Capacity);
    Require(consumers.ActiveConsumers()==MaxConsumers && consumers.LastConsumerId()==MaxConsumers+1);
    Require(consumers.GetGeneration(generation)->unsupported);
    for(uint64_t id=1;id<=MaxConsumers;++id)Require(consumers.CompleteConsumer(Terminal(id)));
    Require(!consumers.ActiveConsumers() && consumers.RegisterConsumer(MaxConsumers+2,Request,Pair));
    Require(!consumers.RegisterConsumer(MaxConsumers+1,Request,Pair) && consumers.Rejection()==Failure::Duplicate);
    Ledger generations;
    for(uint64_t i=0;i<MaxGenerations;++i)Birth(generations,Header+i*0x100,Epoch+i,true);
    Require(generations.ActiveGenerations()==MaxGenerations && generations.TrackedOwners()==MaxOwners);
    Require(!generations.BirthGeneration(Observation(Header+MaxOwners*0x100,Epoch+MaxOwners)) && generations.Rejection()==Failure::Capacity);
    const auto oldest=uint64_t(1);
    Require(generations.CloseSource(oldest,Epoch,Epoch+MaxOwners+1) && generations.VerifyDetach(oldest,Epoch,true));
    Require(generations.BeginRetire(oldest,Epoch) && generations.FinishRetire(oldest,Epoch));
    Require(generations.ActiveGenerations()==MaxGenerations-1 && generations.TrackedOwners()==MaxOwners);
    Require(!generations.BirthGeneration(Observation(Header+MaxOwners*0x100,Epoch+MaxOwners+2)) && generations.Rejection()==Failure::Capacity);
    const auto replacement=Birth(generations,Header,Epoch+MaxOwners+3);
    Require(replacement==MaxGenerations+1 && generations.ActiveGenerations()==MaxGenerations);
    Ledger manySameOwner;
    for(uint64_t i=0;i<MaxGenerations;++i){const auto born=100+i;const auto id=Birth(manySameOwner,Header,born);
        Require(manySameOwner.CloseSource(id,born,born+1));}
    Require(manySameOwner.ActiveGenerations()==MaxGenerations && manySameOwner.TrackedOwners()==1);
    Require(!manySameOwner.BirthGeneration(Observation(Header,100+MaxGenerations)) && manySameOwner.Rejection()==Failure::Capacity);
    Ledger retirement;const auto guarded=Birth(retirement,Header,Epoch,true);
    Require(retirement.CloseSource(guarded,Epoch,Epoch+1) && retirement.VerifyDetach(guarded,Epoch,true));
    Require(!retirement.FinishRetire(guarded,Epoch) && retirement.Rejection()==Failure::Retirement);
    Require(retirement.BeginRetire(guarded,Epoch));
    Require(retirement.UnsupportedGeneration(guarded,Epoch,Failure::Mutation));
    Require(!retirement.FinishRetire(guarded,Epoch) && retirement.GetGeneration(guarded)->retiring);
    Require(!retirement.BeginRetire(guarded,Epoch) && retirement.ActiveGenerations()==1);
}
void ObservationForgetting(){
    Ledger ledger;const auto generation=Birth(ledger);
    Require(!ledger.ForgetClosedObservation(generation,Epoch) && ledger.Rejection()==Failure::Incomplete);
    Require(ledger.RegisterConsumer(1,Request,Pair) && ledger.BindConsumer(1,Request,Pair,generation,Epoch));
    Require(ledger.CloseSource(generation,Epoch,Epoch+1));
    Require(!ledger.ForgetClosedObservation(generation,Epoch) && ledger.GetGeneration(generation)->pendingConsumers==1);
    Require(ledger.CompleteConsumer(Terminal(1)));
    Require(!ledger.OwnershipEligible(generation) && !ledger.ReadyToRetire(generation));
    Require(ledger.ForgetClosedObservation(generation,Epoch) && ledger.ForgottenObservations()==1);
    Require(!ledger.ActiveGenerations() && ledger.TrackedOwners()==1 && !ledger.RetiredGenerations());
    Require(!ledger.ForgetClosedObservation(generation,Epoch) && ledger.Rejection()==Failure::Generation);
    Require(ledger.ForgottenObservations()==1);
    Require(!ledger.BirthGeneration(Observation(Header,Epoch)) && ledger.Rejection()==Failure::Duplicate);
    const auto replacement=Birth(ledger,Header,Epoch+1);
    Require(replacement>generation && ledger.FindGeneration(Header,Epoch+1)==replacement);
    Ledger delayed;const auto unseen=Birth(delayed);
    Require(delayed.RegisterConsumer(1,Request,Pair) && delayed.CloseSource(unseen,Epoch,Epoch+1));
    Require(!delayed.GetGeneration(unseen)->pendingConsumers && !delayed.ForgetClosedObservation(unseen,Epoch));
    Require(delayed.CompleteConsumer(Terminal(1)) && delayed.ForgetClosedObservation(unseen,Epoch));
    Ledger detached;const auto owned=Birth(detached,Header,Epoch,true);
    Require(detached.CloseSource(owned,Epoch,Epoch+1) && detached.VerifyDetach(owned,Epoch,true));
    Require(!detached.ForgetClosedObservation(owned,Epoch) && detached.ReadyToRetire(owned));
    Require(detached.BeginRetire(owned,Epoch) && !detached.ForgetClosedObservation(owned,Epoch));
    Require(detached.FinishRetire(owned,Epoch) && !detached.ForgottenObservations() && detached.RetiredGenerations()==1);
    Ledger unsupported;const auto unresolved=Birth(unsupported);
    Require(unsupported.RegisterConsumer(1,Request,Pair) && unsupported.BindConsumer(1,Request,Pair,unresolved,Epoch));
    Require(unsupported.CloseSource(unresolved,Epoch,Epoch+1) && unsupported.UnsupportedConsumer(1,Request,Pair));
    Require(!unsupported.ActiveConsumers() && unsupported.GetGeneration(unresolved)->unsupported);
    Require(!unsupported.ForgetClosedObservation(unresolved,Epoch) && !unsupported.ForgottenObservations());
    Ledger unsupportedIdle;const auto idle=Birth(unsupportedIdle);
    Require(unsupportedIdle.UnsupportedGeneration(idle,Epoch) && unsupportedIdle.CloseSource(idle,Epoch,Epoch+1));
    Require(unsupportedIdle.ForgetClosedObservation(idle,Epoch) && unsupportedIdle.ForgottenObservations()==1);
    Ledger wrongEpoch;const auto exact=Birth(wrongEpoch);
    Require(wrongEpoch.CloseSource(exact,Epoch,Epoch+1));
    Require(!wrongEpoch.ForgetClosedObservation(exact,Epoch+1) && wrongEpoch.Rejection()==Failure::Epoch);
    Require(wrongEpoch.GetGeneration(exact) && !wrongEpoch.ForgottenObservations());
    Require(wrongEpoch.ForgetClosedObservation(exact,Epoch) && wrongEpoch.ForgottenObservations()==1);
    Ledger longObservation;constexpr uint64_t Cycles=12050;
    for(uint64_t i=0;i<Cycles;++i){const auto born=100+i*2;const auto id=Birth(longObservation,Header,born);
        Require(id==i+1 && longObservation.CloseSource(id,born,born+1));
        Require(!longObservation.CanAdopt(id,true) && !longObservation.OwnershipEligible(id) && !longObservation.ReadyToRetire(id));
        Require(longObservation.ForgetClosedObservation(id,born));
        Require(!longObservation.ActiveGenerations() && longObservation.TrackedOwners()==1);
        Require(!longObservation.BirthGeneration(Observation(Header,born)) && longObservation.Rejection()==Failure::Duplicate);
    }
    Require(longObservation.ForgottenObservations()==Cycles && longObservation.LastGenerationId()==Cycles && !longObservation.RetiredGenerations());
}
void ProvedNoSourceCancellation(){
    Ledger ledger;const auto generation=Birth(ledger,Header,Epoch,true);
    Require(ledger.RegisterConsumer(1,Request,Pair) && ledger.CloseSource(generation,Epoch,Epoch+1));
    Require(ledger.VerifyDetach(generation,Epoch,true) && !ledger.ReadyToRetire(generation));
    Require(ledger.CancelUnboundConsumer(1,Request,Pair,true));
    Require(!ledger.HasConsumer(1) && ledger.CancelledConsumers()==1 && !ledger.CompletedConsumers());
    Require(!ledger.GetGeneration(generation)->unsupported && ledger.ReadyToRetire(generation));
    Require(!ledger.CancelUnboundConsumer(1,Request,Pair,true) && ledger.Rejection()==Failure::Consumer);
    Require(ledger.CancelledConsumers()==1 && !ledger.RegisterConsumer(1,Request,Pair));
    Ledger observation;const auto unseen=Birth(observation);
    Require(observation.RegisterConsumer(1,Request,Pair) && observation.CloseSource(unseen,Epoch,Epoch+1));
    Require(!observation.ForgetClosedObservation(unseen,Epoch));
    Require(observation.CancelUnboundConsumer(1,Request,Pair,true) && observation.ForgetClosedObservation(unseen,Epoch));
    Require(observation.CancelledConsumers()==1 && observation.ForgottenObservations()==1 && !observation.CompletedConsumers());
    Ledger falseProof;
    Require(falseProof.RegisterConsumer(1,Request,Pair));
    Require(!falseProof.CancelUnboundConsumer(1,Request,Pair) && falseProof.Rejection()==Failure::NoSourceProof);
    Require(falseProof.HasConsumer(1) && !falseProof.CancelledConsumers());
    Require(!falseProof.CancelUnboundConsumer(1,Request,Pair,true) && falseProof.Rejection()==Failure::Unsupported);
    Require(!falseProof.CompleteConsumer(Terminal(1)) && !falseProof.CompletedConsumers());
    Require(falseProof.UnsupportedConsumer(1,Request,Pair) && falseProof.UnsupportedConsumers()==1);
    Ledger bound;const auto owned=Birth(bound);
    Require(bound.RegisterConsumer(1,Request,Pair) && bound.BindConsumer(1,Request,Pair,owned,Epoch));
    Require(!bound.CancelUnboundConsumer(1,Request,Pair,true) && bound.Rejection()==Failure::Consumer);
    Require(bound.GetGeneration(owned)->unsupported && bound.GetGeneration(owned)->pendingConsumers==1);
    Require(!bound.CompletedConsumers() && !bound.CancelledConsumers() && bound.HasConsumer(1));
    Require(!bound.CompleteConsumer(Terminal(1)) && bound.UnsupportedConsumer(1,Request,Pair));
    Require(bound.GetGeneration(owned)->pendingConsumers==1 && !bound.GetGeneration(owned)->completedConsumers);
    Ledger wrongEpoch;
    Require(wrongEpoch.RegisterConsumer(1,Request,Pair));
    Require(!wrongEpoch.CancelUnboundConsumer(1,Request+1,Pair,true) && wrongEpoch.Rejection()==Failure::Epoch);
    Require(wrongEpoch.HasConsumer(1) && !wrongEpoch.CancelledConsumers());
    Require(!wrongEpoch.CancelUnboundConsumer(1,Request,Pair,true) && wrongEpoch.Rejection()==Failure::Unsupported);
    Ledger sticky;
    Require(sticky.RegisterConsumer(1,Request,Pair));
    auto proof=Terminal(1);proof.originalReturned=false;
    Require(!sticky.CompleteConsumer(proof));
    Require(!sticky.CancelUnboundConsumer(1,Request,Pair,true) && sticky.Rejection()==Failure::Unsupported);
    Require(!sticky.CancelledConsumers() && !sticky.CompletedConsumers() && sticky.HasConsumer(1));
    Require(!sticky.CancelUnboundConsumer(99,Request,Pair,true) && sticky.Rejection()==Failure::Consumer);
}
void ProvisionalAdmissionAndIdentitySeal(){
    Ledger delayed;const auto generation=Birth(delayed,Header,Epoch,true);
    Require(delayed.RegisterProvisionalConsumer(1));
    Require(delayed.LastConsumerId()==1 && delayed.HasConsumer(1));
    Require(delayed.CloseSource(generation,Epoch,Epoch+1) && delayed.GetGeneration(generation)->closedAtConsumer==1);
    Require(delayed.VerifyDetach(generation,Epoch,true) && !delayed.ReadyToRetire(generation));
    // Pair identity advances inside the native root, after admission but before
    // its constructor. Sealing preserves the original admission ID/high-water.
    Require(delayed.SealProvisionalConsumer(1,Request,Pair+1));
    Require(delayed.LastConsumerId()==1 && delayed.BindConsumer(1,Request,Pair+1,generation,Epoch,Admission::Execution));
    Require(!delayed.SealProvisionalConsumer(1,Request,Pair+1) && delayed.Rejection()==Failure::Duplicate);
    Require(!delayed.GetGeneration(generation)->unsupported && !delayed.ReadyToRetire(generation));
    Require(delayed.CompleteConsumer(Terminal(1,Request,Pair+1)) && delayed.ReadyToRetire(generation));
    Require(delayed.CompletedConsumers()==1 && !delayed.SealProvisionalConsumer(1,Request,Pair+1));
    Ledger replacement;const auto old=Birth(replacement);
    Require(replacement.RegisterProvisionalConsumer(1) && replacement.CloseSource(old,Epoch,Epoch+1));
    const auto current=Birth(replacement,Header,Epoch+1);
    Require(replacement.SealProvisionalConsumer(1,Request,Pair+1));
    Require(replacement.BindConsumer(1,Request,Pair+1,current,Epoch+1));
    Require(replacement.CompleteConsumer(Terminal(1,Request,Pair+1)) && replacement.ForgetClosedObservation(old,Epoch));
    Ledger unsafeBind;const auto unsealedGeneration=Birth(unsafeBind);
    Require(unsafeBind.RegisterProvisionalConsumer(1));
    Require(!unsafeBind.BindConsumer(1,0,0,unsealedGeneration,Epoch) && unsafeBind.Rejection()==Failure::Unsealed);
    Require(unsafeBind.GetGeneration(unsealedGeneration)->unsupported && !unsafeBind.GetGeneration(unsealedGeneration)->boundConsumers);
    Require(!unsafeBind.SealProvisionalConsumer(1,Request,Pair+1) && unsafeBind.Rejection()==Failure::Unsupported);
    Require(!unsafeBind.CompleteConsumer(Terminal(1,0,0)) && !unsafeBind.CompletedConsumers());
    Require(unsafeBind.UnsupportedConsumer(1,0,0));
    Ledger unsafeComplete;
    Require(unsafeComplete.RegisterProvisionalConsumer(1));
    Require(!unsafeComplete.CompleteConsumer(Terminal(1,0,0)) && unsafeComplete.Rejection()==Failure::Unsealed);
    Require(!unsafeComplete.SealProvisionalConsumer(1,Request,Pair) && unsafeComplete.Rejection()==Failure::Unsupported);
    Require(!unsafeComplete.CancelUnboundConsumer(1,0,0,true) && !unsafeComplete.CancelledConsumers());
    Require(unsafeComplete.HasConsumer(1) && !unsafeComplete.CompletedConsumers());
    Ledger changedIdentity;const auto immutable=Birth(changedIdentity);
    Require(changedIdentity.RegisterProvisionalConsumer(1) && changedIdentity.SealProvisionalConsumer(1,Request,Pair+1));
    Require(changedIdentity.BindConsumer(1,Request,Pair+1,immutable,Epoch));
    Require(!changedIdentity.SealProvisionalConsumer(1,Request+1,Pair+2) && changedIdentity.Rejection()==Failure::Epoch);
    Require(changedIdentity.GetGeneration(immutable)->unsupported && changedIdentity.GetGeneration(immutable)->pendingConsumers==1);
    Require(!changedIdentity.CompleteConsumer(Terminal(1,Request,Pair+1)) && !changedIdentity.CompletedConsumers());
    Ledger wrongTerminal;const auto wrong=Birth(wrongTerminal);
    Require(wrongTerminal.RegisterProvisionalConsumer(1) && wrongTerminal.SealProvisionalConsumer(1,Request,Pair+1));
    Require(wrongTerminal.BindConsumer(1,Request,Pair+1,wrong,Epoch));
    Require(!wrongTerminal.CompleteConsumer(Terminal(1,Request,Pair)) && wrongTerminal.Rejection()==Failure::Epoch);
    Require(wrongTerminal.GetGeneration(wrong)->unsupported && !wrongTerminal.CompletedConsumers());
    Ledger noSource;const auto idle=Birth(noSource);
    Require(noSource.RegisterProvisionalConsumer(1) && noSource.CloseSource(idle,Epoch,Epoch+1));
    Require(!noSource.ForgetClosedObservation(idle,Epoch));
    Require(noSource.CancelUnboundConsumer(1,0,0,true) && noSource.CancelledConsumers()==1);
    Require(!noSource.CompletedConsumers() && noSource.ForgetClosedObservation(idle,Epoch));
    Ledger missingProof;
    Require(missingProof.RegisterProvisionalConsumer(1));
    Require(!missingProof.CancelUnboundConsumer(1,0,0) && missingProof.Rejection()==Failure::NoSourceProof);
    Require(!missingProof.SealProvisionalConsumer(1,Request,Pair) && missingProof.Rejection()==Failure::Unsupported);
    Ledger normal;
    Require(normal.RegisterConsumer(1,Request,Pair) && normal.CompleteConsumer(Terminal(1)));
    Require(normal.RegisterConsumer(2,0,0) && normal.CompleteConsumer(Terminal(2,0,0)));
    Require(normal.RegisterProvisionalConsumer(3) && normal.SealProvisionalConsumer(3,0,0));
    Require(normal.CompleteConsumer(Terminal(3,0,0)));
    Require(normal.RegisterConsumer(4,Request,Pair));
    Require(!normal.SealProvisionalConsumer(4,Request,Pair) && normal.Rejection()==Failure::Duplicate);
    Require(normal.CompleteConsumer(Terminal(4)) && normal.CompletedConsumers()==4);
    Require(!normal.RegisterProvisionalConsumer(4) && normal.Rejection()==Failure::Duplicate);
    Ledger capacity;
    for(uint64_t id=1;id<=MaxConsumers;++id)Require(capacity.RegisterProvisionalConsumer(id));
    Require(!capacity.RegisterProvisionalConsumer(MaxConsumers+1) && capacity.Rejection()==Failure::Capacity);
    Require(capacity.ActiveConsumers()==MaxConsumers && capacity.LastConsumerId()==MaxConsumers+1);
    for(uint64_t id=1;id<=MaxConsumers;++id){Require(capacity.SealProvisionalConsumer(id,Request,Pair+1));
        Require(capacity.CompleteConsumer(Terminal(id,Request,Pair+1)));}
    Require(!capacity.ActiveConsumers() && capacity.RegisterProvisionalConsumer(MaxConsumers+2));
    Require(!capacity.RegisterProvisionalConsumer(MaxConsumers+1) && capacity.Rejection()==Failure::Duplicate);
    Require(!capacity.SealProvisionalConsumer(999,Request,Pair) && capacity.Rejection()==Failure::Consumer);
}
}
int main()try{
    SourceAndRetirement();DelayedExecutionAndRearm();BoundedLongProcess();RejectedObservations();
    FailedConsumersRemainUnresolved();WrongEpochAndLateAdmission();CapacityAndRetirementGuards();ObservationForgetting();ProvedNoSourceCancellation();
    ProvisionalAdmissionAndIdentitySeal();
    std::puts("PASS native definition generations: bounded and provisionally sealed identities, observation cleanup, proved no-source cancellation, execution discoveries, sticky unsupported use, adoption and retirement gates; no native writes/destructors");
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
