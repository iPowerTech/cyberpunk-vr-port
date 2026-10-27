#pragma once
#include "Render/RegisteredConstantUpload.hpp"
#include "Render/NativeCommandRecording.hpp"
#include <array>
#include <memory>

namespace cvr::stereo::breadcrumb_write {
namespace detail {
using ReadMemory=bool(*)(void*,uint64_t,void*,size_t);
inline constexpr uint32_t MarkerInReturn=0x1FF1FA,MarkerOutReturn=0x1FFA22;
inline constexpr std::array<native_recording::detail::Code,17> Contract{{
    {0x1FE758,371,0x936D5B6C41CB0A2Eull},{0x1FF0D0,349,0x0141AFDA673755AAull},
    {0x1E3DD96,138,0xD34AEA9BACC63836ull},{0x1FF9B0,154,0x5CDD09B9FA4EDC68ull},
    {0x1E3DEB8,87,0xE102B78A5A6340D8ull},{0x87518C,260,0xAED12122B3DF2EF6ull},
    {0x87573C,978,0x720ACEF7FDE8F9E2ull},{0x6DB8C0,439,0xA6010802D70A9014ull},
    {0x6DBA78,270,0x2F0A902B0A1B9099ull},{0x1EDF598,104,0x5B99BB99A59BF8E5ull},
    {0x6DBD28,125,0x928F85F001892D33ull},{0x6DB828,149,0x4C7717E2EAAEDC00ull},
    {0x6DBCCC,58,0x637C36993DCDB1D5ull},{0x18BC8E4,93,0x4D4D375F4F0FE1B4ull},
    {0x148FE78,35,0xAB691D90DEA01DD6ull},{0x2A48B88,41,0x4696DE7BF1F8BB56ull},
    {0x1E1F8E0,12,0x8955E0D7506A7358ull}
}};
inline bool Read(ReadMemory read,void* user,uint64_t address,void* output,size_t bytes){
    return read && address && bytes && address<=UINT64_MAX-bytes && read(user,address,output,bytes);
}
template<class T> bool Field(ReadMemory read,void* user,uint64_t address,uint64_t offset,T& output){
    return address && address<=UINT64_MAX-offset && Read(read,user,address+offset,&output,sizeof(output));
}
inline uint32_t CallerMode(uint64_t base,uint64_t caller){
    if(!base || caller<base)return 0;
    if(caller-base==MarkerInReturn)return 1;
    if(caller-base==MarkerOutReturn)return 2;
    return 0;
}
// Frames are OS-unwound return addresses, never a scan for plausible values.
inline uint32_t NearestCaller(uint64_t base,uint64_t imageBytes,std::span<const uint64_t> frames){
    if(!base || !imageBytes || base>UINT64_MAX-imageBytes || frames.size()>64)return 0;
    for(const auto rip:frames)if(rip>=base && rip-base<imageBytes)return CallerMode(base,rip);
    return 0;
}
inline bool Parameters(uint32_t callerMode,UINT count,const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER& value,uint32_t mode){
    return count==1 && (callerMode==1 || callerMode==2) && mode==callerMode && value.Value==callerMode &&
        value.Dest && !(value.Dest&3) && value.Dest<=UINT64_MAX-4;
}
struct Observation {
    uint64_t base{},registry{},device{},holder{},list{},marker{},entry{},heap{},resource{},gpu{},slotGpu{},bitmap{};
    uint32_t kind{},slot{},capacity{},stride{},slots{},words{};
    bool operator==(const Observation&)const=default;
};
struct Frame {
    uint64_t rip{},rsp{},rbp{},holder{},stackLow{},stackHigh{};
    bool operator==(const Frame&)const=default;
};
struct FrameLayout {uint64_t parameters{},modes{};uint32_t mode{};};
inline bool StackSpan(const Frame& frame,uint64_t address,uint64_t bytes){
    return frame.stackLow && frame.stackLow<frame.stackHigh && frame.stackHigh>=bytes &&
        address>=frame.stackLow && address<=frame.stackHigh-bytes;
}
inline bool Layout(uint64_t base,const Frame& frame,FrameLayout& output){
    output={};const auto mode=CallerMode(base,frame.rip);
    if(!mode || !frame.holder || frame.holder>UINT64_MAX-8 || frame.rsp%16 ||
        frame.rsp>UINT64_MAX-0x54 || !StackSpan(frame,frame.rsp,8))return false;
    if(mode==1 && frame.rbp!=frame.rsp+0x30)return false;
    FrameLayout value{frame.rsp+0x20,frame.rsp+(mode==1?0x50:0x40),mode};
    if(!StackSpan(frame,value.parameters,sizeof(D3D12_WRITEBUFFERIMMEDIATE_PARAMETER)) ||
        !StackSpan(frame,value.modes,sizeof(D3D12_WRITEBUFFERIMMEDIATE_MODE)))return false;
    output=value;return true;
}
// Only the audited nonvolatile argument holder is recovered: RDI in marker-in,
// RBX in marker-out. RBP is a frame pointer only in marker-in, not marker-out.
inline bool FrameFromRegisters(uint64_t base,uint64_t rip,uint64_t rsp,uint64_t rbp,uint64_t rdi,uint64_t rbx,
                               uint64_t low,uint64_t high,Frame& output){
    output={};const auto mode=CallerMode(base,rip);if(!mode)return false;
    Frame value{rip,rsp,mode==1?rbp:0,mode==1?rdi:rbx,low,high};FrameLayout layout;
    if(!Layout(base,value,layout))return false;output=value;return true;
}
// Reader failures: 1 arguments/frame, 2 globals/holder, 3 marker association,
// 4 factory shape, 5 heap/buffer, 6 bitmap/lock, 7 slot range, 8 destination.
// 9 native stack arguments differ. Padding bytes are not compared.
inline bool Observe(ReadMemory read,void* user,uint64_t base,const Frame& frame,uint64_t actualList,
                    const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER& parameter,uint32_t mode,
                    Observation& output,uint32_t* failure=nullptr){
    output={};if(failure)*failure=0;Observation value;value.base=base;value.holder=frame.holder;value.list=actualList;
    const auto fail=[&](uint32_t code){if(failure)*failure=code;output=value;return false;};
    FrameLayout layout;
    if(!base || base>UINT64_MAX-0x3494018 || !actualList || !Layout(base,frame,layout) ||
        !Parameters(layout.mode,1,parameter,mode))return fail(1);
    const auto destination=parameter.Dest;D3D12_WRITEBUFFERIMMEDIATE_PARAMETER native{};uint32_t nativeMode{};
    if(!Read(read,user,layout.parameters,&native,sizeof(native)) || !Read(read,user,layout.modes,&nativeMode,4) ||
        native.Dest!=parameter.Dest || native.Value!=parameter.Value || nativeMode!=mode)return fail(9);
    uint8_t stopped{};
    if(!Field(read,user,base,0x3438A28,value.registry) || !value.registry ||
        !Field(read,user,value.registry,1,stopped) || stopped ||
        !Field(read,user,base,0x3493DD0,value.device) || !value.device ||
        !Field(read,user,frame.holder,0,value.marker) || !value.marker)return fail(2);
    uint64_t markerList{};
    if(!Field(read,user,value.marker,0,markerList) || markerList!=actualList ||
        !Field(read,user,value.marker,8,value.kind) || !value.kind || value.kind>4 ||
        !Field(read,user,value.marker,0x10,value.capacity) ||
        !Field(read,user,value.marker,0x60,value.slotGpu) || !value.slotGpu ||
        !Field(read,user,value.marker,0x68,value.slot) || !value.slot)return fail(3);
    value.entry=base+0x3493DD8+uint64_t(value.kind)*0x70;
    const uint32_t expectedSlots=value.kind==1?1024:value.kind==4?64:8;
    const uint32_t expectedStride=value.kind==1?4096:value.kind==4?2048:512;
    if(!Field(read,user,value.entry,0x20,value.stride) || value.stride!=expectedStride || value.capacity!=expectedStride/4 ||
        !Field(read,user,value.entry,0x60,value.slots) || value.slots!=expectedSlots || value.slot>value.slots ||
        !Field(read,user,value.entry,0x5C,value.words) || value.words!=(value.slots+63)/64)return fail(4);
    if(!Field(read,user,value.entry,0x30,value.heap) || !value.heap ||
        !Field(read,user,value.entry,0x38,value.resource) || !value.resource ||
        !Field(read,user,value.entry,0x40,value.gpu) || !value.gpu)return fail(5);
    uint8_t lock{};uint64_t word{};
    if(!Field(read,user,value.entry,0x48,lock) || lock || !Field(read,user,value.entry,0x50,value.bitmap) || !value.bitmap ||
        !Field(read,user,value.bitmap,uint64_t((value.slot-1)/64)*8,word) || !(word&(uint64_t(1)<<((value.slot-1)%64))) ||
        !Field(read,user,value.entry,0x48,lock) || lock)return fail(6);
    const uint64_t total=uint64_t(value.stride)*value.slots,offset=uint64_t(value.slot-1)*value.stride;
    if(value.gpu>UINT64_MAX-total || value.slotGpu!=value.gpu+offset || value.slotGpu>UINT64_MAX-value.stride)return fail(7);
    if(destination<value.slotGpu || destination-value.slotGpu>value.stride-4)return fail(8);
    output=value;return true;
}
inline bool Shape(const Observation& observation,const D3D12_RESOURCE_DESC& desc,const D3D12_HEAP_DESC& heap,
                  const D3D12_RESOURCE_ALLOCATION_INFO& allocation,uint64_t gpu){
    const auto flags=D3D12_RESOURCE_FLAGS(D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE|
        ((heap.Flags&D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER)?D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER:0));
    return gpu==observation.gpu && desc.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER &&
        desc.Width==uint64_t(observation.stride)*observation.slots && desc.Height==1 && desc.DepthOrArraySize==1 && desc.MipLevels==1 &&
        desc.Format==DXGI_FORMAT_UNKNOWN && desc.SampleDesc.Count==1 && !desc.SampleDesc.Quality &&
        desc.Layout==D3D12_TEXTURE_LAYOUT_ROW_MAJOR && desc.Flags==flags &&
        allocation.SizeInBytes && allocation.SizeInBytes!=UINT64_MAX && allocation.SizeInBytes>=desc.Width &&
        allocation.Alignment && allocation.SizeInBytes<=heap.SizeInBytes &&
        !(allocation.SizeInBytes%allocation.Alignment) && (!heap.Alignment || heap.Alignment==allocation.Alignment);
}
inline bool ObservedCreationMatches(const constants::ResourceCreationInfo& value,const Observation& native,
                                   uint64_t allocationBytes){
    return value.kind==2 && reinterpret_cast<uintptr_t>(value.resource.Get())==native.resource &&
        reinterpret_cast<uintptr_t>(value.heap.Get())==native.heap && !value.heapOffset && value.allocationBytes==allocationBytes &&
        (!value.initialStateKnown || value.initialState==D3D12_RESOURCE_STATE_COPY_DEST);
}
}
enum class Provenance:uint32_t {ObservedCreation=1,NativeFactory=2};
struct Snapshot {
    constants::ResourceCreationInfo creation;
    D3D12_RESOURCE_DESC resourceDesc{};
    D3D12_HEAP_DESC heapDesc{};
    uint64_t destination{};uint32_t value{},mode{};
    Provenance provenance{};
};
class Lease {
    struct Data;std::shared_ptr<const Data> data_;
    friend bool Capture(ID3D12GraphicsCommandList2*,UINT,const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER*,
                        const D3D12_WRITEBUFFERIMMEDIATE_MODE*,uintptr_t,Lease&);
    friend bool Describe(const Lease&,Snapshot&);
public:explicit operator bool()const{return bool(data_);}
};
// Only the actual audited native breadcrumb call. This records the buffer's
// physical identity for access classification, never grants later GPU access.
bool Capture(ID3D12GraphicsCommandList2*,UINT,const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER*,
             const D3D12_WRITEBUFFERIMMEDIATE_MODE*,uintptr_t caller,Lease&);
// Returns held, authenticated metadata. NativeFactory is not an API creation
// receipt and is never registered as one; COM retention is not allocation pinning.
bool Describe(const Lease&,Snapshot&);
uint32_t LastFailure();
// stage,detail,caller,markerHolder,marker,entry,resource,heap,gpu,slotGpu,slot,kind.
// Stages:1arguments,2contract(detail=RVA),3native frame,4parameters,
// 6native reader(detail=Observe rejection),7COM shape,8creation,9changed,10exception.
// Native-frame details101..110: image,caller,stack,nearest game frame,function
// extent,frame layout,unwind metadata,unwind progress,exception,frame depth.
std::array<uint64_t,12> LastDetails();
}
