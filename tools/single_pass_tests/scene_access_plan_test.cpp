#include "Render/SceneAccessPlan.hpp"
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace cvr::stereo::scene_access;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Resource Buffer(uint64_t id=1,uint64_t bytes=4096){
    return {0x10000+id*0x100,id,{0,0,0,AllocationKind::Committed},Dimension::Buffer,bytes,1};
}
Resource Texture(uint64_t id=1,uint32_t subresources=8){
    auto value=Buffer(id);value.dimension=Dimension::Texture;value.bytes=0;value.subresources=subresources;return value;
}
Resource Placed(Resource value,uint64_t heap=0x50000,uint64_t offset=0,uint64_t bytes=8192){
    value.allocation={heap,offset,bytes,AllocationKind::Placed};return value;
}
Range Bytes(uint64_t offset=0,uint64_t bytes=64){return {RangeKind::BufferBytes,offset,bytes,0,0};}
Range Subresources(uint32_t first=0,uint32_t count=1){return {RangeKind::TextureSubresources,0,0,first,count};}
Range Whole(){return {RangeKind::WholeTexture};}
struct Fixture {
    std::array<Recording,2> recordings{{{0x1000,1,1,0x2000,1,0,11,12,13},{0x3000,2,2,0x4000,0,1,11,12,13}}};
    std::array<std::vector<Event>,2> events;
    Event& Add(uint32_t side,Resource resource,Kind kind,Range range=Bytes(),Usage usage=Usage::Other){
        events.at(side).push_back({recordings.at(side),10+events.at(side).size(),kind,usage,resource,range});
        return events.at(side).back();
    }
    std::array<Section,2> Sections()const{
        return {{{recordings[0],events[0],{RequiredCoverage,uint32_t(events[0].size()),10,true,true}},
                 {recordings[1],events[1],{RequiredCoverage,uint32_t(events[1].size()),10,true,true}}}};
    }
    Analysis Run()const{const auto sections=Sections();return Analyze(sections[0],sections[1]);}
};
void Safe(const Analysis& result){
    Require(result.Ready() && result.failure==Failure::None && result.completeEvidence && result.noObservedConflicts &&
        !result.conflicts && !result.hazards && !result.ExecutionPermitted(),"Safe observations failed or granted execution");
}
void Conflict(const Analysis& result,uint32_t hazards){
    Require(!result.Ready() && result.failure==Failure::Conflict && result.completeEvidence && !result.noObservedConflicts &&
        result.conflicts && result.hazards==hazards && result.peerEvent!=NoEvent && result.mainEvent!=NoEvent &&
        !result.ExecutionPermitted(),"Observed commute conflict escaped rejection");
}
void Positive(){
    Fixture fixture;Safe(fixture.Run());
    fixture.Add(0,Buffer(),Kind::Read,Bytes(),Usage::Constant);fixture.Add(1,Buffer(),Kind::Read,Bytes(),Usage::Constant);
    auto result=fixture.Run();Safe(result);Require(result.resources==1 && !result.comparisons && result.reads==std::array<uint64_t,2>{1,1},"Shared-read accounting failed");
    fixture.events[0][0].kind=Kind::Write;fixture.events[1][0].range=Bytes(64,64);Safe(fixture.Run());
    fixture.events[1][0].kind=Kind::Write;Safe(fixture.Run());
    fixture.events[1][0].range=Bytes(4095,1);Safe(fixture.Run());
    fixture.events[0][0].range=Bytes(4094,1);Safe(fixture.Run());
    fixture.events[1][0].range=Bytes(4094,2);Conflict(fixture.Run(),MainWritesPeerWrites);
    fixture=Fixture{};fixture.Add(0,Buffer(1),Kind::Write);fixture.Add(1,Buffer(2),Kind::ReadWrite);Safe(fixture.Run());
    fixture.events[1][0].resource=Placed(Buffer(2));Safe(fixture.Run());
    fixture.events[0][0].resource=Placed(Buffer(1),0x60000);Safe(fixture.Run());
    fixture.events[0][0].resource=Placed(Buffer(1),0x50000,8192);Safe(fixture.Run());
    fixture.events[0][0].resource=Placed(Buffer(1),0x50000,8191);Conflict(fixture.Run(),MainWritesPeerWrites|PeerWritesMainReads);
    fixture=Fixture{};fixture.Add(0,Texture(),Kind::Write,Subresources(0,2));fixture.Add(1,Texture(),Kind::Read,Subresources(2,6));Safe(fixture.Run());
    fixture.events[1][0].range=Subresources(1,2);Conflict(fixture.Run(),PeerWritesMainReads);
    fixture.events[1][0].range=Whole();Conflict(fixture.Run(),PeerWritesMainReads);
    fixture.events[0][0].kind=Kind::Read;Safe(fixture.Run());
    fixture.events[1][0].kind=Kind::Write;Conflict(fixture.Run(),MainWritesPeerReads);
    fixture.events[0][0].resource=Texture(2);Safe(fixture.Run());
    // Different aliases cannot use disjoint logical subresources as a proof.
    fixture.events[0][0].resource=Placed(Texture(2));fixture.events[0][0].range=Subresources(0);
    fixture.events[1][0].resource=Placed(Texture(1));fixture.events[1][0].range=Subresources(7);
    Conflict(fixture.Run(),MainWritesPeerReads);
    // Likewise, physical aliasing between different buffer resources remains
    // conservative even when the logical access byte ranges are disjoint.
    fixture=Fixture{};fixture.Add(0,Placed(Buffer(1)),Kind::Read,Bytes(0));
    fixture.Add(1,Placed(Buffer(2)),Kind::Write,Bytes(2048));Conflict(fixture.Run(),MainWritesPeerReads);
    fixture.events[0][0].resource=Placed(Texture(3));fixture.events[0][0].range=Subresources(3);
    Conflict(fixture.Run(),MainWritesPeerReads);
}
void DirectionsAndUsage(){
    for(const auto peerKind:{Kind::Read,Kind::Write,Kind::ReadWrite})for(const auto mainKind:{Kind::Read,Kind::Write,Kind::ReadWrite}){
        Fixture fixture;fixture.Add(0,Buffer(),peerKind);fixture.Add(1,Buffer(),mainKind);
        uint32_t hazards{};
        if(mainKind!=Kind::Read && peerKind!=Kind::Write)hazards|=MainWritesPeerReads;
        if(mainKind!=Kind::Read && peerKind!=Kind::Read)hazards|=MainWritesPeerWrites;
        if(peerKind!=Kind::Read && mainKind!=Kind::Write)hazards|=PeerWritesMainReads;
        const auto result=fixture.Run();if(hazards)Conflict(result,hazards);else Safe(result);
        Require(result.conflicts==(hazards?1u:0u),"Pair counted more than once");
    }
    Fixture fixture;
    fixture.Add(0,Buffer(1),Kind::ReadWrite,Bytes(),Usage::ShaderUav);
    fixture.Add(0,Buffer(2),Kind::ReadWrite,Bytes(),Usage::Counter);
    fixture.Add(0,Buffer(3),Kind::Read,Bytes(20,20),Usage::Indirect);
    fixture.Add(1,Buffer(1),Kind::Read,Bytes(),Usage::ShaderRead);
    fixture.Add(1,Buffer(2),Kind::Write,Bytes(),Usage::Counter);
    fixture.Add(1,Buffer(3),Kind::Write,Bytes(36,4),Usage::Copy);
    auto result=fixture.Run();Conflict(result,MainWritesPeerReads|MainWritesPeerWrites|PeerWritesMainReads);
    Require(result.conflicts==3 && result.resources==3 && result.reads==std::array<uint64_t,2>{3,1} &&
        result.writes==std::array<uint64_t,2>{2,2},"UAV/counter/indirect effects missing");
    Require(result.usages[0][std::size_t(Usage::Indirect)]==1 && result.usages[0][std::size_t(Usage::Counter)]==1 &&
        result.usages[1][std::size_t(Usage::Copy)]==1,"Usage accounting lost operand class");
    fixture.events[1][2].range=Bytes(40,4);result=fixture.Run();Require(result.conflicts==2,"Indirect argument byte bound not respected");
    for(uint32_t usage=0;usage<uint32_t(Usage::Count);++usage){Fixture value;
        value.Add(0,Buffer(),Kind::Read,Bytes(),Usage(usage));Safe(value.Run());}
}
void RecordingAndMetadata(){
    const auto reject=[&](auto mutation,Failure expected){Fixture fixture;
        fixture.Add(0,Buffer(),Kind::Read);fixture.Add(1,Buffer(),Kind::Read);auto sections=fixture.Sections();
        mutation(fixture,sections);const auto result=Analyze(sections[0],sections[1]);
        Require(result.failure==expected && !result.Ready() && !result.completeEvidence && !result.ExecutionPermitted(),"Invalid evidence identity accepted");};
    reject([](auto&,auto& s){s[0].recording.list=0;},Failure::Recording);
    reject([](auto&,auto& s){s[1].recording.nativeGeneration=0;},Failure::Recording);
    reject([](auto&,auto& s){s[0].recording.token=0;},Failure::Recording);
    reject([](auto&,auto& s){s[0].recording.context=0;},Failure::Recording);
    reject([](auto&,auto& s){s[0].recording.view=0;},Failure::Recording);
    reject([](auto&,auto& s){s[1].recording.node=UINT32_MAX;},Failure::Recording);
    reject([](auto&,auto& s){s[0].recording.node=65536;},Failure::Recording);
    reject([](auto&,auto& s){s[0].recording.consumer=0;},Failure::Recording);
    reject([](auto&,auto& s){s[1].recording.epoch=0;},Failure::Recording);
    reject([](auto&,auto& s){s[1].recording.pair=0;},Failure::Recording);
    reject([](auto&,auto& s){++s[1].recording.consumer;},Failure::Recording);
    reject([](auto&,auto& s){++s[1].recording.epoch;},Failure::Recording);
    reject([](auto&,auto& s){++s[1].recording.pair;},Failure::Recording);
    reject([](auto&,auto& s){s[1].recording.list=s[0].recording.list;},Failure::Recording);
    reject([](auto&,auto& s){s[1].recording.token=s[0].recording.token;},Failure::Recording);
    reject([](auto&,auto& s){s[1].recording.context=s[0].recording.context;},Failure::Recording);
    reject([](auto&,auto& s){s[1].recording.node=s[0].recording.node|0x20000000;},Failure::Recording);
    reject([](auto& f,auto&){++f.events[0][0].recording.list;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[0][0].recording.nativeGeneration;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[0][0].recording.token;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[0][0].recording.context;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[0][0].recording.view;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[0][0].recording.node;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[0][0].recording.consumer;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[0][0].recording.epoch;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[0][0].recording.pair;},Failure::EventIdentity);
    reject([](auto& f,auto&){++f.events[1][0].ordinal;},Failure::EventOrder);
    reject([](auto&,auto& s){s[0].coverage.firstOrdinal=UINT64_MAX;},Failure::EventOrder);
    reject([](auto& f,auto&){f.events[0][0].resource.cookie=0;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){f.events[0][0].resource.pointer=0;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){++f.events[1][0].resource.cookie;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){++f.events[1][0].resource.pointer;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){++f.events[1][0].resource.bytes;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){f.events[1][0].resource=Placed(Buffer());},Failure::ResourceIdentity);
    reject([](auto& f,auto&){f.events[0][0].resource.dimension=Dimension::Unknown;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){f.events[0][0].resource.subresources=2;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){f.events[0][0].resource.bytes=0;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){f.events[0][0].resource.bytes=UINT64_MAX;},Failure::ResourceIdentity);
    reject([](auto& f,auto&){f.events[0][0].resource.allocation.kind=AllocationKind::Unknown;},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource.allocation.kind=AllocationKind::Reserved;},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource.allocation.heap=1;},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource.allocation.offset=1;},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource.allocation.bytes=UINT64_MAX;},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource.allocation.bytes=4095;},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource=Placed(Buffer(),0);},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource=Placed(Buffer(),1,0,0);},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource=Placed(Buffer(),1,UINT64_MAX-100,4096);},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].resource=Placed(Buffer(),1,0,UINT64_MAX);},Failure::Allocation);
    reject([](auto& f,auto&){f.events[0][0].usage=Usage::Count;},Failure::Usage);
    for(const auto kind:{Kind::Transition,Kind::Alias,Kind::Unknown,Kind(99)}){
        const auto expected=kind==Kind::Transition?Failure::Transition:kind==Kind::Alias?Failure::Alias:Failure::Unknown;
        reject([&](auto& f,auto&){f.events[0][0].kind=kind;},expected);
    }
}
void Ranges(){
    const auto reject=[&](Resource resource,Range range,Failure expected=Failure::Range){Fixture fixture;
        fixture.Add(0,resource,Kind::Read,range);const auto result=fixture.Run();
        Require(result.failure==expected && !result.completeEvidence && !result.noObservedConflicts && !result.Ready(),"Invalid range accepted");};
    reject(Buffer(),Bytes(0,0));reject(Buffer(),Bytes(UINT64_MAX,2));reject(Buffer(),Bytes(4090,7));
    reject(Buffer(),Whole());reject(Buffer(),Subresources());reject(Buffer(),{});
    auto range=Bytes();range.firstSubresource=1;reject(Buffer(),range);range=Bytes();range.subresources=1;reject(Buffer(),range);
    reject(Texture(),Bytes());reject(Texture(),Subresources(0,0));reject(Texture(),Subresources(8));
    reject(Texture(),Subresources(7,2));reject(Texture(),Subresources(UINT32_MAX,2));
    range=Whole();range.bytes=1;reject(Texture(),range);range=Whole();range.firstSubresource=1;reject(Texture(),range);
    range=Subresources();range.offset=1;reject(Texture(),range);
    reject(Texture(1,0),Whole(),Failure::ResourceIdentity);reject(Texture(1,65537),Whole(),Failure::ResourceIdentity);
    auto texture=Texture();texture.bytes=4;reject(texture,Whole(),Failure::ResourceIdentity);
    Fixture fixture;fixture.Add(0,Texture(1,65536),Kind::Read,Subresources(65535));Safe(fixture.Run());
    fixture=Fixture{};fixture.Add(0,Buffer(1,UINT64_MAX-1),Kind::Write,Bytes(UINT64_MAX-2,1));
    fixture.Add(1,Buffer(1,UINT64_MAX-1),Kind::Read,Bytes(UINT64_MAX-3,1));Safe(fixture.Run());
    fixture.events[1][0].range=Bytes(UINT64_MAX-3,2);Conflict(fixture.Run(),PeerWritesMainReads);
}
void CoverageAndBounds(){
    Fixture fixture;fixture.Add(0,Buffer(),Kind::Read);fixture.Add(1,Buffer(),Kind::Read);
    const auto missing=[&](auto mutation){auto sections=fixture.Sections();mutation(sections);const auto result=Analyze(sections[0],sections[1]);
        Require(result.failure==Failure::Coverage && !result.completeEvidence && result.noObservedConflicts && !result.Ready() &&
            !result.ExecutionPermitted(),"Incomplete evidence became a commute proof");};
    for(uint32_t bit=1;bit<=PhysicalAllocations;bit<<=1)for(uint32_t side=0;side<2;++side)
        missing([&](auto& sections){sections[side].coverage.mask&=~bit;});
    missing([](auto& s){s[0].coverage.mask|=1u<<20;});
    missing([](auto& s){s[1].coverage.closed=false;});missing([](auto& s){s[0].coverage.whole=false;});
    missing([](auto& s){s[0].coverage.eventCount=0;});missing([](auto& s){s[1].coverage.eventCount=2;});
    auto sections=fixture.Sections();fixture.events[1][0].kind=Kind::Write;sections[0].coverage.mask=0;
    auto result=Analyze(sections[0],sections[1]);
    Require(result.failure==Failure::Conflict && !result.completeEvidence && !result.noObservedConflicts && result.conflicts==1,
        "Incomplete evidence hid an observed conflict");
    fixture=Fixture{};
    for(uint32_t i=0;i<MaxEventsPerSection;++i)fixture.Add(0,Buffer(),Kind::Read);
    Safe(fixture.Run());fixture.Add(0,Buffer(),Kind::Read);Require(fixture.Run().failure==Failure::Capacity,"Event capacity not bounded");
    fixture=Fixture{};
    for(uint32_t i=0;i<=MaxResources;++i)fixture.Add(i%2,Buffer(i+1),Kind::Read);
    Require(fixture.Run().failure==Failure::Capacity,"Resource capacity not bounded");
    fixture=Fixture{};
    for(uint32_t i=0;i<1025;++i)fixture.Add(0,Buffer(1),Kind::Write);
    for(uint32_t i=0;i<1024;++i)fixture.Add(1,Buffer(2),Kind::Read);
    result=fixture.Run();Require(result.failure==Failure::Capacity && result.comparisons==MaxComparisons &&
        !result.completeEvidence && !result.noObservedConflicts,"Comparison budget produced partial acceptance");
    fixture=Fixture{};
    for(uint32_t i=0;i<MaxEventsPerSection;++i){fixture.Add(0,Buffer(),Kind::Read);fixture.Add(1,Buffer(),Kind::Read);}
    result=fixture.Run();Safe(result);Require(!result.comparisons,"Read-only sections spent pair budget");
    fixture.events[1].back().kind=Kind::Write;fixture.events[1].back().resource=Buffer(2);
    result=fixture.Run();Safe(result);Require(result.comparisons==MaxEventsPerSection,"Read-only pairs polluted comparison budget");
}
}
int main()try{
    Positive();DirectionsAndUsage();RecordingAndMetadata();Ranges();CoverageAndBounds();
    std::puts("PASS source-bound scene access ranges, both commute hazards, aliasing and complete-evidence separation");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
