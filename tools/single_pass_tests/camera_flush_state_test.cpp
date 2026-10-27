#include "Render/NativeCameraFlushState.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <utility>

using cvr::stereo::camera_upload::Context;
namespace flush=cvr::stereo::camera_upload::flush_state;
namespace {
constexpr uint64_t MemoryBase=0x10000,State=0x10000,Layout=0x12000,Descriptors=0x14000,NativeContext=0x26000;
constexpr Context Current{NativeContext,State,0x29000};
constexpr uint64_t Replacement=0x88880000,GroupBits=(1ull<<2)|(1ull<<7);
constexpr uint64_t CpuHeapBase=0x400000,GpuHeapBase=0x800000000ull;
std::array<uint8_t,0x18000> memory;
std::vector<std::pair<uint64_t,size_t>> readSpans;
uint64_t reads{},failAt{},mutateAt{},mutationAddress{},mutationValue{};
size_t mutationBytes{};
void Require(bool good,const char* message){if(!good)throw std::runtime_error(message);}
template<class T> void Put(uint64_t address,T value) {
    Require(address>=MemoryBase && sizeof(T)<=memory.size() && address-MemoryBase<=memory.size()-sizeof(T),"Fixture write out of bounds");
    std::memcpy(memory.data()+(address-MemoryBase),&value,sizeof(T));
}
bool ReadMemory(uintptr_t address,void* output,size_t bytes) {
    ++reads;
    if(reads==failAt)return false;
    if(reads==mutateAt){
        Require(mutationAddress>=MemoryBase && mutationBytes<=8 && mutationAddress-MemoryBase<=memory.size()-mutationBytes,"Fixture mutation out of bounds");
        std::memcpy(memory.data()+(mutationAddress-MemoryBase),&mutationValue,mutationBytes);
    }
    if(address<MemoryBase || bytes>memory.size() || address-MemoryBase>memory.size()-bytes)return false;
    readSpans.emplace_back(address,bytes);
    std::memcpy(output,memory.data()+(address-MemoryBase),bytes);return true;
}
constexpr uint64_t Entry(uint32_t group){return Layout+0x38+uint64_t(group)*16;}
void Reset() {
    memory={};readSpans.clear();reads=failAt=mutateAt=mutationAddress=mutationValue=0;mutationBytes=0;
    Put<uint64_t>(NativeContext+0x30,Current.list);Put<uint64_t>(NativeContext+0x60,State);
    Put<uint32_t>(NativeContext+0x3c0,101);Put<uint32_t>(NativeContext+0x3c4,202);
    Put<uint64_t>(State,0x28000);Put<uint32_t>(State+8,512);Put<uint32_t>(State+0xc,64);
    Put<uint32_t>(State+0x10,32);Put<uint32_t>(State+0x14,8);Put<uint64_t>(State+0x28,Descriptors);
    Put<uint64_t>(State+0x68,Layout);Put<uint64_t>(Layout+8,GroupBits);
    Put<uint64_t>(Layout+0x18,1ull<<2);Put<uint64_t>(Layout+0x20,1ull<<7);
    Put<uint8_t>(Layout+0x455,2);Put<uint8_t>(Layout+0x471,7);
    for(const uint32_t group:{2u,7u}){
        const auto entry=Entry(group);
        Put<uint32_t>(entry+4,group==2?16u:48u);Put<uint16_t>(entry+8,group==2?0:1);
        Put<uint16_t>(entry+10,group==2?4:3);Put<uint8_t>(entry+13,group==2?0:1);
        Put<uint8_t>(entry+14,group==2?17:3);Put<int32_t>(State+0x80+group*4,-1);
    }
    for(uint32_t i=0;i<8192;++i)Put<uint64_t>(Descriptors+uint64_t(i)*8,0x100000+uint64_t(i)*32);
}
flush::Snapshot Capture(uint32_t stages=3) {
    flush::Snapshot result;Require(flush::Read(ReadMemory,Current,stages,result),"Valid native snapshot rejected");return result;
}
void Replace(const flush::Snapshot& original) {
    Put<uint64_t>(State+0x70,original.dirty|original.groups);
    for(const auto& table:original.tables){
        Put<uint64_t>(Descriptors+uint64_t(table.start+1-table.first)*8,Replacement);
        Put<int32_t>(State+0x80+uint64_t(table.group)*4,-1);
    }
}
void Emit(const flush::Snapshot& pending) {
    Put<uint64_t>(State+0x70,pending.dirty&~pending.groups);Put<uint32_t>(State+0x14,pending.cursor+pending.count);
    Put<uint64_t>(State+0x50,CpuHeapBase);Put<uint64_t>(State+0x58,GpuHeapBase);
    Put<uint8_t>(State+0x60,1);Put<uint8_t>(State+0x61,1);
    // Native 0x1F2579 leaves copied table cache indices at -1, not a cached slot.
    for(const auto& table:pending.tables)Put<int32_t>(State+0x80+uint64_t(table.group)*4,-1);
}
template<class T> void Reject(uint64_t address,T value,uint32_t stages=3) {
    Reset();Put<T>(address,value);flush::Snapshot out;
    Require(!flush::Read(ReadMemory,Current,stages,out),"Malformed native field accepted");
}
void StageGroupsAndCompleteReads() {
    for(const uint32_t stages:{1u,2u,3u}){
        Reset();const auto s=Capture(stages);
        Require(s.context==Current && s.heap==0x28000 && s.layout==Layout && s.cpuTable==Descriptors,"Native identity fields differ");
        Require(s.base==512 && s.capacity==64 && s.stride==32 && s.cursor==8 && flush::Room(s),"Allocator fields or room differ");
        Require(s.count==(stages==1?4u:stages==2?3u:7u) && s.tables.size()==(stages==3?2u:1u),"Selected table count differs");
        for(const auto& table:s.tables){
            Require(table.group!=table.rootParameter && table.rootParameter==(table.group==2?17u:3u),"Group ID mistaken for D3D root parameter");
            Require(table.descriptors.size()==table.count,"Only camera slot was captured");
            for(size_t i=0;i<table.descriptors.size();++i)
                Require(table.descriptors[i]==0x100000+uint64_t(table.start+i)*32,"Full CPU table contents differ");
            const auto span=std::pair<uint64_t,size_t>{Descriptors+uint64_t(table.start)*8,size_t(table.count)*8};
            Require(std::count(readSpans.begin(),readSpans.end(),span)==2,"Full table not read twice");
        }
    }
    Reset();Put<uint8_t>(Layout+0x471,2);Put<uint64_t>(Layout+0x20,1ull<<2);
    const auto shared=Capture();Require(shared.groups==(1ull<<2) && shared.count==4 && shared.tables.size()==1,"Shared stage group counted twice");
    Put<uint64_t>(Layout+0x20,1ull<<7);flush::Snapshot out;
    Require(!flush::Read(ReadMemory,Current,3,out),"Shared group bypassed PS visibility proof");
    Reject<uint8_t>(Entry(7)+14,17);
    Reject<uint32_t>(Entry(7)+4,18);Reject<uint32_t>(Entry(7)+4,14);
    Reset();Put<uint32_t>(Entry(7)+4,20);
    Require(Capture().tables.size()==2,"Adjacent non-overlapping CPU tables rejected");
}
void InvalidFields() {
    Reset();flush::Snapshot out;
    Require(!flush::Read(nullptr,Current,1,out) && !flush::Read(ReadMemory,{},1,out),"Missing reader/context accepted");
    for(const uint32_t stages:{0u,4u,0xffffffffu})Require(!flush::Read(ReadMemory,Current,stages,out),"Invalid stage mask accepted");
    Require(!flush::Read(ReadMemory,{UINT64_MAX-0x30,State,Current.list},1,out) &&
        !flush::Read(ReadMemory,{NativeContext,UINT64_MAX-0x100,Current.list},1,out),"Context arithmetic overflow accepted");
    Reject<uint64_t>(NativeContext+0x30,Current.list+8);Reject<uint64_t>(NativeContext+0x60,State+8);
    for(const uint32_t handle:{0u,0x80000000u,0xffffffffu}){
        Reject<uint32_t>(NativeContext+0x3c0,handle,1);Reject<uint32_t>(NativeContext+0x3c4,handle,2);
        Reject<uint32_t>(NativeContext+0x3c0,handle,3);Reject<uint32_t>(NativeContext+0x3c4,handle,3);
    }
    for(const uint32_t handle:{1u,0x7fffffffu}){
        Reset();Put<uint32_t>(NativeContext+0x3c0,handle);Put<uint32_t>(NativeContext+0x3c4,handle);
        Require(Capture().tables.size()==2,"Valid native graphics program handle endpoint rejected");
    }
    Reset();Put<uint32_t>(NativeContext+0x3c4,0);Require(Capture(1).count==4,"Unrequested PS program unnecessarily required");
    Reset();Put<uint32_t>(NativeContext+0x3c0,0);Require(Capture(2).count==3,"Unrequested VS program unnecessarily required");
    Reject<uint64_t>(State,0);Reject<uint64_t>(State+0x28,0);Reject<uint64_t>(State+0x68,0);
    Reject<uint64_t>(State+0x28,UINT64_MAX-8);Reject<uint64_t>(State+0x68,UINT64_MAX-0x100);
    Reject<uint8_t>(State+0x180,1);Reject<uint32_t>(State+0x10,0);Reject<uint32_t>(State+0x10,257);
    Reject<uint32_t>(State+0xc,0);Reject<uint32_t>(State+0x14,65);Reject<uint32_t>(State+8,UINT32_MAX-32);
    Reject<uint32_t>(State+8,UINT32_MAX/32);Reject<uint64_t>(Layout+8,1ull<<7);
    Reject<uint64_t>(Layout,1ull<<2,1);Reject<uint64_t>(Layout,1ull<<7,2);Reject<uint64_t>(Layout,GroupBits,3);
    for(const uint8_t invalid:{uint8_t(2),uint8_t(255)}){
        Reject<uint8_t>(State+0x60,invalid);Reject<uint8_t>(State+0x61,invalid);
    }
    Reject<uint64_t>(Layout+0x18,1ull<<7);Reject<uint64_t>(Layout+0x20,1ull<<2);
    for(const uint8_t group:{uint8_t(64),uint8_t(255)})Reject<uint8_t>(Layout+0x455,group);
    Reject<uint8_t>(Entry(2)+13,2);Reject<uint8_t>(Entry(2)+14,64);
    Reject<uint16_t>(Entry(2)+10,0);Reject<uint16_t>(Entry(2)+8,2);Reject<uint16_t>(Entry(2)+10,1);
    Reject<uint16_t>(Entry(2)+10,8193);Reject<uint32_t>(Entry(2)+4,8192);
    Reject<uint64_t>(Descriptors+16*8,0);Reject<uint64_t>(Descriptors+19*8,0);
    Reset();Put<uint16_t>(Entry(2)+8,1);Put<uint16_t>(Entry(2)+10,1);Put<uint32_t>(Entry(2)+4,8191);
    Require(Capture(1).tables[0].descriptors.size()==1,"Last bounded CPU descriptor rejected");
    Reset();Put<uint8_t>(Layout+0x455,63);Put<uint64_t>(Layout+8,1ull<<63);Put<uint64_t>(Layout+0x18,1ull<<63);
    std::memcpy(memory.data()+(Entry(63)-MemoryBase),memory.data()+(Entry(2)-MemoryBase),16);
    Put<int32_t>(State+0x80+63*4,-1);
    Require(Capture(1).groups==(1ull<<63),"Highest group bit rejected or shifted incorrectly");
}
void ReadFailuresAndChanges() {
    Reset();Capture();const auto completeReads=reads;
    for(uint64_t failure=1;failure<=completeReads;++failure){
        Reset();failAt=failure;flush::Snapshot out;
        Require(!flush::Read(ReadMemory,Current,3,out),"Incomplete snapshot read accepted");
    }
    Reset();flush::Snapshot once;Require(flush::ReadOnce(ReadMemory,Current,3,once),"Single snapshot rejected");const auto firstReads=reads;
    struct Change {uint64_t address,value;size_t bytes;};
    for(const auto& change:std::array<Change,8>{{
        {State+0x14,9,4},{State,0x28008,8},{NativeContext+0x30,Current.list+8,8},
        {Layout+0x18,0,8},{Descriptors+16*8,0x77770000,8},{Descriptors+19*8,0x77770000,8},
        {State+0x70,1ull<<2,8},{Entry(2)+14,18,1}}}){
        Reset();mutateAt=firstReads+1;mutationAddress=change.address;mutationValue=change.value;mutationBytes=change.bytes;
        flush::Snapshot out;Require(!flush::Read(ReadMemory,Current,3,out),"State changed between complete reads was accepted");
    }
}
void PendingAndFlushed() {
    for(const uint32_t stages:{1u,2u,3u}){
        Reset();const auto original=Capture(stages);Replace(original);const auto pending=Capture(stages);
        Require(flush::Pending(original,pending,Replacement) && !flush::Room(pending),"Valid dirty replacement rejected");
        Require(!flush::Pending(original,pending,0) && !flush::Pending(original,pending,Replacement+8),"Wrong replacement accepted");
        auto changed=pending;changed.cursor++;Require(!flush::Pending(original,changed,Replacement),"Pending cursor advanced");
        changed=pending;changed.dirty|=1ull<<63;Require(!flush::Pending(original,changed,Replacement),"Foreign dirty table accepted");
        changed=pending;changed.dirty=0;Require(!flush::Pending(original,changed,Replacement),"Clean replacement accepted as pending");
        changed=pending;changed.rebind=original.groups;Require(!flush::Pending(original,changed,Replacement),"Cached rebind accepted");
        for(size_t t=0;t<pending.tables.size();++t){
            changed=pending;changed.tables[t].cached=0;Require(!flush::Pending(original,changed,Replacement),"Cached replacement accepted");
            for(size_t word=0;word<pending.tables[t].descriptors.size();++word){
                changed=pending;changed.tables[t].descriptors[word]^=8;
                Require(!flush::Pending(original,changed,Replacement),"Changed CPU descriptor word accepted");
            }
        }
        Emit(pending);const auto emitted=Capture(stages);
        Require(flush::Flushed(pending,emitted) && flush::SameAllocation(original,emitted),"Native copied-table ABI rejected");
        changed=emitted;changed.cursor--;Require(!flush::Flushed(pending,changed),"Partial cursor advance accepted");
        changed=emitted;changed.cursor++;Require(!flush::Flushed(pending,changed),"Extra cursor advance accepted");
        changed=emitted;changed.dirty=original.groups;Require(!flush::Flushed(pending,changed),"Unflushed dirty table accepted");
        changed=emitted;changed.rebind=original.groups;Require(!flush::Flushed(pending,changed),"Unflushed cached table accepted");
        for(size_t t=0;t<emitted.tables.size();++t){
            changed=emitted;changed.tables[t].cached=0;Require(!flush::Flushed(pending,changed),"Copied table incorrectly became a cached table");
            changed=emitted;changed.tables[t].descriptors.back()^=8;Require(!flush::Flushed(pending,changed),"Table changed during flush");
        }
        for(const auto& table:original.tables)
            Put<uint64_t>(Descriptors+uint64_t(table.start+1-table.first)*8,table.descriptors[1-table.first]);
        Put<uint64_t>(State+0x70,original.dirty|original.groups);const auto restoring=Capture(stages);Emit(restoring);const auto restored=Capture(stages);
        Require(flush::Flushed(restoring,restored) && restored.cursor==original.cursor+2*original.count,"Restoration did not consume second fresh table");
        for(size_t t=0;t<original.tables.size();++t)Require(restored.tables[t].descriptors==original.tables[t].descriptors,"Original CPU table not restored");
    }
}
void AllocationAndBudget() {
    Reset();const auto original=Capture();auto changed=original;
    changed.capacity=changed.cursor+2*changed.count;Require(flush::Room(changed),"Exact two-copy budget rejected");
    --changed.capacity;Require(!flush::Room(changed),"Insufficient two-copy budget accepted");
    changed=original;changed.dirty=1ull<<2;Require(!flush::Room(changed),"Visible dirty allocation admitted");
    changed=original;changed.rebind=1;Require(!flush::Room(changed),"Cached rebind admitted");
    changed=original;changed.count=0;Require(!flush::Room(changed),"Empty allocation admitted");
    changed=original;changed.cursor=changed.capacity+1;Require(!flush::Room(changed),"Invalid cursor underflow admitted");
    changed=original;changed.cursor=UINT32_MAX;Require(!flush::Room(changed),"Wrapped cursor budget admitted");
    const auto reject=[&](const flush::Snapshot& value){Require(!flush::SameAllocation(original,value),"Allocation/ABI drift accepted");};
    changed=original;changed.context.list++;reject(changed);changed=original;changed.context.state++;reject(changed);
    changed=original;changed.context.context++;reject(changed);changed=original;changed.heap++;reject(changed);
    changed=original;changed.layout++;reject(changed);changed=original;changed.cpuTable++;reject(changed);
    changed=original;changed.base++;reject(changed);changed=original;changed.capacity++;reject(changed);
    changed=original;changed.stride++;reject(changed);changed=original;changed.groups^=1;reject(changed);
    changed=original;changed.count++;reject(changed);changed=original;changed.tables.pop_back();reject(changed);
    changed=original;changed.resourceMask^=1ull<<40;reject(changed);
    changed=original;changed.samplerMask^=1ull<<41;reject(changed);
    changed=original;changed.graphicsMask^=1ull<<42;reject(changed);
    for(size_t stage=0;stage<original.programs.size();++stage){changed=original;changed.programs[stage]++;reject(changed);}
    for(size_t stage=0;stage<original.visibility.size();++stage){changed=original;changed.visibility[stage]^=1ull<<43;reject(changed);}
    for(size_t t=0;t<original.tables.size();++t){
        changed=original;changed.tables[t].group++;reject(changed);changed=original;changed.tables[t].rootParameter++;reject(changed);
        changed=original;changed.tables[t].start++;reject(changed);changed=original;changed.tables[t].count++;reject(changed);
        changed=original;changed.tables[t].first^=1;reject(changed);
    }
}
void InvisibleDirtyPreserved() {
    constexpr uint64_t compute=0x00000ff000000000ull,sampler=1ull<<44,unclassified=1ull<<45;
    constexpr uint64_t untouched=compute|sampler|unclassified;
    for(const uint32_t stages:{1u,2u,3u}){
        Reset();Put<uint64_t>(Layout+8,GroupBits|compute);Put<uint64_t>(Layout,sampler);
        Put<uint32_t>(State+8,98604);Put<uint32_t>(State+0xc,446);Put<uint32_t>(State+0x14,387);
        Put<uint64_t>(Layout+0x28,compute|sampler);Put<uint64_t>(State+0x70,untouched);
        // Visibility without resource/sampler classification must not emit either.
        Put<uint64_t>(Layout+0x18,(1ull<<2)|unclassified);
        const auto original=Capture(stages);
        Require(original.graphicsMask==GroupBits && original.dirty==untouched && flush::Room(original),
            "Non-emitting dirty tables blocked the camera scope");
        Require(original.count==(stages==1?4u:stages==2?3u:7u),"Invisible sampler was counted as a camera copy");
        Replace(original);const auto pending=Capture(stages);
        Require(pending.dirty==(untouched|original.groups) && flush::Pending(original,pending,Replacement),
            "Replacement lost unrelated dirty bits");
        auto changed=pending;changed.dirty&=~sampler;
        Require(!flush::Pending(original,changed,Replacement),"Pending ignored disappearance of invisible sampler dirtiness");
        changed=pending;changed.dirty|=1ull<<46;
        Require(!flush::Pending(original,changed,Replacement),"Pending ignored new invisible dirty bits");
        changed=pending;changed.rebind=sampler;
        Require(!flush::Pending(original,changed,Replacement),"Invisible cached rebind incorrectly visibility-filtered");
        Emit(pending);const auto emitted=Capture(stages);
        Require(flush::Flushed(pending,emitted) && emitted.dirty==untouched &&
            emitted.cursor==original.cursor+original.count,"Flush consumed an invisible table or lost its dirty bit");
        changed=emitted;changed.dirty=0;
        Require(!flush::Flushed(pending,changed),"Flush accepted removal of unrelated dirty bits");
        changed=emitted;changed.dirty|=1ull<<46;
        Require(!flush::Flushed(pending,changed),"Flush accepted new unrelated dirty bits");
        changed=emitted;changed.rebind=sampler;
        Require(!flush::Flushed(pending,changed),"Flush accepted a visibility-filtered cached rebind");
        for(const auto& table:original.tables)
            Put<uint64_t>(Descriptors+uint64_t(table.start+1-table.first)*8,table.descriptors[1-table.first]);
        Put<uint64_t>(State+0x70,untouched|original.groups);const auto restoring=Capture(stages);Emit(restoring);
        const auto restored=Capture(stages);
        Require(flush::Flushed(restoring,restored) && restored.dirty==untouched &&
            restored.cursor==original.cursor+2*original.count,"Restoration modified invisible pending work");
        Put<uint64_t>(State+0x78,sampler);Require(!flush::Room(Capture(stages)),"Invisible cached rebind admitted by Room");
    }
}
void ActiveVisibilityAndDrift() {
    constexpr uint64_t sampler=1ull<<40,third=1ull<<41,fourth=1ull<<42;
    // The emission mask includes every active native program, not just requested camera stages.
    Reset();Put<uint64_t>(State+0x70,1ull<<7);Require(!flush::Room(Capture(1)),"Active PS dirtiness ignored by VS-only scope");
    Reset();Put<uint64_t>(State+0x70,1ull<<2);Require(!flush::Room(Capture(2)),"Active VS dirtiness ignored by PS-only scope");
    for(const uint32_t stages:{1u,2u,3u}){
        Reset();Put<uint64_t>(Layout,sampler);Put<uint64_t>(Layout+0x18,(1ull<<2)|sampler);
        Put<uint64_t>(State+0x70,sampler);Require(!flush::Room(Capture(stages)),"Visible sampler copy admitted as unrelated work");
        Reset();Put<uint64_t>(Layout+8,GroupBits|third|fourth);
        Put<uint64_t>(Layout+0x28,third);Put<uint64_t>(Layout+0x30,fourth);
        Put<uint64_t>(State+0x70,third|fourth);
        for(const uint32_t program:{0u,1u,0x7fffffffu,0x80000000u,0xffffffffu}){
            Put<uint32_t>(NativeContext+0x10c,program);const auto snapshot=Capture(stages);
            const bool active=program!=0 && program<=0x7fffffffu;
            Require(snapshot.graphicsMask==(GroupBits|(active?third:0)) && flush::Room(snapshot)==!active,
                "Third native program visibility does not match unsigned validity test");
        }
        Put<uint32_t>(NativeContext+0x10c,1);Put<uint64_t>(State+0x70,fourth);
        Require(flush::Room(Capture(stages)),"Fourth native visibility lane was activated");
    }
    for(const uint32_t inactive:{0u,0x80000000u,0xffffffffu}){
        Reset();Put<uint32_t>(NativeContext+0x3c4,inactive);Put<uint64_t>(State+0x70,1ull<<7);
        Require(flush::Room(Capture(1)),"Inactive unrequested PS incorrectly emits dirty tables");
        Reset();Put<uint32_t>(NativeContext+0x3c0,inactive);Put<uint64_t>(State+0x70,1ull<<2);
        Require(flush::Room(Capture(2)),"Inactive unrequested VS incorrectly emits dirty tables");
    }
    Reset();flush::Snapshot once;Require(flush::ReadOnce(ReadMemory,Current,3,once),"Visibility baseline read failed");const auto firstReads=reads;
    struct Change {uint64_t address,value;size_t bytes;};
    for(const auto& change:std::array<Change,8>{{
        {NativeContext+0x3c0,102,4},{NativeContext+0x3c4,203,4},{NativeContext+0x10c,1,4},
        {Layout+8,GroupBits|third,8},{Layout,sampler,8},{Layout+0x18,(1ull<<2)|fourth,8},
        {Layout+0x28,third,8},{Layout+0x30,fourth,8}}}){
        Reset();mutateAt=firstReads+1;mutationAddress=change.address;mutationValue=change.value;mutationBytes=change.bytes;
        flush::Snapshot out;Require(!flush::Read(ReadMemory,Current,3,out),"Program/layout mask drift survived double-read");
    }
    Reset();const auto original=Capture();Replace(original);const auto pending=Capture();Emit(pending);const auto emitted=Capture();
    const auto reject=[&](const flush::Snapshot& changedPending,const flush::Snapshot& changedEmitted){
        Require(!flush::Pending(original,changedPending,Replacement) && !flush::Flushed(pending,changedEmitted),
            "Program/layout mask drift survived pending/flushed invariants");
    };
    for(size_t stage=0;stage<3;++stage){auto p=pending,e=emitted;p.programs[stage]++;e.programs[stage]++;reject(p,e);}
    for(size_t stage=0;stage<4;++stage){auto p=pending,e=emitted;p.visibility[stage]^=fourth;e.visibility[stage]^=fourth;reject(p,e);}
    auto p=pending,e=emitted;p.resourceMask^=third;e.resourceMask^=third;reject(p,e);
    p=pending;e=emitted;p.samplerMask^=sampler;e.samplerMask^=sampler;reject(p,e);
    p=pending;e=emitted;p.graphicsMask^=fourth;e.graphicsMask^=fourth;reject(p,e);
}
void CachedHeapInitializationAndDrift() {
    for(const uint8_t flags:{uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(3)}){
        Reset();Put<uint8_t>(State+0x60,flags&1);Put<uint8_t>(State+0x61,(flags>>1)&1);
        Put<uint64_t>(State+0x50,(flags&1)?CpuHeapBase:0x1111);
        Put<uint64_t>(State+0x58,(flags&2)?GpuHeapBase:0x2222);
        const auto original=Capture();Replace(original);const auto pending=Capture();
        Require(flush::Pending(original,pending,Replacement),"Stable heap caches rejected before flush");
        Emit(pending);const auto emitted=Capture();
        Require(emitted.cpuValid==1 && emitted.gpuValid==1 && emitted.cachedCpu==CpuHeapBase && emitted.cachedGpu==GpuHeapBase &&
            flush::Flushed(pending,emitted) && flush::SameAllocation(original,emitted),"Cold heap cache initialization rejected");
        auto hotPending=emitted;hotPending.dirty|=hotPending.groups;
        auto hotEmitted=emitted;hotEmitted.cursor+=hotEmitted.count;
        Require(flush::Pending(emitted,hotPending,Replacement) && flush::Flushed(hotPending,hotEmitted),"Stable hot heap caches rejected");
        for(unsigned field=0;field<4;++field){
            auto changedPending=hotPending,changedEmitted=hotEmitted;
            switch(field){
            case 0:changedPending.cachedCpu+=32;changedEmitted.cachedCpu+=32;break;
            case 1:changedPending.cachedGpu+=32;changedEmitted.cachedGpu+=32;break;
            case 2:changedPending.cpuValid=0;changedEmitted.cpuValid=0;break;
            default:changedPending.gpuValid=0;changedEmitted.gpuValid=0;break;
            }
            Require(!flush::SameAllocation(emitted,changedEmitted) && !flush::Pending(emitted,changedPending,Replacement) &&
                !flush::Flushed(hotPending,changedEmitted),"Already-valid heap cache changed or became invalid");
        }
    }
    Reset();Put<uint64_t>(State+0x50,CpuHeapBase);Put<uint64_t>(State+0x58,GpuHeapBase);
    Put<uint8_t>(State+0x60,1);Put<uint8_t>(State+0x61,1);
    flush::Snapshot once;Require(flush::ReadOnce(ReadMemory,Current,3,once),"Cached heap baseline read failed");const auto firstReads=reads;
    struct Change {uint64_t address,value;size_t bytes;};
    for(const auto& change:std::array<Change,4>{{
        {State+0x50,CpuHeapBase+32,8},{State+0x58,GpuHeapBase+32,8},{State+0x60,0,1},{State+0x61,0,1}}}){
        Reset();Put<uint64_t>(State+0x50,CpuHeapBase);Put<uint64_t>(State+0x58,GpuHeapBase);
        Put<uint8_t>(State+0x60,1);Put<uint8_t>(State+0x61,1);
        mutateAt=firstReads+1;mutationAddress=change.address;mutationValue=change.value;mutationBytes=change.bytes;
        flush::Snapshot out;Require(!flush::Read(ReadMemory,Current,3,out),"Heap cache drift survived double-read");
    }
    Reset();mutateAt=firstReads+1;mutationAddress=State+0x60;mutationValue=1;mutationBytes=1;
    flush::Snapshot out;Require(!flush::Read(ReadMemory,Current,3,out),"Cache initialization during double-read accepted as stable");
}
}
int main()try{
    StageGroupsAndCompleteReads();InvalidFields();ReadFailuresAndChanges();PendingAndFlushed();AllocationAndBudget();
    InvisibleDirtyPreserved();ActiveVisibilityAndDrift();CachedHeapInitializationAndDrift();
    std::puts("PASS native camera flush snapshots, visibility, unrelated dirty work, restoration and no-rollover budget");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
