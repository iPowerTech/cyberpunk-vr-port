#include "Render/NativeTargetAccess.hpp"
#include <cstdio>
#include <source_location>
#include <stdexcept>

using namespace cvr::stereo::target_access;
static void Require(bool value,const std::source_location location=std::source_location::current()){
    if(!value){char message[512];std::snprintf(message,sizeof(message),"native target access assertion failed at %s:%u",
        location.file_name(),unsigned(location.line()));throw std::runtime_error(message);}
}
static detail::Event Event(Kind kind,uint32_t resource=detail::NoResource){detail::Event value;value.kind=kind;value.resource=resource;return value;}
static detail::Event Transition(uint32_t resource,uint32_t before,uint32_t after,UINT subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES){
    auto value=Event(Kind::Transition,resource);value.before=before;value.after=after;value.subresource=subresource;return value;
}
static detail::Event Proof(Kind kind,uint32_t resource=detail::NoResource,bool completed=true){
    auto value=Event(kind,resource);value.proof=7;value.flags=completed;return value;
}
int main()try{
    // Use the runtime's actual allocation/recycling plan across many more Reset
    // operations than retained slots. Source generations stay immutable.
    std::vector<detail::RecordingSlot> slots;uint64_t generation{},heldRows{},heldMarkers{};
    const auto reset=[&](uint64_t list){
        detail::RecordingPlan plan;
        Require(detail::PlanRecording(slots,list,generation,heldRows,heldMarkers,plan));
        Require(plan.generation==generation+1);
        const detail::RecordingSlot fresh{list,plan.generation,0,0,false,false};
        if(plan.index==slots.size())slots.push_back(fresh);else slots[plan.index]=fresh;
        generation=plan.generation;heldRows=plan.rows;heldMarkers=plan.markers;
        uint64_t actualRows{},actualMarkers{};for(const auto& slot:slots){actualRows+=slot.rows;actualMarkers+=slot.markers;}
        Require(actualRows==heldRows && actualMarkers==heldMarkers);
        return plan.index;
    };
    const auto record=[&](uint32_t index,uint64_t rows,uint64_t markers){
        slots[index].rows+=rows;slots[index].markers+=markers;heldRows+=rows;heldMarkers+=markers;
    };
    const auto source0=reset(1000);record(source0,20,3);slots[source0].source=true;
    const auto source1=reset(1001);record(source1,30,4);slots[source1].source=true;
    const auto sourceGeneration0=slots[source0].generation,sourceGeneration1=slots[source1].generation;
    for(uint32_t i=0;i<RecordingLimit*4;++i){
        const auto index=reset(2000+i%32);record(index,40+i%7,4+i%3);slots[index].closed=i%2==0;
        Require(slots.size()<=34 && heldRows<AccessLimit && heldMarkers<MarkerLimit);
        Require(slots[source0].generation==sourceGeneration0 && slots[source0].rows==20 && slots[source0].markers==3 && slots[source0].source);
        Require(slots[source1].generation==sourceGeneration1 && slots[source1].rows==30 && slots[source1].markers==4 && slots[source1].source);
    }
    const auto laterSameList=reset(1000);Require(laterSameList!=source0);
    record(laterSameList,17,2);
    Require(reset(1000)==laterSameList && slots[source0].generation==sourceGeneration0 && slots[source0].rows==20);

    slots.clear();generation=heldRows=heldMarkers=0;
    for(uint32_t i=0;i<RecordingLimit;++i){const auto index=reset(3000+i);record(index,2+i%3,1);slots[index].closed=true;}
    slots[0].source=true;slots[1].closed=false;slots[3].source=true;
    const auto kept0=slots[0],kept1=slots[1],kept3=slots[3];
    Require(reset(9000)==2);
    for(uint32_t i=0;i<RecordingLimit*3;++i){
        const auto index=reset(10000+i);record(index,8,2);slots[index].closed=true;
        Require(slots.size()==RecordingLimit && slots[0].generation==kept0.generation && slots[0].source &&
            slots[1].generation==kept1.generation && !slots[1].closed && slots[3].generation==kept3.generation && slots[3].source);
    }
    detail::RecordingPlan plan;
    for(auto& slot:slots)slot.closed=false;
    Require(!detail::PlanRecording(slots,99999,generation,heldRows,heldMarkers,plan) && plan.failure==Failure::Capacity);
    slots[2].closed=true;slots[2].source=true;
    Require(!detail::PlanRecording(slots,99999,generation,heldRows,heldMarkers,plan) && plan.failure==Failure::Capacity);
    slots[2].source=false;
    Require(detail::PlanRecording(slots,99999,generation,heldRows,heldMarkers,plan) && plan.index==2);
    record(2,3,1);
    Require(!detail::PlanRecording(slots,slots[2].list,generation,0,0,plan) && plan.failure==Failure::Recording);
    Require(!detail::PlanRecording(slots,slots[2].list,UINT64_MAX,heldRows,heldMarkers,plan) && plan.failure==Failure::Capacity);

    // Pressure can discard only closed unmarked histories. The source, open
    // and current recordings keep every event, even when no room remains.
    std::array<detail::RecordingSlot,6> pressure{{
        {1,1,6000,1000,true,true},{2,2,6000,1000,false,false},{3,3,AccessLimit-13384,1000,false,false},
        {4,4,1000,500,false,true},{5,5,384,100,false,true},{6,6,0,0,false,true}
    }};
    detail::PressurePlan pressurePlan;uint64_t pressureRows=AccessLimit,pressureMarkers=3600;
    Require(detail::PlanPressure(pressure,2,pressureRows,pressureMarkers,false,pressurePlan) && pressurePlan.index==3);
    Require(pressurePlan.rows==AccessLimit-1000 && pressurePlan.markers==3100);
    pressure[3].rows=pressure[3].markers=0;pressureRows=pressurePlan.rows;pressureMarkers=pressurePlan.markers;
    pressure[2].rows+=1000;pressureRows+=1000;
    Require(detail::PlanPressure(pressure,2,pressureRows,pressureMarkers,false,pressurePlan) && pressurePlan.index==4);
    Require(pressurePlan.rows==AccessLimit-384 && pressurePlan.markers==3000);
    pressure[4].rows=pressure[4].markers=0;pressureRows=pressurePlan.rows;pressureMarkers=pressurePlan.markers;
    pressure[2].rows+=384;pressureRows+=384;
    Require(!detail::PlanPressure(pressure,2,pressureRows,pressureMarkers,false,pressurePlan) && pressurePlan.failure==Failure::Capacity);
    pressure[2].closed=true;
    Require(!detail::PlanPressure(pressure,2,pressureRows,pressureMarkers,false,pressurePlan) && pressurePlan.failure==Failure::Capacity);
    Require(pressure[0].rows==6000 && pressure[0].markers==1000 && pressure[0].source);
    Require(pressure[1].rows==6000 && pressure[1].markers==1000 && !pressure[1].closed);
    Require(pressure[2].rows==AccessLimit-12000 && pressure[2].markers==1000);
    Require(pressure[3].rows==0 && pressure[4].rows==0 && pressure[5].rows==0);

    constexpr uint64_t markerShare=(MarkerLimit-296)/2,markerRows=2*(markerShare+100)+1100;
    std::array<detail::RecordingSlot,4> markerPressure{{
        {1,2,markerShare+100,markerShare,true,true},{2,3,markerShare+100,markerShare,false,false},
        {3,5,1000,296,false,true},{4,1,100,0,false,true}
    }};
    Require(detail::PlanPressure(markerPressure,1,markerRows,MarkerLimit,false,pressurePlan) && pressurePlan.index==UINT32_MAX);
    Require(detail::PlanPressure(markerPressure,1,markerRows,MarkerLimit,true,pressurePlan) && pressurePlan.index==2);
    Require(pressurePlan.rows==markerRows-1000 && pressurePlan.markers==MarkerLimit-296);
    markerPressure[2].source=true;
    Require(!detail::PlanPressure(markerPressure,1,markerRows,MarkerLimit,true,pressurePlan) && pressurePlan.failure==Failure::Capacity);
    markerPressure[2].source=false;markerPressure[2].closed=false;
    Require(!detail::PlanPressure(markerPressure,1,markerRows,MarkerLimit,true,pressurePlan) && pressurePlan.failure==Failure::Capacity);
    Require(!detail::PlanPressure(markerPressure,4,markerRows,MarkerLimit,true,pressurePlan) && pressurePlan.failure==Failure::Recording);

    const detail::Resource authenticated{700,{800,0x10000,0x20000,2},D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
    Require(detail::CompareAuthenticatedCreation(authenticated,authenticated)==detail::CreationMatch::Same);
    auto cached=authenticated;cached.allocation={};
    Require(detail::CompareAuthenticatedCreation(cached,authenticated)==detail::CreationMatch::Upgrade);
    cached=authenticated;cached.pointer++;
    Require(detail::CompareAuthenticatedCreation(cached,authenticated)==detail::CreationMatch::Conflict);
    cached=authenticated;cached.flags=0;
    Require(detail::CompareAuthenticatedCreation(cached,authenticated)==detail::CreationMatch::Conflict);
    cached=authenticated;cached.allocation.heap++;
    Require(detail::CompareAuthenticatedCreation(cached,authenticated)==detail::CreationMatch::Conflict);
    cached=authenticated;cached.allocation.offset+=0x10000;
    Require(detail::CompareAuthenticatedCreation(cached,authenticated)==detail::CreationMatch::Conflict);
    cached=authenticated;cached.allocation.bytes+=0x10000;
    Require(detail::CompareAuthenticatedCreation(cached,authenticated)==detail::CreationMatch::Conflict);
    cached=authenticated;cached.allocation.kind=1;
    Require(detail::CompareAuthenticatedCreation(cached,authenticated)==detail::CreationMatch::Conflict);
    cached=authenticated;cached.allocation.bytes=0;
    Require(detail::CompareAuthenticatedCreation(cached,authenticated)==detail::CreationMatch::Conflict);
    cached=authenticated;cached.allocation={};
    Require(detail::CompareAuthenticatedCreation(cached,cached)==detail::CreationMatch::Conflict);

    detail::ReceiptStatus live;live.observing=true;
    live.key.epoch=5;live.key.metadataVersion=3;live.key.queue=100;live.key.thread=200;live.key.scope=300;live.key.frame=400;
    for(uint32_t side=0;side<2;++side){auto& source=live.key.sources[side];
        source.list=500+side;source.generation=600+side;source.nativeGeneration=700+side;source.token=800+side;
        source.context=900+side;source.registry=1000;source.rows=100;source.markers=10;source.frame=400;source.side=side;
        source.sourceRow=40;source.exitRow=80;source.closeResult=S_OK;source.whole=source.closed=source.omKnown=true;
    }
    const auto receiptKey=live.key;
    Require(detail::ReceiptMatches(receiptKey,live));
    const auto rejectedReceipt=[&](auto change){auto value=live;change(value);Require(!detail::ReceiptMatches(receiptKey,value));};
    rejectedReceipt([](auto& v){v.observing=false;});
    rejectedReceipt([](auto& v){v.finished=true;});
    rejectedReceipt([](auto& v){v.unavailable=true;});
    rejectedReceipt([](auto& v){v.failure=Failure::Capacity;});
    rejectedReceipt([](auto& v){++v.key.epoch;});
    rejectedReceipt([](auto& v){++v.key.metadataVersion;});
    rejectedReceipt([](auto& v){++v.key.queue;});
    rejectedReceipt([](auto& v){++v.key.thread;});
    rejectedReceipt([](auto& v){++v.key.scope;});
    rejectedReceipt([](auto& v){++v.key.frame;});
    rejectedReceipt([](auto& v){++v.key.sources[0].generation;});
    rejectedReceipt([](auto& v){++v.key.sources[1].nativeGeneration;});
    rejectedReceipt([](auto& v){++v.key.sources[0].token;});
    rejectedReceipt([](auto& v){++v.key.sources[1].context;});
    rejectedReceipt([](auto& v){++v.key.sources[0].registry;});
    rejectedReceipt([](auto& v){++v.key.sources[1].rows;});
    rejectedReceipt([](auto& v){++v.key.sources[0].markers;});
    rejectedReceipt([](auto& v){++v.key.sources[1].sourceRow;});
    rejectedReceipt([](auto& v){++v.key.sources[0].exitRow;});
    rejectedReceipt([](auto& v){v.key.sources[0].proof=1;});
    rejectedReceipt([](auto& v){v.key.sources[1].failure=Failure::Reset;});
    rejectedReceipt([](auto& v){v.key.sources[0].closeResult=E_FAIL;});
    rejectedReceipt([](auto& v){v.key.sources[1].whole=false;});
    rejectedReceipt([](auto& v){v.key.sources[0].closed=false;});
    rejectedReceipt([](auto& v){v.key.sources[1].omKnown=false;});
    rejectedReceipt([](auto& v){++v.key.sources[0].om[8];});
    auto malformed=live;malformed.key.sources[0].exitRow=malformed.key.sources[0].rows;
    Require(!detail::ReceiptMatches(malformed.key,malformed));
    malformed=live;malformed.key.sources[1].token=malformed.key.sources[0].token;
    Require(!detail::ReceiptMatches(malformed.key,malformed));
    malformed=live;malformed.key.sources[0].whole=false;
    Require(!detail::ReceiptMatches(malformed.key,malformed));

    cvr::stereo::submission_frontier::Boundary boundary;
    boundary.probeEpoch=1;boundary.batch=2;boundary.thread=200;boundary.queue=100;boundary.lists=1234;
    boundary.consumer=3;boundary.graphEpoch=4;boundary.pairGeneration=5;boundary.frameOwner=6;boundary.fingerprint=7;boundary.invocation=8;
    boundary.count=4;boundary.afterIndex=3;boundary.sourceIndices={2,1};
    cvr::stereo::queue_residency::Invocation invocation{200,100,1234,{499,501,500,502}};
    const std::array<uint64_t,2> sourceLists{500,501};
    Require(detail::ReceiptBoundary(boundary,invocation,sourceLists));
    auto badBoundary=boundary;++badBoundary.queue;Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;++badBoundary.thread;Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;++badBoundary.lists;Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;--badBoundary.count;Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;badBoundary.sourceIndices={0,2};Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;badBoundary.sourceIndices={1,2};Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;badBoundary.sourceIndices={2,2};Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;badBoundary.sourceIndices={2,UINT32_MAX};Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;--badBoundary.afterIndex;Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    badBoundary=boundary;badBoundary.invocation=0;Require(!detail::ReceiptBoundary(badBoundary,invocation,sourceLists));
    auto badInvocation=invocation;badInvocation.lists[0]=500;Require(!detail::ReceiptBoundary(boundary,badInvocation,sourceLists));
    badInvocation=invocation;badInvocation.lists[3]=0;Require(!detail::ReceiptBoundary(boundary,badInvocation,sourceLists));
    badInvocation=invocation;badInvocation.lists.pop_back();badBoundary=boundary;--badBoundary.count;
    Require(!detail::ReceiptBoundary(badBoundary,badInvocation,sourceLists));

    constexpr auto rt=uint32_t(D3D12_RESOURCE_STATE_RENDER_TARGET),depth=uint32_t(D3D12_RESOURCE_STATE_DEPTH_WRITE),
        copy=uint32_t(D3D12_RESOURCE_STATE_COPY_SOURCE),read=uint32_t(D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    std::vector<detail::Resource> resources;
    std::array<detail::Watch,4> watched{};
    for(uint32_t t=0;t<4;++t){resources.push_back({100+t,{900,65536ull*t,65536,2},0});
        watched[t]={resources.back(),t==3?2u:1u,{t==3?depth:rt,t==3?depth:rt}};
    }
    resources.push_back({200,{901,0,65536,2},0});
    resources.push_back({201,{900,65536ull*4,65536,2},0});
    resources.push_back({202,{900,0,65536,2},0});
    resources.push_back({203,{},0});
    resources.push_back({204,{0,0,0,1},0});
    detail::Check result;
    const std::vector<detail::Event> own{Event(Kind::Unknown),Event(Kind::Source),Event(Kind::Write,0),Event(Kind::Exit)};
    Require(detail::CheckWindow(own,resources,watched,true,result));
    Require(result.rows==0 && result.finalStates[3][0]==depth);
    Require(!detail::CheckWindow(own,resources,watched,false,result) && result.failure==Failure::Unknown);
    auto events=own;events.push_back(Event(Kind::Shader));events.push_back(Event(Kind::Write,4));
    Require(detail::CheckWindow(events,resources,watched,true,result) && result.excludedShaders==1 && result.writes==1);
    events.push_back(Event(Kind::Write,0));
    Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::Write && result.resource==0);
    events=own;events.push_back(Event(Kind::Read,0));
    Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::Read);

    // State exclusion applies to all depth/stencil planes, not only color or OM.
    events=own;events.push_back(Transition(3,depth,read,0));
    Require(detail::CheckWindow(events,resources,watched,true,result));
    events.push_back(Event(Kind::Shader));
    Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::Shader && result.resource==3);
    for(const auto state:{uint32_t(D3D12_RESOURCE_STATE_COMMON),uint32_t(D3D12_RESOURCE_STATE_UNORDERED_ACCESS),copy,read}){
        auto unsafe=watched;unsafe[0].states[0]=state;
        const std::array shader{Event(Kind::Shader)};
        Require(!detail::CheckWindow(shader,resources,unsafe,false,result) && result.failure==Failure::Shader);
    }
    auto simultaneous=watched;simultaneous[0].resource.flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    Require(!detail::CheckWindow({},resources,simultaneous,false,result) && result.failure==Failure::Metadata);

    // Later whole recordings may draw only to provably disjoint targets.
    events={Event(Kind::Source),Event(Kind::Write,4),Event(Kind::Shader),Event(Kind::Exit),Event(Kind::Write,5)};
    Require(detail::CheckWindow(events,resources,watched,false,result) && result.excludedShaders==1);
    for(const auto index:{0u,6u,7u}){
        auto touching=events;touching.push_back(Event(Kind::Write,index));
        Require(!detail::CheckWindow(touching,resources,watched,false,result));
    }
    events={Transition(0,rt,copy),Transition(0,copy,rt)};
    Require(!detail::CheckWindow(events,resources,watched,false,result) && result.failure==Failure::Transition);
    events={Event(Kind::Unknown)};
    Require(!detail::CheckWindow(events,resources,watched,false,result) && result.failure==Failure::Unknown);

    // An explicit proof scope permits only copy reads with balanced barriers.
    auto proof=own;
    for(uint32_t t=0;t<4;++t)proof.push_back(Proof(Kind::ProofBegin,t));
    proof.push_back(Transition(0,rt,copy));proof.push_back(Event(Kind::Read,0));proof.push_back(Event(Kind::Write,8));
    proof.push_back(Transition(0,copy,rt));
    proof.push_back(Transition(3,depth,copy,0));auto planeRead=Event(Kind::Read,3);planeRead.subresource=0;proof.push_back(planeRead);
    proof.push_back(Transition(3,copy,depth,0));proof.push_back(Proof(Kind::ProofEnd));
    Require(detail::CheckWindow(proof,resources,watched,true,result) && result.proofScopes==1 && result.proofReads==2);
    auto bad=proof;bad.pop_back();Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad.back().flags=0;Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad[bad.size()-2].after=read;Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad[9]=Event(Kind::Write,0);Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad[9]=Event(Kind::Shader);Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad[9]=Event(Kind::Unknown);Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad[9]=Event(Kind::Alias,4);bad[9].other=5;
    Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad[9]=Event(Kind::Uav,4);Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad[8]=Transition(0,rt,rt);Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad[9]=Event(Kind::Read,3);Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::State);
    bad=proof;bad[1]=Event(Kind::Shader);Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Source);
    bad=proof;bad[7].proof=8;Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);
    bad=proof;bad.insert(bad.end()-1,Proof(Kind::ProofBegin,0));Require(!detail::CheckWindow(bad,resources,watched,true,result) && result.failure==Failure::Proof);

    // Saved placement snapshots, not future object queries, decide overlap.
    events={Event(Kind::Alias,4)};events[0].other=5;
    Require(detail::CheckWindow(events,resources,watched,false,result));
    events[0].other=6;Require(!detail::CheckWindow(events,resources,watched,false,result) && result.failure==Failure::Alias);
    events={Event(Kind::Alias)};Require(!detail::CheckWindow(events,resources,watched,false,result) && result.failure==Failure::Alias);
    auto committed=watched;for(auto& watch:committed)watch.resource.allocation={0,0,0,1};
    Require(detail::CheckWindow(events,resources,committed,false,result));
    events={Event(Kind::Read,UINT32_MAX-1)};
    Require(!detail::CheckWindow(events,resources,watched,false,result) && result.failure==Failure::Metadata);
    events={Event(Kind::Alias,UINT32_MAX-1)};
    Require(!detail::CheckWindow(events,resources,committed,false,result) && result.failure==Failure::Metadata);

    // Source marker starts the original-state history; Exit must occur exactly once.
    events={Event(Kind::Exit)};Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::Source);
    events={Event(Kind::Source)};Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::MissingExit);
    events={Event(Kind::Source),Event(Kind::Source),Event(Kind::Exit)};
    Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::Source);
    events={Event(Kind::Source),Event(Kind::Exit),Event(Kind::Exit)};
    Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::MissingExit);
    events=own;auto split=Transition(0,rt,copy);split.flags=D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;events.push_back(split);
    Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::State);
    events.back()=Transition(3,depth,copy,2);Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::State);
    events.back()=Transition(0,copy,rt);Require(!detail::CheckWindow(events,resources,watched,true,result) && result.failure==Failure::State);
    auto empty=watched;empty[0].count=0;Require(!detail::CheckWindow({},resources,empty,false,result));
    auto unknown=watched;unknown[0].resource.allocation={};Require(!detail::CheckWindow({},resources,unknown,false,result));
    std::puts("native target access tests passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
