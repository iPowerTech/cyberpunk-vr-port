#include "Render/NativeInstanceRing.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstring>
#include <functional>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ring=cvr::stereo::instance_ring;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Status(ring::Status actual,ring::Status expected,const char* message){
    if(actual!=expected)throw std::runtime_error(std::string(message)+": status="+std::to_string(unsigned(actual))+
        " expected="+std::to_string(unsigned(expected)));
}
static ring::Scope Scope(uint64_t occurrence){
    auto thread=uint64_t(std::hash<std::thread::id>{}(std::this_thread::get_id()));if(!thread)thread=1;
    return {0x7000,9,occurrence,thread,0x8000,0x3000,0x2401};
}
struct Fixture {
    std::mutex metadata;ring::Ledger ledger;std::atomic<uint64_t> head;
    std::vector<uint8_t> mapped=std::vector<uint8_t>(6*1024*1024);
    Fixture(uint64_t initial=0,ring::Limits limits={}):ledger(limits),head(initial){}
    ring::RingObservation Native()const {
        return {0x1000,1,0x2000,0x10000000,reinterpret_cast<uintptr_t>(mapped.data()),0x3000,0x4000,0x5000,0x6000,
            6*1024*1024,0x20000,head.load(std::memory_order_acquire),0,17,48,5};
    }
    template<class Function>auto Meta(Function&& function){
        std::lock_guard lock(metadata);return function(ledger,Native());
    }
    void NativeCopy(uint32_t first,uint32_t count,uint8_t value){
        std::fill_n(mapped.data()+size_t(first)*48,size_t(count)*48,value);
    }
    template<size_t N>std::array<uint8_t,N> Sample(uint32_t first)const {
        std::array<uint8_t,N> bytes{};std::memcpy(bytes.data(),mapped.data()+size_t(first)*48,N);return bytes;
    }
};
static ring::ReadRequest Read(const ring::WriterTicket& ticket,uint64_t absolute,uint32_t count){
    return {ticket,Scope(100+ticket.id),absolute,uint32_t(absolute%0x20000),count};
}
static bool Filled(std::span<const uint8_t> bytes,uint8_t value){
    return !bytes.empty() && std::all_of(bytes.begin(),bytes.end(),[=](uint8_t byte){return byte==value;});
}
static ring::WriterTicket Seed(Fixture& fixture,uint64_t absolute,uint32_t count,uint64_t occurrence,uint8_t value){
    ring::WriterTicket ticket;
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(occurrence),ticket);}),ring::Status::Observed,"Seed begin");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,ticket,absolute,uint32_t(absolute%native.capacity),count);}),
        ring::Status::Observed,"Seed exact range qualification");
    fixture.NativeCopy(uint32_t(absolute%0x20000),count,value);
    const auto bytes=std::span<const uint8_t>(fixture.mapped.data()+size_t(absolute%0x20000)*48,size_t(count)*48);
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{ticket,absolute,uint32_t(absolute%native.capacity),count},bytes);}),
        ring::Status::Completed,"Seed completion");return ticket;
}
static void ReversedDisjointCallbacks(){
    Fixture fixture;std::barrier entered(2),qualified(2),outsideNative(2);
    std::latch firstCas(1),secondEvent(1),secondComplete(1);
    std::array<ring::WriterTicket,2> tickets;std::array<ring::CpuCandidate,2> candidates;
    std::array<std::array<ring::Status,6>,2> results{};std::array<std::thread::id,2> threads;
    std::array<uint64_t,2> old{},next{};std::array<bool,2> cas{};std::atomic<unsigned> nativeActive{},peakNative{};
    auto worker=[&](uint32_t index){
        threads[index]=std::this_thread::get_id();
        results[index][0]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(index+1),tickets[index]);});
        entered.arrive_and_wait();
        if(index)firstCas.wait();
        old[index]=index*2;auto expected=old[index];next[index]=old[index]+2;
        cas[index]=fixture.head.compare_exchange_strong(expected,next[index],std::memory_order_acq_rel);
        if(!index){firstCas.count_down();secondEvent.wait();}
        results[index][1]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Reuse(native,old[index],2,&tickets[index]);});
        results[index][2]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,tickets[index],old[index],uint32_t(old[index]),2);});
        if(index)secondEvent.count_down();
        qualified.arrive_and_wait();
        const auto active=nativeActive.fetch_add(1,std::memory_order_acq_rel)+1;auto maximum=peakNative.load();
        while(maximum<active && !peakNative.compare_exchange_weak(maximum,active)){}
        // No metadata lock spans this simulated native copy/sampling phase.
        outsideNative.arrive_and_wait();fixture.NativeCopy(uint32_t(old[index]),2,uint8_t(0x31+index));
        const auto bytes=fixture.Sample<96>(uint32_t(old[index]));nativeActive.fetch_sub(1,std::memory_order_acq_rel);
        if(!index)secondComplete.wait();
        results[index][3]=fixture.Meta([&](auto& ledger,const auto&){return ledger.ValidateWrite(tickets[index]);});
        results[index][4]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{tickets[index],old[index],uint32_t(old[index]),2},bytes);});
        results[index][5]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(tickets[index],old[index],2),candidates[index]);});
        if(index)secondComplete.count_down();
    };
    std::thread a(worker,0),b(worker,1);a.join();b.join();
    Require(threads[0]!=threads[1] && cas[0] && cas[1] && old[0]==0 && old[1]==2 && next[0]==2 && next[1]==4,
        "Real worker/CAS reservation order differs");
    Require(peakNative.load()==2,"Native phases were not simultaneously active outside metadata scopes");
    for(uint32_t index=0;index<2;++index){
        for(uint32_t field=0;field<4;++field)Status(results[index][field],ring::Status::Observed,"Disjoint reverse-arrival metadata rejected");
        Status(results[index][4],ring::Status::Completed,"Disjoint reverse completion rejected");
        Status(results[index][5],ring::Status::Completed,"Query blocked by a disjoint known pending writer");
        Require(candidates[index].Producer().writer==tickets[index] && Filled(candidates[index].Bytes(),uint8_t(0x31+index)),
            "Parallel owned CPU writer version changed");
    }
    Require(fixture.ledger.LiveCpuBytes()==192,"Parallel payload accounting differs");
}
static void CachedOverwriteAndUnknownPending(){
    Fixture fixture(128);const auto oldOverlap=Seed(fixture,0,2,1,0x11),oldDisjoint=Seed(fixture,4,2,2,0x22);
    ring::CpuCandidate retained;
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(oldOverlap,0,2),retained);}),ring::Status::Completed,"Retain cached prior bytes");
    std::latch unknownStarted(1),unknownTested(1),freshQualified(1);std::barrier copies(2);
    std::array<ring::WriterTicket,2> tickets;std::array<std::array<ring::Status,7>,2> results{};
    std::array<ring::CpuCandidate,2> current;ring::Status blocked{},overwritten{},survived{};bool cas{};
    std::thread cached([&]{
        results[0][0]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(3),tickets[0]);});
        unknownStarted.wait();ring::CpuCandidate rejected;
        blocked=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(oldDisjoint,4,2),rejected);});
        results[0][1]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,tickets[0],0,0,2);});
        overwritten=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(oldOverlap,0,2),rejected);});
        unknownTested.count_down();freshQualified.wait();
        survived=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(oldDisjoint,4,2),rejected);});
        copies.arrive_and_wait();fixture.NativeCopy(0,2,0x33);const auto bytes=fixture.Sample<96>(0);
        results[0][2]=fixture.Meta([&](auto& ledger,const auto&){return ledger.ValidateWrite(tickets[0]);});
        results[0][3]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{tickets[0],0,0,2},bytes);});
        results[0][4]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(tickets[0],0,2),current[0]);});
    });
    std::thread fresh([&]{
        results[1][0]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(4),tickets[1]);});
        unknownStarted.count_down();unknownTested.wait();uint64_t expected=128;
        cas=fixture.head.compare_exchange_strong(expected,130,std::memory_order_acq_rel);
        results[1][1]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Reuse(native,128,2,&tickets[1]);});
        results[1][2]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,tickets[1],128,128,2);});
        freshQualified.count_down();copies.arrive_and_wait();fixture.NativeCopy(128,2,0x44);const auto bytes=fixture.Sample<96>(128);
        results[1][3]=fixture.Meta([&](auto& ledger,const auto&){return ledger.ValidateWrite(tickets[1]);});
        results[1][4]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{tickets[1],128,128,2},bytes);});
        results[1][5]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(tickets[1],128,2),current[1]);});
    });
    cached.join();fresh.join();Require(cas,"Fresh disjoint native reservation failed");
    Status(blocked,ring::Status::Pending,"Unknown pending range did not block provisional query");
    Status(overwritten,ring::Status::UnknownWriter,"Cached qualification did not invalidate old overlap before native write");
    Status(survived,ring::Status::Completed,"Disjoint fresh reservation globally quarantined prior provenance");
    for(uint32_t field=0;field<3;++field)Status(results[0][field],ring::Status::Observed,"Cached writer qualification/validation failed");
    Status(results[0][3],ring::Status::Completed,"Cached writer completion failed");Status(results[0][4],ring::Status::Completed,"Cached current query failed");
    for(uint32_t field=0;field<4;++field)Status(results[1][field],ring::Status::Observed,"Fresh disjoint writer metadata failed");
    Status(results[1][4],ring::Status::Completed,"Fresh disjoint completion failed");Status(results[1][5],ring::Status::Completed,"Fresh disjoint query failed");
    Require(Filled(retained.Bytes(),0x11) && Filled(current[0].Bytes(),0x33) && Filled(current[1].Bytes(),0x44),
        "Cached overwrite mutated retained CPU bytes or borrowed another writer");
}
static void EqualSamplesCannotHideOverlappingAba(){
    Fixture fixture(128);std::latch firstSampled(1),restored(1);
    std::array<ring::WriterTicket,2> tickets;std::array<std::array<ring::Status,4>,2> results{};
    std::array<uint8_t,48> before{},after{};
    std::thread first([&]{
        results[0][0]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(1),tickets[0]);});
        results[0][1]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,tickets[0],10,10,1);});
        fixture.NativeCopy(10,1,0x55);before=fixture.Sample<48>(10);firstSampled.count_down();restored.wait();after=fixture.Sample<48>(10);
        results[0][2]=fixture.Meta([&](auto& ledger,const auto&){return ledger.ValidateWrite(tickets[0]);});
        results[0][3]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{tickets[0],10,10,1},after);});
    });
    std::thread overlapping([&]{
        firstSampled.wait();results[1][0]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(2),tickets[1]);});
        results[1][1]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,tickets[1],10,10,1);});
        fixture.NativeCopy(10,1,0x99);fixture.NativeCopy(10,1,0x55);const auto bytes=fixture.Sample<48>(10);
        restored.count_down();results[1][2]=fixture.Meta([&](auto& ledger,const auto&){return ledger.ValidateWrite(tickets[1]);});
        results[1][3]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{tickets[1],10,10,1},bytes);});
    });
    first.join();overlapping.join();Require(before==after && Filled(before,0x55),"ABA timeline did not restore identical samples");
    Status(results[0][0],ring::Status::Observed,"First ABA writer begin");Status(results[0][1],ring::Status::Observed,"First ABA range");
    Status(results[1][0],ring::Status::Observed,"Second ABA writer begin");Status(results[1][1],ring::Status::Overlap,"Overlapping inflight range not marked");
    for(uint32_t index=0;index<2;++index){Status(results[index][2],ring::Status::Overlap,"Sampling ticket forgot prior overlap");
        Status(results[index][3],ring::Status::Pending,"Equal samples legitimized a conflicted completion");}
    Require(!fixture.ledger.LiveCpuBytes(),"Conflicted equal samples acquired owned CPU payloads");
}
static void SameGenerationRecoveryAndRetainedBudget(){
    ring::Limits limits;limits.liveCpuBytes=96;Fixture fixture(128,limits);std::latch ready(1);
    ring::WriterTicket old,fresh;ring::CpuCandidate retained,current;
    std::array<ring::Status,11> results{};uint64_t heldBudget{},releasedBudget{};
    std::thread producer([&]{
        results[0]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(1),old);});
        results[1]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,old,0,0,2);});
        fixture.NativeCopy(0,2,0x71);const auto bytes=fixture.Sample<96>(0);
        results[2]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{old,0,0,2},bytes);});
        results[3]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(old,0,2),retained);});ready.count_down();
    });
    std::thread recovering([&]{
        ready.wait();fixture.Meta([&](auto& ledger,const auto& native){ledger.Unknown(native.ring);return 0;});
        results[4]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.RestartObservation(native);});
        results[5]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(old,0,2),current);});
        const auto lateBytes=fixture.Sample<96>(0);
        results[9]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{old,0,0,2},lateBytes);});
        results[10]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.RestartObservation(native);});
        heldBudget=fixture.Meta([&](auto& ledger,const auto&){return ledger.LiveCpuBytes();});
        ring::WriterTicket limited;
        const auto begin=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(2),limited);});
        const auto qualify=fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,limited,4,4,1);});
        fixture.NativeCopy(4,1,0x72);const auto bytes=fixture.Sample<48>(4);
        results[6]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{limited,4,4,1},bytes);});
        // Preserve failure evidence while allowing both real workers to join.
        if(begin!=ring::Status::Observed || qualify!=ring::Status::Observed)results[6]=ring::Status::Exception;
        retained={};releasedBudget=fixture.Meta([&](auto& ledger,const auto&){return ledger.LiveCpuBytes();});
        fixture.Meta([&](auto& ledger,const auto&){ledger.Reset();return 0;});
        results[7]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(3),fresh);});
        const auto qualified=fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,fresh,4,4,1);});
        results[8]=fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{fresh,4,4,1},bytes);});
        if(qualified!=ring::Status::Observed)results[8]=ring::Status::Exception;
    });
    producer.join();recovering.join();
    for(uint32_t field=0;field<2;++field)Status(results[field],ring::Status::Observed,"Recovery seed metadata");
    Status(results[2],ring::Status::Completed,"Recovery seed completion");Status(results[3],ring::Status::Completed,"Recovery seed retained bytes");
    Status(results[4],ring::Status::Observed,"Fresh local observation could not recover same native generation");
    Status(results[5],ring::Status::UnknownWriter,"Same-generation local recovery revived an old ticket");
    Status(results[9],ring::Status::UnknownWriter,"Late old completion attached to same-generation local recovery");
    Status(results[10],ring::Status::Observed,"Local recovery could not discard a late stale completion");
    Status(results[6],ring::Status::Capacity,"Local recovery hid externally retained CPU bytes from its budget");
    Status(results[7],ring::Status::Observed,"Released budget did not permit a new observer cohort");Status(results[8],ring::Status::Completed,"Fresh observer cohort completion");
    Require(heldBudget==96 && !releasedBudget && fresh.ringGeneration==old.ringGeneration && fresh.observerEpoch!=old.observerEpoch && fresh.id!=old.id,
        "Local cleanup confused native generation, observer epoch or retained-byte accounting");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(old,0,2),current);}),ring::Status::UnknownWriter,
        "Restored numeric labels revived old observer bytes");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(fresh,4,1),current);}),ring::Status::Completed,"Fresh observer data missing");
    Require(Filled(current.Bytes(),0x72),"New observer cohort borrowed old retained bytes");
}
static void ReturnedWriterBeforePeerReservation(){
    Fixture fixture(128);ring::WriterTicket first,peer;ring::CpuCandidate result;
    const auto begin=[&](uint64_t occurrence,ring::WriterTicket& ticket){return fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(occurrence),ticket);});};
    Status(begin(1,first),ring::Status::Observed,"First writer start");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,first,0,0,1);}),ring::Status::Observed,"First writer known range");
    Status(begin(2,peer),ring::Status::Observed,"Peer starts before its reservation");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Publish(native,{Scope(77),native.head});}),
        ring::Status::Pending,"Publication cannot close active writers");
    fixture.NativeCopy(0,1,0x31);
    Status(fixture.Meta([&](auto& ledger,const auto&){return ledger.ValidateWrite(first);}),ring::Status::Pending,"Unknown peer must prevent sampling");
    Status(fixture.Meta([&](auto& ledger,const auto&){return ledger.DiscardCompleted(first);}),ring::Status::Observed,"Returned writer can discard its sample locally");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Reuse(native,128-2,2,&peer);}),ring::Status::Observed,"Peer ticket survived unrelated sample rejection");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,peer,126,126,1);}),ring::Status::Observed,"Peer exact reservation qualifies");
    fixture.NativeCopy(126,1,0x72);const auto bytes=fixture.Sample<48>(126);
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{peer,126,126,1},bytes);}),ring::Status::Completed,"Peer can complete without ring restart");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(peer,126,1),result);}),ring::Status::Completed,"Peer candidate is current");
    Require(Filled(result.Bytes(),0x72),"Peer borrowed discarded sample");
    Status(fixture.Meta([&](auto& ledger,const auto&){return ledger.DiscardCompleted(peer);}),ring::Status::Pending,"Published CPU payload cannot be discarded as an unfinished writer");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(first,0,1),result);}),ring::Status::UnknownWriter,"Discarded writer does not acquire CPU bytes");
}
static void UnobservedCachedWriterKeepsDisjointVersions(){
    Fixture fixture(128);ring::WriterTicket saved,unknown,overlap;ring::CpuCandidate result;
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(1),saved);}),ring::Status::Observed,"Saved start");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,saved,0,0,1);}),ring::Status::Observed,"Saved range");
    fixture.NativeCopy(0,1,0x23);const auto bytes=fixture.Sample<48>(0);
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Complete(native,{saved,0,0,1},bytes);}),ring::Status::Completed,"Saved completion");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(2),unknown);}),ring::Status::Observed,"Unobserved cache entry");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.InvalidatePhysical(native,4,1);}),ring::Status::Observed,"Unobserved range invalidation");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(saved,0,1),result);}),ring::Status::Pending,"Unobserved in-flight write must block sampling");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Begin(native,Scope(3),overlap);}),ring::Status::Observed,"Concurrent overlap start");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.QualifyWrite(native,overlap,4,4,1);}),ring::Status::Observed,"Concurrent overlap range");
    fixture.NativeCopy(4,1,0x31);
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.FinishUnobserved(native,unknown,4,1);}),ring::Status::Observed,"Actual return ends unknown range");
    Status(fixture.Meta([&](auto& ledger,const auto&){return ledger.ValidateWrite(overlap);}),ring::Status::Overlap,"Unknown write finishing during sampling must retain conflict");
    Status(fixture.Meta([&](auto& ledger,const auto&){return ledger.DiscardCompleted(overlap);}),ring::Status::Observed,"Discard conflicted returned writer");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(saved,0,1),result);}),ring::Status::Completed,"Disjoint saved version survives unknown cache");
    Require(Filled(result.Bytes(),0x23),"Unknown cache altered disjoint version");
    Status(fixture.Meta([&](auto& ledger,const auto& native){return ledger.Query(native,Read(unknown,4,1),result);}),ring::Status::UnknownWriter,"Unobserved cache cannot create a candidate");
}
int main()try{
    ReversedDisjointCallbacks();CachedOverwriteAndUnknownPending();EqualSamplesCannotHideOverlappingAba();SameGenerationRecoveryAndRetainedBudget();
    ReturnedWriterBeforePeerReservation();
    UnobservedCachedWriterKeepsDisjointVersions();
    std::puts("PASS two real CPU writers, reversed reservation/completion events, cached overwrite, local pending ranges, ABA rejection and same-generation retention; no GPU permission");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"instance_ring_concurrent_test: %s\n",error.what());return 1;}
