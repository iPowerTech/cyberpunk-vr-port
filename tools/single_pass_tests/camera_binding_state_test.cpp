#include "Render/NativeCameraBindingState.hpp"
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>
using namespace cvr::stereo::camera_upload;
static std::array<uint8_t,0x9000> memory;
static uint64_t reads{},mutateAt{};static bool failRead{};
static void Require(bool yes){if(!yes)throw std::runtime_error("camera binding state assertion failed");}
template<class T> static void Put(uint64_t address,T value){Require(address>=0x10000 && address-0x10000<=memory.size()-sizeof(T));std::memcpy(memory.data()+address-0x10000,&value,sizeof(value));}
static bool Read(uintptr_t address,void* out,size_t bytes){
    if(failRead || address<0x10000 || bytes>memory.size() || address-0x10000>memory.size()-bytes)return false;
    if(++reads==mutateAt)Put<uint64_t>(0x14000+17*8,0x9999);
    std::memcpy(out,memory.data()+address-0x10000,bytes);return true;
}
static Context context{0x18000,0x10000,0x19000};
static void Reset(){
    memory={};reads=mutateAt=0;failRead=false;
    Put<uint64_t>(0x10068,0x12000);Put<uint64_t>(0x10028,0x14000);
    Put<uint8_t>(0x12455,2);Put<uint8_t>(0x12471,7);
    Put<uint32_t>(0x12038+2*16+4,16);Put<uint16_t>(0x12038+2*16+8,0);
    Put<uint32_t>(0x12038+7*16+4,48);Put<uint16_t>(0x12038+7*16+8,1);
    Put<uint64_t>(0x14000+17*8,0x1110);Put<uint64_t>(0x14000+48*8,0x2220);
}
struct Calls {std::vector<std::array<uint32_t,2>> values;uint32_t failMask{};};
static bool Bind(void* user,Context expected,uint32_t handle,uint32_t stage){
    auto& calls=*static_cast<Calls*>(user);calls.values.push_back({handle,stage});if(calls.failMask&stage)return false;
    Require(expected==context && ((stage==1 && handle==11) || (stage==2 && handle==22)));
    Put<uint64_t>(0x14000+(stage==1?17:48)*8,stage==1?0x1110:0x2220);return true;
}
int main()try{
    Reset();CpuSlot slot;
    Require(ResolveCameraSlot(Read,context.state,0,slot) && slot.index==17 && slot.parameter==2 && slot.descriptor==0x1110);
    Require(ResolveCameraSlot(Read,context.state,1,slot) && slot.index==48 && slot.parameter==7 && slot.descriptor==0x2220);
    Require(!ResolveCameraSlot(Read,context.state,2,slot));
    for(uint8_t bad:{uint8_t(64),uint8_t(255)}){Reset();Put<uint8_t>(0x12471,bad);Require(!ResolveCameraSlot(Read,context.state,1,slot));}
    Reset();Put<uint16_t>(0x12038+2*16+8,2);Require(!ResolveCameraSlot(Read,context.state,0,slot));
    Reset();Put<uint32_t>(0x12038+2*16+4,8191);Require(!ResolveCameraSlot(Read,context.state,0,slot));
    Reset();Put<uint64_t>(0x10028,UINT64_MAX-8);Require(!ResolveCameraSlot(Read,context.state,0,slot));
    Reset();mutateAt=7;Require(!ResolveCameraSlot(Read,context.state,0,slot));
    for(uint32_t stages:{1u,2u,3u}){
        Reset();BindingTracker tracker;tracker.Observe(Read,context,11,1);tracker.Observe(Read,context,22,2);
        BindingScope scope;Require(tracker.Capture(Read,context,stages,scope));
        Require(scope.original[0].handle==11 && scope.original[1].handle==22);
        scope.replacement=0x8880;scope.applied=stages;
        if(stages&1)Put<uint64_t>(0x14000+17*8,scope.replacement);
        if(stages&2)Put<uint64_t>(0x14000+48*8,scope.replacement);
        Calls calls;Require(RestoreScope(Read,context,scope,Bind,&calls) && !scope.applied && scope.restored==stages && !scope.preserved);
        const auto count=calls.values.size();Require(RestoreScope(Read,{},scope,Bind,&calls) && calls.values.size()==count);
    }
    Reset();BindingTracker tracker;tracker.Observe(Read,context,11,1);tracker.Observe(Read,context,22,2);BindingScope scope;
    Require(tracker.Capture(Read,context,3,scope));scope.replacement=0x8880;scope.applied=3;
    Put<uint64_t>(0x14000+17*8,scope.replacement);Put<uint64_t>(0x14000+48*8,0x3330);
    Calls calls;Require(RestoreScope(Read,context,scope,Bind,&calls) && scope.restored==1 && scope.preserved==2 && calls.values.size()==1);
    Reset();tracker.Clear();tracker.Observe(Read,context,11,1);Require(!tracker.Capture(Read,context,3,scope));
    tracker.Observe(Read,{},99,4);Require(tracker.Capture(Read,context,1,scope));
    tracker.Observe(Read,context,22,2);Require(tracker.Capture(Read,context,3,scope));scope.replacement=0x8880;scope.applied=3;
    Put<uint64_t>(0x14000+17*8,scope.replacement);Put<uint64_t>(0x14000+48*8,scope.replacement);calls={};calls.failMask=2;
    Require(!RestoreScope(Read,{context.context,context.state,context.list+1},scope,Bind,&calls) && calls.values.empty());
    Require(!RestoreScope(Read,context,scope,Bind,&calls) && scope.applied==2 && scope.restored==1);
    calls.failMask=0;Require(RestoreScope(Read,context,scope,Bind,&calls) && scope.restored==3 && !scope.applied);
    Put<uint64_t>(0x14000+17*8,0x4440);Require(!tracker.Capture(Read,context,1,scope));
    tracker.Clear();Require(!tracker.Capture(Read,context,1,scope));
    std::puts("PASS native CBV slot decoding, torn/bounded reads, separate VS/PS handles, scoped restoration, foreign changes and partial retry");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
