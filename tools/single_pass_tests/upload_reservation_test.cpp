#include "Render/NativeUploadReservation.hpp"
#include <cstdio>
#include <stdexcept>

using namespace cvr::stereo::upload_reservation;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Witness Cas(uint64_t oldHead,uint64_t begin,uint64_t bytes=256,uint64_t capacity=4096){
    return {0x1000,oldHead,begin,begin+bytes,bytes,capacity,0x2000,0x1F28C7,0x246};
}
void BodyGuard(){
    constexpr char hex[]=
        "48895c241855565741544155415641574883ec40498bf94c8bea488bd94d85c9740441c60100"
        "488b6908498d40ff48898424800000004c8be049f7d44c8b0b4a8d0c084923cc4c8b431033d2"
        "488bc149f7f04a8d042a493bc0774b4a8d1429488bc2482bc5493bc00f87c4177801498bc1"
        "f0480fb113752433d2488bc148f77310488bc2488b9c24900000004883c440415f415e415d415c"
        "5f5e5dc3488b842480000000eb96498bc0482bc24803c8ebaa";
    static_assert(sizeof(hex)==AllocatorBytes*2+1);
    std::array<uint8_t,AllocatorBytes> body{};
    const auto nibble=[](char c){return uint8_t(c>='a'?c-'a'+10:c-'0');};
    for(size_t i=0;i<body.size();++i)body[i]=uint8_t((nibble(hex[i*2])<<4)|nibble(hex[i*2+1]));
    Require(Fnv(body.data(),body.size())==AllocatorFnv,"exact 177-byte allocator guard");
    body[WitnessRva-AllocatorRva]^=1;
    Require(Fnv(body.data(),body.size())!=AllocatorFnv,"changed witness rejected by whole-body guard");
}
}
int main()try{
    Require(ValidXstate(7,0xE7,832) && ValidXstate(0xE7,0xE7,2688),"all enabled AVX/AVX512 state fits the fixed bound");
    Require(!ValidXstate(1,7,832) && !ValidXstate(0x207,7,2048) && !ValidXstate(7,7,575) &&
        !ValidXstate(7,7,MaxXstateBytes+1),"unsupported or oversized enabled state cannot be omitted");
    BodyGuard();Ledger<8,2> ledger(7);Row row;Reservation receipt;Range range;
    auto failed=Cas(0,0);failed.flags&=~0x40ull;
    Require(ledger.Observe(failed,9,row)==Status::Ignored && !ledger.Count() && !row.sequence,"failed CAS never creates a receipt");
    Require(ledger.Observe(Cas(0,0),9,row)==Status::UnknownGeneration && row.oldHead==0 && row.absoluteEnd==256 && row.thread==9,
        "successful CAS retains exact unknown-generation witness");
    Require(ledger.Find(row.sequence,receipt)==Status::UnknownGeneration && !receipt.sequence,"unknown generation never validates");
    Require(ledger.BeginGeneration(0x1000,11,4096),"explicit lifecycle generation");
    Require(ledger.Find(1,receipt)==Status::UnknownGeneration,"later lifecycle cannot promote an earlier reservation");
    Require(ledger.Observe(Cas(4000,4096,512),10,row)==Status::Captured && row.physicalOffset==0 && row.arenaGeneration==11,
        "wrap padding retains absolute range rather than physical lookup");
    Require(ledger.Find(row.sequence,receipt)==Status::Captured,"known exact reservation");
    Require(ledger.ValidateRange(receipt,256,256,256,144,4096,range)==Status::Captured && range.absoluteBegin==4352 &&
        range.absoluteEnd==4608 && range.logicalBytes==144,"bounded slab-local slice");
    Require(ledger.ValidateRange(receipt,256,0,256,144,4096,range)==Status::RangeMismatch,"physical offset must equal absolute modulo capacity");
    Require(ledger.ValidateRange(receipt,257,257,256,144,4096,range)==Status::RangeMismatch,"padded range cannot escape reservation");
    Require(ledger.ValidateRange(receipt,256,256,256,257,4096,range)==Status::RangeMismatch,"logical bytes cannot exceed padded envelope");
    Require(ledger.ValidateRange(receipt,256,256,256,144,511,range)==Status::RangeMismatch,"padded envelope must fit backing bytes");
    auto forged=receipt;++forged.arenaGeneration;
    Require(ledger.ValidateRange(forged,0,0,256,144,4096,range)==Status::GenerationMismatch,"forged generation rejected");
    // Callbacks may arrive in the reverse order of concurrent successful CASes.
    Require(ledger.Observe(Cas(4864,4864),10,row)==Status::Captured && ledger.Observe(Cas(4608,4608),11,row)==Status::Captured,
        "callback order never infers allocator head history");
    Require(ledger.BeginGeneration(0x1000,12,4096),"new lifecycle generation");
    Require(ledger.Find(receipt.sequence,forged)==Status::GenerationMismatch,"prior generation receipt invalidated");
    Require(!ledger.BeginGeneration(0x1000,12,4096),"duplicate generation quarantined");
    Require(ledger.Observe(Cas(5120,5120),10,row)==Status::Quarantined,"quarantine remains explicit");
    ledger.Reset(8);Require(!ledger.Count() && !ledger.Exhausted(),"bounded explicit observer reset");
    Require(ledger.BeginGeneration(0x1000,1,4096),"new observer generation");
    auto invalid=Cas(UINT64_MAX-100,UINT64_MAX-100);invalid.absoluteEnd=155;
    Require(ledger.Observe(invalid,10,row)==Status::InvalidWitness,"absolute addition overflow rejected");
    Require(ledger.Observe(Cas(0,0),10,row)==Status::Quarantined,"invalid tuple quarantines ring");
    Ledger<2,1> bounded;Require(bounded.BeginGeneration(0x1000,1,4096),"bounded generation");
    Require(bounded.Observe(Cas(0,0),1,row)==Status::Captured,"bounded first witness");const auto first=row.sequence;
    Require(bounded.Observe(Cas(256,256),1,row)==Status::Captured,"bounded second witness");
    Require(bounded.Observe(Cas(512,512),1,row)==Status::Capacity && bounded.Exhausted() && bounded.Count()==2,"rows never evicted on overflow");
    Require(bounded.Find(first,receipt)==Status::Quarantined,"overflow invalidates earlier structural receipts");
    Ledger<4,1> ringLimit;Require(ringLimit.BeginGeneration(0x1000,1,4096),"single-ring generation");
    auto other=Cas(0,0);other.ring=0x3000;
    Require(ringLimit.Observe(other,1,row)==Status::Capacity && ringLimit.Exhausted(),"ring inventory overflow quarantined");
    Ledger<4,1> changed;Require(changed.BeginGeneration(0x1000,1,4096),"capacity-change generation");
    Require(changed.Observe(Cas(0,0,256,8192),1,row)==Status::Quarantined,"capacity change requires a new exact lifecycle");
    Ledger<4,1> raced;Require(raced.BeginGeneration(0x1000,1,4096),"capacity-race generation");
    Require(raced.Observe(Cas(0,0),1,row,false)==Status::InvalidWitness,"ring capacity must still equal the allocator's saved R8");
    Ledger<4,1> recycled(1);Require(recycled.BeginGeneration(0x1000,1,4096),"recycled observer generation");
    Require(recycled.Observe(Cas(0,0),1,row)==Status::Captured && recycled.Find(row.sequence,receipt)==Status::Captured,"pre-reset receipt");
    recycled.Reset(2);Require(recycled.BeginGeneration(0x1000,1,4096),"post-reset generation");
    Require(recycled.Observe(Cas(0,0),1,row)==Status::Captured &&
        recycled.ValidateRange(receipt,0,0,256,144,4096,range)==Status::GenerationMismatch,"observer reset cannot revive identical absolute tuple");
    recycled.EndGeneration(0x1000);Require(recycled.Find(row.sequence,forged)==Status::Quarantined,"arena destruction closes its observed generation");
    Require(!ValidWitness(Cas(0,4096,256)) && !ValidWitness(Cas(0,3840,512)) && !ValidWitness(Cas(1,0)),
        "oversized head advance, physical crossing, and backwards start rejected");
    std::puts("PASS exact allocator guard, successful CAS-only absolute receipts, unknown generations, range validation, and bounded quarantine");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
