#include "Render/StereoDescriptorStore.hpp"
#include <cstdio>
#include <random>
#include <stdexcept>
using namespace cvr::stereo::descriptors;
static void Require(bool v){if(!v)throw std::runtime_error("descriptor store assertion failed");}
int main() try {
    Require(DescriptorMutationObservation(false,true,false)==DescriptorMutationMode::Canonical);
    Require(DescriptorMutationObservation(false,false,true)==DescriptorMutationMode::Canonical);
    Require(DescriptorMutationObservation(false,false,false)==DescriptorMutationMode::Ignore);
    Require(DescriptorMutationObservation(true,true,false)==DescriptorMutationMode::Ignore);
    Require(DescriptorMutationObservation(true,true,true)==DescriptorMutationMode::InvalidateTracked);
    Store scopedInternal;Require(scopedInternal.HeapCreated(0x1000,0x100000,128,32,1));
    Value originalMetadata{};originalMetadata[0]=2;originalMetadata[2]=81;
    scopedInternal.Write(0x1040,32,scopedInternal.Intern(originalMetadata));
    const auto watchedInternal=scopedInternal.Range(0x100040,4);const auto privateBefore=scopedInternal.Stats();
    for(uint32_t i=0;i<2048;++i){
        const uint64_t privateCpu=0x200000ull+uint64_t(i)*4096,privateGpu=0x10000000ull+uint64_t(i)*4096;
        const auto observation=DescriptorMutationObservation(true,true,true);
        if(observation==DescriptorMutationMode::Canonical)Require(scopedInternal.HeapCreated(privateCpu,privateGpu,32,32,100+i));
        Require(observation==DescriptorMutationMode::InvalidateTracked);
        Require(scopedInternal.InvalidateTracked(privateCpu,32,32)==TrackedDescriptorWrite::Ignored);
    }
    Require(scopedInternal.Stats()==privateBefore && !scopedInternal.Overflow() &&
        SameBorrowedRange(watchedInternal,scopedInternal.Range(0x100040,4)));
    // Internal copies to a native heap still erase canonical metadata and stamp
    // the destination, even when the private source heap was never observed.
    Require(scopedInternal.InvalidateTracked(0x1040,1,32)==TrackedDescriptorWrite::Invalidated);
    Require(!scopedInternal.At(0x1040) && !SameBorrowedRange(watchedInternal,scopedInternal.Range(0x100040,4)));
    const auto afterInternal=scopedInternal.Range(0x100040,4);const auto valuesBefore=scopedInternal.Values();
    Require(scopedInternal.InvalidateTracked(0x1040,1,32)==TrackedDescriptorWrite::Invalidated);
    Require(scopedInternal.Values()==valuesBefore && !SameBorrowedRange(afterInternal,scopedInternal.Range(0x100040,4)));
    const auto unchangedInternal=scopedInternal.Range(0x100040,4);
    Require(scopedInternal.InvalidateTracked(0x1200,1,32)==TrackedDescriptorWrite::Invalidated &&
        SameBorrowedRange(unchangedInternal,scopedInternal.Range(0x100040,4)));
    Require(scopedInternal.InvalidateTracked(0,0,0)==TrackedDescriptorWrite::Ignored);
    Store malformedInternal;Require(malformedInternal.HeapCreated(0x1000,0x100000,32,32,2));
    malformedInternal.Write(0x1000,32,malformedInternal.Intern(originalMetadata));
    Require(malformedInternal.InvalidateTracked(0xff0,2,32)==TrackedDescriptorWrite::Rejected &&
        malformedInternal.Stats()[0]==16 && !malformedInternal.At(0x1000) && !malformedInternal.Range(0x100000,1).valid);
    Store misalignedInternal;Require(misalignedInternal.HeapCreated(0x1000,0x100000,32,32,3));
    Require(misalignedInternal.InvalidateTracked(0x1001,1,32)==TrackedDescriptorWrite::Rejected && misalignedInternal.Stats()[0]==16);
    Store nativeCapacity;
    for(uint32_t i=0;i<1024;++i)Require(nativeCapacity.HeapCreated(0x1000000ull+uint64_t(i)*4096,0,1,32,i+1));
    Require(!nativeCapacity.HeapCreated(0x2000000,0,1,32,1025) && nativeCapacity.Stats()[0]==2);
    Require(nativeCapacity.InvalidateTracked(0x1000000,1,32)==TrackedDescriptorWrite::Rejected);

    // Metadata inventory never turns an unknown slot into a typed null or a
    // cookie into a live resource. Production also requires DecodeSrv success.
    const auto inventory=[&](const Value* value,bool decoded=true){BorrowedDescriptorInventory counts;
        const auto cookie=InventoryBorrowedDescriptor(value,decoded,counts);Require(counts.slots==1);
        return std::pair{cookie,counts};};
    Require(inventory(nullptr).second.unknown==1);
    Value unknown{};Require(inventory(&unknown).second.unknown==1);
    unknown[0]=5;Require(inventory(&unknown).second.unknown==1);
    Value nullSrv{};nullSrv[0]=2;nullSrv[4]=28;nullSrv[5]=4;nullSrv[6]=5768;nullSrv[8]=1;
    Require(!inventory(&nullSrv).first && inventory(&nullSrv).second.knownNull==1);
    Require(inventory(&nullSrv,false).second.unknown==1 && !inventory(&nullSrv,false).second.knownNull);
    unknown=nullSrv;unknown[1]=2;Require(inventory(&unknown).second.unknown==1);
    unknown=nullSrv;unknown[2]=73;Require(inventory(&unknown).second.unknown==1);
    unknown=nullSrv;unknown[5]=0;Require(inventory(&unknown).second.unknown==1);
    unknown=nullSrv;unknown[5]=11;unknown[7]=0x10000;
    Require(inventory(&unknown).second.otherKinds==1 && !inventory(&unknown).second.knownNull);
    Value resourceSrv=nullSrv;resourceSrv[1]=5;resourceSrv[2]=77;resourceSrv[13]=3ull|(28ull<<32);
    resourceSrv[14]=64;resourceSrv[15]=32ull|(1ull<<32)|(1ull<<48);resourceSrv[17]=1;
    auto classified=inventory(&resourceSrv);
    Require(classified.first==77 && classified.second.resourceSRV==1 && classified.second.texture2D==1 &&
        !classified.second.texture2DArray && !classified.second.shapeUnknown && !classified.second.trackedCookies);
    auto arraySrv=resourceSrv;arraySrv[5]=5;arraySrv[15]=32ull|(6ull<<32)|(1ull<<48);arraySrv[16]=4;
    classified=inventory(&arraySrv);
    Require(classified.first==77 && classified.second.texture2DArray==1 && !classified.second.texture2D && classified.second.flagsRTDSUAV==1);
    unknown=resourceSrv;unknown[1]=1;Require(inventory(&unknown).second.shapeUnknown==1);
    unknown=resourceSrv;unknown[14]=0;Require(inventory(&unknown).second.shapeUnknown==1);
    unknown=resourceSrv;unknown[2]=0;Require(inventory(&unknown).second.unknown==1);
    unknown=resourceSrv;unknown[1]=16;Require(inventory(&unknown).second.unknown==1);
    for(const uint64_t kind:{1ull,3ull,4ull}){Value other{};other[0]=kind;
        Require(!inventory(&other).first && inventory(&other).second.otherKinds==1);}

    // Exact family1950/1888 active register domains, independent of selector
    // predictions. Unused bindless roots must not enlarge the borrowed ranges.
    const std::vector<BorrowedRootRange> rootRanges{
        {0,2,0,0,4,0,1},{1,2,0,4,1,0,1},{2,2,0,5,2,0,1},
        {13,0,1,0,UINT32_MAX,0,1},{18,2,0,0,4,0,5},{19,2,0,4,1,0,5},
        {21,2,0,7,1,0,5},{22,2,0,8,6,0,5},{23,0,0,0,8,0,5},
        {24,0,0,8,24,0,5},{29,3,0,0,8,0,5},{30,3,0,8,8,0,5},
        {31,0,1,0,UINT32_MAX,0,5},{32,0,2,0,UINT32_MAX,0,5}
    };
    const std::vector<BorrowedShaderUse> uses{
        {2,1,0,1,1},{2,1,0,4,1},{2,1,0,5,1},{2,5,0,1,1},{2,5,0,4,1},
        {2,5,0,7,1},{2,5,0,12,1},{0,5,0,0,7},{0,5,0,8,1},
        {3,5,0,0,2},{3,5,0,11,1},{0,5,1,0,32768}
    };
    const std::vector<BorrowedUsedRange> expectedUses{
        {0,2,1,1,1,false},{1,2,0,1,1,false},{2,2,0,1,1,false},{18,2,1,1,1,false},
        {19,2,0,1,1,false},{21,2,0,1,1,false},{22,2,4,1,1,false},{23,0,0,7,7,false},
        {24,0,0,1,1,false},{29,3,0,2,2,false},{30,3,3,1,1,false},{31,0,0,UINT32_MAX,32768,true}
    };
    std::vector<BorrowedUsedRange> planned;
    Require(PlanBorrowedShaderRanges(36,true,false,rootRanges,uses,planned) && planned==expectedUses);
    const auto rejectMapping=[&](uint32_t count,bool supported,bool direct,std::span<const BorrowedRootRange> roots,
                                 std::span<const BorrowedShaderUse> selected){
        planned=expectedUses;Require(!PlanBorrowedShaderRanges(count,supported,direct,roots,selected,planned) && planned.empty());
    };
    rejectMapping(36,false,false,rootRanges,uses);rejectMapping(36,true,true,rootRanges,uses);
    rejectMapping(0,true,false,rootRanges,uses);rejectMapping(65,true,false,rootRanges,uses);
    rejectMapping(36,true,false,{},uses);rejectMapping(36,true,false,rootRanges,{});
    auto wrongUses=uses;wrongUses[0].visibility=5;
    // PS b1 is valid, but must map to PS p18 rather than a VS descriptor.
    Require(PlanBorrowedShaderRanges(36,true,false,rootRanges,wrongUses,planned) && planned.front().parameter==1);
    wrongUses=uses;wrongUses[0].visibility=2;rejectMapping(36,true,false,rootRanges,wrongUses);
    wrongUses=uses;wrongUses[0].space=2;rejectMapping(36,true,false,rootRanges,wrongUses);
    wrongUses=uses;wrongUses[0].count=4;rejectMapping(36,true,false,rootRanges,wrongUses);
    wrongUses=uses;wrongUses[0].type=4;rejectMapping(36,true,false,rootRanges,wrongUses);
    wrongUses=uses;wrongUses[0].count=0;rejectMapping(36,true,false,rootRanges,wrongUses);
    wrongUses=uses;wrongUses[0].shaderRegister=UINT32_MAX;wrongUses[0].count=2;rejectMapping(36,true,false,rootRanges,wrongUses);
    auto wrongRoots=rootRanges;wrongRoots.push_back(rootRanges[0]);rejectMapping(36,true,false,wrongRoots,uses);
    wrongRoots=rootRanges;wrongRoots[0].visibility=0;rejectMapping(36,true,false,wrongRoots,uses);
    wrongRoots=rootRanges;wrongRoots[0].parameter=36;rejectMapping(36,true,false,wrongRoots,uses);
    wrongRoots=rootRanges;wrongRoots[0].offset=UINT32_MAX;rejectMapping(36,true,false,wrongRoots,uses);
    wrongRoots=rootRanges;wrongRoots[0].count=0;rejectMapping(36,true,false,wrongRoots,uses);
    wrongRoots=rootRanges;wrongRoots[0].type=4;rejectMapping(36,true,false,wrongRoots,uses);
    wrongRoots=rootRanges;wrongRoots[0].visibility=8;rejectMapping(36,true,false,wrongRoots,uses);
    const std::array finiteRoots{BorrowedRootRange{0,0,0,0,8,5,5}};
    const std::array partialUses{BorrowedShaderUse{0,5,0,0,2},BorrowedShaderUse{0,5,0,1,3},BorrowedShaderUse{0,5,0,6,1}};
    Require(PlanBorrowedShaderRanges(1,true,false,finiteRoots,partialUses,planned));
    Require(planned==std::vector<BorrowedUsedRange>{{0,0,5,4,3,false},{0,0,11,1,1,false}});
    const std::array allVisible{BorrowedRootRange{0,0,0,0,8,0,0}};
    const std::array twoStages{BorrowedShaderUse{0,1,0,2,1},BorrowedShaderUse{0,5,0,2,1}};
    Require(PlanBorrowedShaderRanges(1,true,false,allVisible,twoStages,planned) &&
        planned==std::vector<BorrowedUsedRange>{{0,0,2,1,1,false}});
    const std::array overlapRoots{BorrowedRootRange{0,0,0,0,8,0,5},BorrowedRootRange{0,2,0,0,8,0,5}};
    const std::array overlapUses{BorrowedShaderUse{0,5,0,0,1},BorrowedShaderUse{2,5,0,0,1}};
    rejectMapping(1,true,false,overlapRoots,overlapUses);
    const std::array unboundedRoot{BorrowedRootRange{0,0,1,0,UINT32_MAX,9,5}};
    const std::array singleInUnbounded{BorrowedShaderUse{0,5,1,5,1}};
    Require(PlanBorrowedShaderRanges(1,true,false,unboundedRoot,singleInUnbounded,planned) &&
        planned==std::vector<BorrowedUsedRange>{{0,0,9,UINT32_MAX,1,true}});
    const std::array afterUnbounded{BorrowedRootRange{0,0,1,0,UINT32_MAX,0,5},BorrowedRootRange{0,0,2,0,1,10,5}};
    rejectMapping(1,true,false,afterUnbounded,singleInUnbounded);
    const std::array mixedSampler{BorrowedRootRange{0,0,1,0,1,0,5},BorrowedRootRange{0,3,0,0,1,1,5}};
    const std::array mixedUses{BorrowedShaderUse{0,5,1,0,1},BorrowedShaderUse{3,5,0,0,1}};
    rejectMapping(1,true,false,mixedSampler,mixedUses);

    // The runtime's independent pin policy keeps byte versions alive after the
    // ordinary pending-list observer has consumed its entries at submission.
    BorrowedRangeTracking tracking;Require(!tracking.Ready() && !tracking.Pin() && !tracking.MutationBegin());tracking.Ready(true);
    Require(tracking.Ready() && !tracking.VersionsRequired(true) && tracking.VersionsRequired(false));
    Require(tracking.Pin() && tracking.Pin() && tracking.Pins()==2 && tracking.VersionsRequired(true));
    const auto epoch=tracking.Epoch();Require(tracking.Stable(epoch));
    Require(tracking.MutationBegin() && !tracking.Stable(epoch));
    Require(tracking.MutationBegin() && !tracking.Stable(epoch));tracking.MutationEnd();Require(!tracking.Stable(epoch));
    tracking.MutationEnd();Require(tracking.Stable(epoch));
    auto diagnostic=BorrowedTrackingDetails(true,true,3,3,167,0,true,2,0,false);
    Require(diagnostic==std::array<uint64_t,4>{UINT64_MAX,32,uint64_t(167)<<32,uint64_t(2)<<32});
    diagnostic=BorrowedTrackingDetails(true,true,3,3,167,9,true,0,0,false);
    Require(diagnostic[1]==8 && diagnostic[2]==(uint64_t(167)<<32|9) && !diagnostic[3]);
    diagnostic=BorrowedTrackingDetails(true,true,3,3,167,0,true,0,9,false);
    Require(diagnostic[1]==64 && diagnostic[3]==9);
    diagnostic=BorrowedTrackingDetails(false,false,3,4,0,2,false,8,10,true);
    Require(diagnostic[1]==255 && diagnostic[2]==2 && diagnostic[3]==(uint64_t(8)<<32|10));
    Require(BorrowedTrackingDetails(true,true,3,3,1,0,true,0,0,false)[1]==0);
    tracking.Release();Require(tracking.Pins()==1 && tracking.VersionsRequired(true));
    tracking.Invalidate();Require(!tracking.Stable(epoch) && tracking.Stable(tracking.Epoch()));
    tracking.Release();Require(!tracking.Pins() && !tracking.VersionsRequired(true));
    for(uint32_t i=0;i<BorrowedRangeTracking::PinLimit;++i)Require(tracking.Pin());
    Require(!tracking.Pin() && tracking.Pins()==BorrowedRangeTracking::PinLimit);
    for(uint32_t i=0;i<BorrowedRangeTracking::PinLimit;++i)tracking.Release();
    tracking.Ready(false);tracking.Ready(true);Require(!tracking.Ready() && !tracking.Pin() && !tracking.MutationBegin() && !tracking.Stable(tracking.Epoch()));

    BorrowedRangeTracking pinned;Store pinnedStore;
    Require(pinnedStore.HeapCreated(0x710000,0x720000,64,32,950));
    pinnedStore.TrackVersions(pinned.VersionsRequired(true));Require(!pinnedStore.TrackVersions());
    pinned.Ready(true);Require(pinned.Pin());pinnedStore.TrackVersions(pinned.VersionsRequired(true));
    const auto borrowed=pinnedStore.Range(0x720080,4);Require(borrowed.valid && borrowed.version && pinnedStore.TrackVersions());
    pinnedStore.Write(0x710000,32,0);Require(SameBorrowedRange(borrowed,pinnedStore.Range(0x720080,4)));
    Require(pinned.MutationBegin());Require(!pinned.Stable(pinned.Epoch()));
    pinnedStore.Copy(0x710080,0x800000,1,32);pinned.MutationEnd();
    Require(!SameBorrowedRange(borrowed,pinnedStore.Range(0x720080,4)));
    const auto copiedRange=pinnedStore.Range(0x720080,4);
    pinnedStore.HeapRemoved(950);Require(!SameBorrowedRange(copiedRange,pinnedStore.Range(0x720080,4)));
    Require(pinnedStore.HeapCreated(0x710000,0x720000,64,32,951));
    Require(!SameBorrowedRange(copiedRange,pinnedStore.Range(0x720080,4)));
    const auto activeRange=pinnedStore.Range(0x720080,UINT32_MAX);
    Require(activeRange.valid && activeRange.count==60);
    pinned.Release();pinnedStore.TrackVersions(pinned.VersionsRequired(true));Require(!pinnedStore.TrackVersions());
    pinnedStore.Write(0x710080,32,0);Require(pinned.Pin());pinnedStore.TrackVersions(pinned.VersionsRequired(true));
    Require(!SameBorrowedRange(activeRange,pinnedStore.Range(0x720080,UINT32_MAX)));
    const auto invalidated=pinned.Epoch();pinned.Invalidate();Require(!pinned.Stable(invalidated));
    pinnedStore.Write(UINT64_MAX-1,32,0);Require(pinnedStore.Overflow() && !pinnedStore.Range(0x720080,4).valid);pinned.Release();

    Store store;Require(store.HeapCreated(0x1000,0x100000,1024,32,1));
    Value a{},b{};a[0]=b[0]=2;a[2]=11;b[2]=12;
    const auto first=store.Intern(a),second=store.Intern(b);Require(first && second!=first && store.Intern(a)==first);
    Require(!store.At(0x1000));auto old=store.Range(0x100000,8);Require(old.valid);
    store.Write(0x80000,32,first);store.Copy(0x1000,0x80000,1,32);
    auto copied=store.Range(0x100000,8);Require(copied.valid && copied.version>old.version && store.At(0x1000)==first);
    store.Write(0x1200,32,second);Require(store.Range(0x100000,8).version==copied.version);
    store.Copy(0x1000,0x90000,1,32);Require(store.At(0x1000)==0 && store.Range(0x100000,8).version>copied.version);
    Require(store.HeapCreated(0x1000,0x100000,1024,32,2));Require(store.Range(0x100000,8).heap==2 && !store.At(0x1200));
    Require(!store.Range(0x100001,1).valid && !store.Range(0x100000,1025).valid && !store.Range(0,1).valid);
    std::mt19937 random(0x44534352);std::array<uint32_t,1024> expected{};
    // Re-intern after heap reuse: slots own IDs, not external references.
    const auto aa=store.Intern(a),bb=store.Intern(b);store.Write(0x80020,32,aa);store.Write(0x80040,32,bb);
    for(unsigned step=0;step<5000;++step) {
        const uint32_t choices[]{0,aa,bb};const auto index=random()%1024,id=choices[random()%3];store.Write(0x1000+index*32,32,id);expected[index]=id;
        const auto begin=random()%1024,count=1+random()%(1024-begin);
        const auto before=store.Range(0x100000+begin*32,count);
        Require(before.valid);for(unsigned i=begin;i<begin+count;++i)Require(store.At(0x1000+i*32)==expected[i]);
        // A write outside a requested interval never changes its version.
        if(begin){store.Write(0x1000,32,expected[0]);Require(store.Range(0x100000+begin*32,count).version==before.version);}
    }
    store.Copy(0x1020,0x1000,4,32);for(unsigned i=1;i<5;++i)Require(!store.At(0x1000+i*32));
    Require(!store.HeapCreated(UINT64_MAX-8,0x200000,2,32,3));
    Store rolling;Require(rolling.HeapCreated(0x2000,0x200000,512,32,77));
    for(unsigned i=0;i<Store::ValueLimit+1000;++i){Value value{};value[0]=1;value[4]=i;
        const auto id=rolling.Intern(value);Require(id!=0);rolling.Write(0x2000,32,id);}
    Require(!rolling.Overflow() && rolling.Values()<8 && rolling.Stats()[3]==1);
    const auto large=rolling.Range(0x200000,256);Require(large.valid);
    Value later{};later[0]=2;rolling.Write(0x2000+300*32,32,rolling.Intern(later));
    Require(rolling.Range(0x200000,256).version==large.version);
    rolling.Write(0x2020,32,rolling.Intern(later));Require(rolling.Range(0x200000,256).version>large.version);
    Store failed;failed.Write(UINT64_MAX-1,32,0);const auto failedStats=failed.Stats();
    for(unsigned i=0;i<1000;++i){failed.Write(0x1000+i*32,32,0);failed.Copy(0x4000,0x1000,1,32);}
    Require(failed.Stats()==failedStats && !failed.Range(0x1000,1).valid);
    // Independent byte oracle: no alignment assumption, crossing page edges,
    // overlapping writes, exact tails, cached ranges and whole-heap overwrite.
    Store paged;constexpr uint64_t cpu=0x2ffdu,gpu=0x800003u;constexpr unsigned span=32768;
    Require(paged.HeapCreated(cpu,gpu,span,1,123));std::vector<uint64_t> versions(span,1);uint64_t serial=1;
    auto stamp=[&](unsigned offset,unsigned count){paged.Write(cpu+offset,count,0);
        std::fill_n(versions.begin()+offset,count,++serial);};
    for(unsigned trial=0;trial<12000;++trial){
        const unsigned offset=random()%span,count=1+random()%std::min(6000u,span-offset);
        stamp(offset,count);
        const unsigned firstByte=random()%span,bytes=1+random()%(span-firstByte);
        const auto expected=*std::max_element(versions.begin()+firstByte,versions.begin()+firstByte+bytes);
        const auto range=paged.Range(gpu+firstByte,bytes);Require(range.valid && range.version==expected);
        if(trial%137==0){stamp(0,span);Require(paged.Range(gpu,UINT32_MAX).version==serial);}
    }
    Require(!paged.Overflow());
    Store high;constexpr uint64_t highCpu=UINT64_MAX-8192;
    Require(high.HeapCreated(highCpu,0x1000000,8192,1,124));high.Write(highCpu+4090,10,0);
    Require(high.Range(0x1000000+4090,10).version==2 && high.Range(0x1000000+4100,4092).version==1);
    Store lifetimes;Require(lifetimes.HeapCreated(0x2ffd,0,32,32,301));
    Require(lifetimes.HeapCreated(0x3801,0x800000,32,32,302));
    auto value=lifetimes.Intern(a);lifetimes.Write(0x2ffd,32,value);lifetimes.Copy(0x3801,0x2ffd,1,32);
    const auto retained=lifetimes.Range(0x800000,32);lifetimes.HeapRemoved(301);
    Require(!lifetimes.At(0x2ffd) && lifetimes.Get(lifetimes.At(0x3801)) && lifetimes.Range(0x800000,32).version==retained.version);
    Require(lifetimes.HeapCreated(0x3801,0x800000,32,32,303));value=lifetimes.Intern(b);lifetimes.Write(0x3801,32,value);
    lifetimes.HeapRemoved(302);Require(lifetimes.Range(0x800000,32).heap==303 && lifetimes.At(0x3801)==value);
    lifetimes.HeapRemoved(303);Require(!lifetimes.Range(0x800000,32).valid && !lifetimes.At(0x3801) && lifetimes.Stats()[4]==0);
    for(unsigned i=0;i<1500;++i){const auto base=0x1000000ull+i*8192;
        Require(lifetimes.HeapCreated(base,0,32,32,500+i));value=lifetimes.Intern(a);lifetimes.Write(base,32,value);
        lifetimes.HeapRemoved(500+i);Require(!lifetimes.At(base) && !lifetimes.Stats()[1] && !lifetimes.Stats()[4]);}
    Require(!lifetimes.Overflow());
    Store full;constexpr uint32_t population=1049000;Require(full.HeapCreated(0x10000000,0x30000000,population,32,9000));
    const auto shared=full.Intern(a);
    for(uint32_t i=0;i<population;++i)full.Write(0x10000000+uint64_t(i)*32,32,shared);
    Require(!full.Overflow() && full.Stats()[2]==population && full.HeapSlots()==population &&
        full.Range(0x30000000,UINT32_MAX).version==uint64_t(population)+1);
    full.HeapRemoved(9000);Require(!full.Stats()[2] && !full.Stats()[3] && !full.Stats()[4] && !full.HeapSlots());
    // Force a hash collision; descriptor identity still compares all 160 bytes.
    auto collide=[](Value value){constexpr uint64_t prime=0x100000001b3ull,target=0xabcdef0123456789ull;
        uint64_t inverse=1,hash=0xcbf29ce484222325ull;
        for(unsigned i=0;i<6;++i)inverse*=2-prime*inverse;
        for(size_t i=0;i+1<value.size();++i){hash^=value[i];hash*=prime;hash^=hash>>32;}
        value.back()=hash^((target^(target>>32))*inverse);return value;};
    Store collision;const auto ca=collide(a),cb=collide(b);const auto ia=collision.Intern(ca),ib=collision.Intern(cb);
    Require(ia && ib && ia!=ib && collision.Intern(ca)==ia && collision.Intern(cb)==ib);
    collision.Write(0x1000,32,ia);collision.Write(0x2000,32,ib);collision.Write(0x1000,32,0);
    Require(collision.Get(ib) && *collision.Get(ib)==cb && collision.Intern(cb)==ib && collision.Intern(ca)!=ib);
    // The representation may change while descriptors remain live. Values,
    // references, unknown addresses, cookies and range versions must not change.
    Store dense,sparse(false);
    const uint64_t bases[]{0x200003,0x400009,0x80000f},gpus[]{0x20000003,0x40000009,0x8000000f};
    const uint32_t strides[]{24,32,40};
    auto create=[&](unsigned h,uint64_t cookie){
        Require(dense.HeapCreated(bases[h],gpus[h],257,strides[h],cookie));
        Require(sparse.HeapCreated(bases[h],gpus[h],257,strides[h],cookie));
    };
    for(unsigned h=0;h<3;++h)create(h,10000+h);
    for(unsigned iteration=0;iteration<20000;++iteration){
        const auto h=random()%3,index=random()%257;const auto address=bases[h]+uint64_t(index)*strides[h];
        if(iteration%401==0){dense.Dense(!dense.Dense());}
        if(iteration%997==0){dense.HeapRemoved(10000+h);sparse.HeapRemoved(10000+h);create(h,10000+h);}
        if(iteration%3){Value value{};value[0]=random()%5;value[4]=random()%64;value[5]=(1+random()%4)*256;
            dense.Write(address,strides[h],dense.Intern(value));sparse.Write(address,strides[h],sparse.Intern(value));
        }else {
            const auto source=(iteration%7)?bases[(h+1)%3]+uint64_t(random()%257)*strides[(h+1)%3]:0xf00000;
            dense.Copy(address,source,1,strides[h]);sparse.Copy(address,source,1,strides[h]);
        }
        if(iteration%127==0){const auto address=0xf00001+uint64_t(iteration%11)*31;
            dense.Write(address,31,dense.Intern(a));sparse.Write(address,31,sparse.Intern(a));}
        for(unsigned query=0;query<3;++query){const auto slot=random()%257,count=1+random()%(257-slot);
            const auto x=dense.Range(gpus[query]+uint64_t(slot)*strides[query],count),y=sparse.Range(gpus[query]+uint64_t(slot)*strides[query],count);
            Require(x.valid==y.valid && x.heap==y.heap && x.cpu==y.cpu && x.gpu==y.gpu && x.version==y.version && x.count==y.count && x.stride==y.stride);
            const auto* left=dense.Get(dense.At(bases[query]+uint64_t(slot)*strides[query]));
            const auto* right=sparse.Get(sparse.At(bases[query]+uint64_t(slot)*strides[query]));
            Require(bool(left)==bool(right) && (!left || *left==*right));
        }
        Require(dense.Stats()==sparse.Stats());
    }
    const auto before=dense.Stats();dense.Dense(false);dense.Dense(true);Require(dense.Stats()==before);
    Store live,reference;
    Require(live.HeapCreated(0x900003,0x90000003,257,32,10001));
    Require(reference.HeapCreated(0x900003,0x90000003,257,32,10001));
    Value cbv{};cbv[0]=1;cbv[4]=0x10000;cbv[5]=256;
    live.WriteValue(0x900003,32,cbv);const auto owned=live.At(0x900003);
    live.Copy(0x900023,0x900003,1,32);cbv[4]+=256;live.WriteValue(0x900003,32,cbv);
    Require(live.Get(owned) && (*live.Get(owned))[4]==0x10000 && live.At(0x900003)!=owned);
    const auto exclusive=live.At(0x900003);cbv[4]+=256;live.WriteValue(0x900003,32,cbv);
    Require(live.At(0x900003)==exclusive && (*live.Get(owned))[4]==0x10000);
    live.Copy(0x900043,0x900023,1,32);live.WriteValue(0x900023,32,Value{});
    Require(live.Get(owned) && live.At(0x900043)==owned);
    live.HeapRemoved(10001);Require(!live.Stats()[2]);
    Require(live.HeapCreated(0x900003,0x90000003,257,32,10001));
    uint64_t lastGeneration{};
    for(unsigned iteration=0;iteration<20000;++iteration){
        if(iteration%503==0){
            live.TrackVersions(!live.TrackVersions());
            if(live.TrackVersions()){
                const auto version=live.Range(0x90000003,257).version;
                Require(version>lastGeneration);lastGeneration=version;
            }
        }
        if(iteration%601==0)live.Dense(!live.Dense());
        const auto index=random()%257;const auto address=0x900003+uint64_t(index)*32;
        if(iteration%3){Value value{};value[0]=random()%5;value[4]=random()%16384*256;value[5]=(1+random()%4)*256;
            live.WriteValue(address,32,value);reference.Write(address,32,reference.Intern(value));
        }else{
            const auto source=iteration%7?0x900003+uint64_t(random()%257)*32:0xf00000;
            live.Copy(address,source,1,32);reference.Copy(address,source,1,32);
        }
        for(unsigned query=0;query<4;++query){const auto slot=random()%257,count=1+random()%(257-slot);
            const auto queryCpu=0x900003+uint64_t(slot)*32;
            const auto* x=live.Get(live.At(queryCpu));const auto* y=reference.Get(reference.At(queryCpu));
            Require(bool(x)==bool(y) && (!x || *x==*y));
            const auto range=live.Range(0x90000003+uint64_t(slot)*32,count);Require(range.valid && range.heap==10001);
            if(!live.TrackVersions())Require(!range.version && !live.Stats()[4]);
        }
    }
    live.TrackVersions(false);live.WriteValue(0x900003+32,32,cbv);const auto preserved=*live.Get(live.At(0x900003+32));
    live.TrackVersions(true);const auto restored=live.Range(0x90000003,256);
    Require(restored.version>lastGeneration && *live.Get(live.At(0x900003+32))==preserved);
    live.WriteValue(0x900003+256*32,32,cbv);Require(live.Range(0x90000003,256).version==restored.version);
    live.WriteValue(0x900003,32,cbv);Require(live.Range(0x90000003,256).version>restored.version);
    Require(!live.Overflow());live.HeapRemoved(10001);Require(!live.Stats()[2] && !live.Stats()[3]);
    static std::vector<Value> retired;
    Store lifetime;lifetime.SetValueRetirement([](const Value& value){retired.push_back(value);});
    Require(lifetime.HeapCreated(0xF00000,0xFF000000,3,32,10101));
    Value origin{};origin[0]=1;origin[2]=111;origin[3]=101;origin[4]=0x10000;origin[5]=256;
    lifetime.WriteValue(0xF00000,32,origin);lifetime.Copy(0xF00020,0xF00000,1,32);
    origin[3]=102;lifetime.WriteValue(0xF00000,32,origin);Require(retired.empty());
    origin[3]=103;lifetime.WriteValue(0xF00000,32,origin);Require(retired.size()==1 && retired[0][3]==102);
    lifetime.WriteValue(0xF00000,32,origin);Require(retired.size()==1);
    lifetime.Copy(0xF00040,0xF00020,1,32);lifetime.WriteValue(0xF00020,32,Value{});Require(retired.size()==1);
    lifetime.HeapRemoved(10101);Require(retired.size()==3 && retired[1][3]==103 && retired[2][3]==101);
    retired.clear();Require(lifetime.HeapCreated(0xF00000,0xFF000000,3,32,10102));
    lifetime.WriteValue(0xF00000,32,origin);lifetime.WriteValue(0xF00020,32,origin);
    Require(lifetime.At(0xF00000)==lifetime.At(0xF00020));
    lifetime.WriteValue(0xF00000,32,Value{});Require(retired.empty());
    lifetime.WriteValue(0xF00020,32,Value{});Require(retired.size()==1 && retired[0][3]==103);
    std::puts("PASS descriptor identity, copy/invalidation, heap reuse, range versions, streaming origin retirement and 5000 update/query cases");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
