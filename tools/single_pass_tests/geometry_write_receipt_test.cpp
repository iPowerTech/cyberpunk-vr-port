#include "Render/GeometryWriteReceipt.hpp"
#include <array>
#include <cstdio>
#include <initializer_list>
#include <source_location>
#include <stdexcept>
#include <string>

using Ledger=cvr::stereo::geometry::GeometryWriteReceipt;
using Status=Ledger::Status;
static void Require(bool value,const std::source_location at=std::source_location::current()){
    if(!value)throw std::runtime_error("geometry write receipt assertion at line "+std::to_string(at.line()));
}
static Ledger::Range Buffer(uint64_t id=1,uint64_t offset=100,uint64_t bytes=16){return {id,offset,bytes,{},true};}
static Ledger::Range Placed(uint64_t id,uint64_t offset,uint64_t bytes,uint64_t heap,uint64_t base,uint64_t allocationBytes){
    return {id,offset,bytes,{heap,base,allocationBytes},false};
}
static void Submit(Ledger& ledger,uint64_t queue,std::initializer_list<uint64_t> lists){
    Require(ledger.Submit(queue,{lists.begin(),lists.size()}));
}
static void SubmitSeparate(Ledger& ledger,uint64_t queue,std::initializer_list<uint64_t> lists){
    for(const auto list:lists)Submit(ledger,queue,{list});
}
struct Pair {
    Ledger ledger;Ledger::Position main,peer;Ledger::Receipt receipt;
    explicit Pair(Ledger::Range range=Buffer()){
        ledger.Reset(1);ledger.Reset(2);main=ledger.Mark(1);peer=ledger.Mark(2);receipt=ledger.Capture(main,range);
    }
    Ledger::Position Writer(){ledger.Reset(3);return ledger.Mark(3);}
    void SubmitPair(){SubmitSeparate(ledger,10,{1,2});}
    Status Check()const{return ledger.Check(receipt,peer);}
};
int main()try{
    {
        Pair pair;Require(pair.Check()==Status::Pending);pair.SubmitPair();Require(pair.Check()==Status::Unchanged);
        Require(pair.ledger.Check({},pair.peer)==Status::MissingProvenance);
        Require(pair.ledger.Check(pair.receipt,{})==Status::MissingProvenance);
    }
    // Writes can be recorded before MAIN yet submitted between its two reads.
    for(const auto where:{0,1,2}){
        Pair pair;Require(pair.ledger.Write(pair.Writer(),Buffer()));
        if(where==0)SubmitSeparate(pair.ledger,10,{3,1,2});
        if(where==1)SubmitSeparate(pair.ledger,10,{1,3,2});
        if(where==2)SubmitSeparate(pair.ledger,10,{1,2,3});
        Require(pair.Check()==(where==1?Status::IntersectingWrite:Status::Unchanged));
    }
    for(const auto write:{Buffer(1,84,16),Buffer(1,116,16),Buffer(2,100,16)}){
        Pair pair;Require(pair.ledger.Write(pair.Writer(),write));pair.SubmitPair();
        Require(pair.Check()==Status::Unchanged); // Even an unsubmitted disjoint writer is irrelevant.
    }
    {
        Pair pair;Submit(pair.ledger,10,{1,2});Require(pair.Check()==Status::Incomparable);
    }
    for(const bool sharesMain:{false,true}){
        Pair pair;Require(pair.ledger.Write(pair.Writer(),Buffer()));
        if(sharesMain){Submit(pair.ledger,10,{3,1});Submit(pair.ledger,10,{2});}
        else {Submit(pair.ledger,10,{1});Submit(pair.ledger,10,{2,3});}
        Require(pair.Check()==Status::Incomparable);
    }
    {
        Pair pair;Require(pair.ledger.Write(pair.Writer(),Buffer(1,116,16)));
        Submit(pair.ledger,10,{3,1});Submit(pair.ledger,10,{2});Require(pair.Check()==Status::Unchanged);
    }
    {
        Pair pair;Require(pair.ledger.Write(pair.Writer(),Buffer(1,115,1)));pair.SubmitPair();
        Require(pair.Check()==Status::Pending);Submit(pair.ledger,20,{3});Require(pair.Check()==Status::Incomparable);
    }
    for(const bool intervening:{false,true}){
        Ledger ledger;ledger.Reset(1);const auto before=ledger.Mark(1),main=ledger.Mark(1),middle=ledger.Mark(1),peer=ledger.Mark(1),after=ledger.Mark(1);
        Require(ledger.Write(before,Buffer()) && ledger.Write(after,Buffer()));
        if(intervening)Require(ledger.Write(middle,Buffer()));
        const auto receipt=ledger.Capture(main,Buffer());Submit(ledger,10,{1});
        Require(ledger.Check(receipt,peer)==(intervening?Status::IntersectingWrite:Status::Unchanged));
    }
    {
        Pair pair;Submit(pair.ledger,10,{1});Submit(pair.ledger,20,{2});Require(pair.Check()==Status::Incomparable);
    }
    {
        Pair pair;pair.ledger.Fence(80,0);Submit(pair.ledger,10,{1});pair.ledger.Signal(10,80,1);
        pair.ledger.Wait(20,80,1);Submit(pair.ledger,20,{2});Require(pair.Check()==Status::Unchanged);
    }
    {
        Pair pair;Require(pair.ledger.Write(pair.Writer(),Buffer()));pair.ledger.Fence(80,0);
        Submit(pair.ledger,20,{3});pair.ledger.Signal(20,80,1);pair.ledger.Wait(10,80,1);pair.SubmitPair();
        Require(pair.Check()==Status::Unchanged);
    }
    {
        Pair pair;Require(pair.ledger.Write(pair.Writer(),Buffer()));SubmitSeparate(pair.ledger,10,{3,1});SubmitSeparate(pair.ledger,10,{3,2});
        Require(pair.Check()==Status::Replayed);
    }
    {
        Pair pair;pair.SubmitPair();Submit(pair.ledger,10,{1});Require(pair.Check()==Status::Replayed);
    }
    {
        Pair pair;Require(pair.ledger.Write(pair.Writer(),Buffer()));pair.ledger.Reset(3);pair.SubmitPair();
        Require(pair.Check()==Status::Unchanged);
    }
    {
        Pair pair;pair.ledger.Reset(1);pair.SubmitPair();Require(pair.Check()==Status::Abandoned);
    }
    for(const bool before:{false,true}){
        Pair pair;SubmitSeparate(pair.ledger,10,before?std::initializer_list<uint64_t>{99,1,2}:std::initializer_list<uint64_t>{1,99,2});
        Require(pair.Check()==(before?Status::Unchanged:Status::MissingProvenance));
    }
    const auto placed=Placed(1,64,16,7,1000,256);
    for(const auto write:{Placed(2,40,8,7,1024,128),Placed(2,100,8,7,1024,128),Placed(2,40,8,8,1024,128)}){
        Pair pair(placed);Require(pair.ledger.Write(pair.Writer(),write));SubmitSeparate(pair.ledger,10,{1,3,2});
        const bool overlap=write.offset==40 && write.allocation.heap==7;
        Require(pair.Check()==(overlap?Status::Aliased:Status::Unchanged));
    }
    for(const auto alias:{Ledger::MemorySpan{},Ledger::MemorySpan{7,1064,1},Ledger::MemorySpan{7,1080,16},Ledger::MemorySpan{8,1064,1}}){
        Pair pair(placed);Require(pair.ledger.Alias(pair.Writer(),alias));SubmitSeparate(pair.ledger,10,{1,3,2});
        const bool overlap=!alias.heap || (alias.heap==7 && alias.offset==1064);
        Require(pair.Check()==(overlap?Status::Aliased:Status::Unchanged));
    }
    {
        Pair pair;Require(pair.ledger.Alias(pair.Writer()));SubmitSeparate(pair.ledger,10,{1,3,2});Require(pair.Check()==Status::Aliased);
    }
    {
        Pair pair(placed);Require(pair.ledger.Write(pair.Writer(),{2,0,16,{},false}));SubmitSeparate(pair.ledger,10,{1,3,2});
        Require(pair.Check()==Status::MissingProvenance);
    }
    {
        Pair pair(placed);Require(pair.ledger.Alias(pair.Writer()));SubmitSeparate(pair.ledger,10,{3,1,2});Require(pair.Check()==Status::Unchanged);
    }
    // A same-queue MAIN predecessor must not hide a separate future wait branch.
    {
        Pair pair;pair.ledger.Fence(80,0);pair.ledger.Fence(81,0);Submit(pair.ledger,10,{1});pair.ledger.Signal(10,80,1);
        pair.ledger.Wait(10,81,1);Submit(pair.ledger,10,{2});Require(pair.Check()==Status::Pending);
        Require(pair.ledger.Write(pair.Writer(),Buffer()));pair.ledger.Wait(20,80,1);Submit(pair.ledger,20,{3});pair.ledger.Signal(20,81,1);
        Require(pair.Check()==Status::IntersectingWrite);
    }
    {
        Pair pair;pair.ledger.Fence(80,0);pair.ledger.Wait(10,80,1);pair.ledger.Signal(20,80,2);pair.SubmitPair();
        Require(pair.Check()==Status::Pending);pair.ledger.Progress(80,2);Require(pair.Check()==Status::Unchanged);
        pair.ledger.InvalidateFence(80);Require(pair.Check()==Status::MissingProvenance);
    }
    {
        Pair pair;pair.ledger.Fence(80,5);pair.ledger.Wait(10,80,5);pair.SubmitPair();Require(pair.Check()==Status::Unchanged);
    }
    {
        Pair pair;pair.ledger.Fence(80,0);pair.ledger.Wait(20,80,1);pair.SubmitPair();Require(pair.Check()==Status::Unchanged);
    }
    {
        Pair pair;pair.ledger.Wait(10,80,1);Require(pair.Check()==Status::MissingProvenance);
    }
    {
        Pair pair;pair.ledger.Fence(80,0);Submit(pair.ledger,10,{1});pair.ledger.Signal(10,80,2);
        pair.ledger.Wait(20,80,2);Submit(pair.ledger,20,{2});Require(pair.Check()==Status::Unchanged);
        pair.ledger.Signal(10,80,1);Require(pair.Check()==Status::MissingProvenance);
    }
    {
        Pair pair;Require(pair.ledger.UnknownWrite(pair.Writer()));SubmitSeparate(pair.ledger,10,{1,3,2});
        Require(pair.Check()==Status::MissingProvenance);
    }
    {
        Pair pair;pair.ledger.Fence(80,0);pair.ledger.Fence(81,0);pair.ledger.Wait(10,80,1);Submit(pair.ledger,10,{1});
        pair.ledger.Signal(10,81,1);pair.ledger.Wait(20,81,1);Submit(pair.ledger,20,{2});pair.ledger.Signal(20,80,1);
        Require(pair.Check()==Status::Incomparable);
    }
    {
        Pair pair;pair.SubmitPair();const auto receipt=pair.receipt;const auto main=pair.main,peer=pair.peer;
        pair.ledger.Clear();pair.ledger.Reset(1);pair.ledger.Reset(2);pair.ledger.Mark(1);pair.ledger.Mark(2);
        Require(pair.ledger.Check(receipt,peer)==Status::MissingProvenance);
        Require(!pair.ledger.Capture(main,Buffer()).valid);
    }
    for(const auto range:{Ledger::Range{},Buffer(1,UINT64_MAX,1),Ledger::Range{1,0,16,{},false},Placed(1,250,16,7,1000,256)}){
        Pair pair(range);Require(pair.Check()==Status::MissingProvenance);
    }
    {
        Pair pair;Require(!pair.ledger.Write({},Buffer()));Require(pair.Check()==Status::MissingProvenance);
    }
    {
        Pair pair;for(uint64_t i=0;i<Ledger::Capacity;++i)pair.ledger.Reset(100+i);
        Require(pair.Check()==Status::Overflow);
    }
    std::puts("PASS metadata-only geometry receipts, actual submission order, ranged/aliased writes, future waits, reset/replay and bounded failure");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
