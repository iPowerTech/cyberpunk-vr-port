#include "Render/NativeSourceSrvBinding.hpp"
#include <cstdio>
#include <map>
#include <stdexcept>

using namespace cvr::stereo::source_srv;
namespace geometry=cvr::stereo::geometry;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Memory {
    std::map<uint64_t,uint8_t> bytes;uint64_t unstable{};uint32_t unstableReads{};
    template<class T>void Put(uint64_t address,T value){const auto* p=reinterpret_cast<const uint8_t*>(&value);
        for(size_t i=0;i<sizeof(value);++i)bytes[address+i]=p[i];}
    void Zero(uint64_t address,size_t count){for(size_t i=0;i<count;++i)bytes[address+i]=0;}
    static bool Read(void* user,uint64_t address,void* output,size_t count){
        auto& memory=*static_cast<Memory*>(user);auto* p=static_cast<uint8_t*>(output);
        for(size_t i=0;i<count;++i){const auto at=memory.bytes.find(address+i);if(at==memory.bytes.end())return false;p[i]=at->second;}
        if(address==memory.unstable && ++memory.unstableReads>1 && count)p[0]^=1;return true;
    }
};
constexpr uint64_t State=0x1000,Layout=0x2000,Table=0x3000,Registry=0x4000,Context=0x5000,HandlePointer=0x6000;
constexpr uint32_t Group=23;
uint64_t GroupEntry(){return Layout+0x38+Group*16;}
void Ordinary(Memory& memory){
    memory.Put(State+0x68,Layout);memory.Put(State+0x28,Table);
    memory.Put<uint8_t>(Layout+0x4C3+0x100+5*2,Group);memory.Put<uint8_t>(Layout+0x4C3+0x100,Group);
    memory.Zero(GroupEntry(),16);memory.Put<uint32_t>(GroupEntry(),0);memory.Put<uint32_t>(GroupEntry()+4,100);
    memory.Put<uint16_t>(GroupEntry()+8,0);memory.Put<uint16_t>(GroupEntry()+10,8);memory.Put<uint8_t>(GroupEntry()+14,23);
    memory.Put<uint32_t>(State+0x80+Group*4,UINT32_MAX);memory.Put<uint64_t>(Table+105*8,0xA000);
}
}
int main()try{
    Memory memory;Ordinary(memory);memory.Put<uint32_t>(HandlePointer,2);
    ThunkFrame frame;frame.rcx=HandlePointer;frame.rdx=5;frame.r8=1;CandidateInput individual;
    Require(detail::DecodeCandidate(Route::Individual,frame,Registry,Memory::Read,&memory,individual) &&
        individual.handle==2 && individual.stage==1 && individual.shaderRegister==5,"exact individual PS t5 arguments");
    frame.r8=2;Require(!detail::Relevant(Route::Individual,frame),"compute index is not PS");
    frame.r8=3;Require(!detail::Relevant(Route::Individual,frame),"stage mask cannot replace stage index");
    frame.r8=1;frame.rdx=4;Require(detail::Relevant(Route::Individual,frame) &&
        !detail::DecodeCandidate(Route::Individual,frame,Registry,Memory::Read,&memory,individual),"t4 cannot replace a requested t5 candidate");
    frame.rdx=7;Require(!detail::Relevant(Route::Individual,frame),"untracked register ignored");
    frame.rdx=5;memory.Put<uint32_t>(HandlePointer,0);
    Require(!detail::DecodeCandidate(Route::Individual,frame,Registry,Memory::Read,&memory,individual),"zero handle rejected");
    memory.Put<uint32_t>(HandlePointer,uint32_t(MaxHandles+1));
    Require(!detail::DecodeCandidate(Route::Individual,frame,Registry,Memory::Read,&memory,individual),"observer handle bound");
    memory.Put<uint32_t>(HandlePointer,2);frame.rcx=UINT64_MAX-2;
    Require(!detail::DecodeCandidate(Route::Individual,frame,Registry,Memory::Read,&memory,individual),"handle pointer overflow");
    frame.rcx=HandlePointer;Require(detail::DecodeCandidate(Route::Individual,frame,Registry,Memory::Read,&memory,individual),"individual candidate restored");
    Slot ordinary;Require(detail::ReadSlot(Memory::Read,&memory,State,ordinary) && ordinary.cpu==0xA000 &&
        ordinary.group==23 && ordinary.parameter==23 && ordinary.offset==5 && ordinary.index==105 && detail::MatchesMode(individual,ordinary),
        "current ordinary layout, slot, and mode");
    memory.Put<uint32_t>(State+0x80+Group*4,16);Slot cachedSlot;
    Require(detail::ReadSlot(Memory::Read,&memory,State,cachedSlot) && !detail::MatchesMode(individual,cachedSlot),"cached overwrite rejects individual candidate");
    ThunkFrame cachedFrame;cachedFrame.rcx=3;cachedFrame.rdx=1;
    const auto bundleEntry=Registry+0xB50B00+2*0x44+5*8;
    memory.Zero(bundleEntry,8);memory.Put<uint32_t>(bundleEntry,2);memory.Put<uint8_t>(bundleEntry+4,2);
    CandidateInput cached;Require(detail::DecodeCandidate(Route::Cached,cachedFrame,Registry,Memory::Read,&memory,cached) &&
        cached.handle==2 && cached.bundleIndex==16 && cached.bundleEntry==bundleEntry && detail::MatchesMode(cached,cachedSlot) &&
        detail::CachedGroup(Memory::Read,&memory,cachedSlot),"exact cached material group owns PS t0..t7");
    auto forged=cachedSlot;++forged.cachedIndex;Require(!detail::MatchesMode(cached,forged),"different cached index rejected");
    memory.Put<uint8_t>(Layout+0x4C3+0x100,22);
    Require(!detail::CachedGroup(Memory::Read,&memory,cachedSlot),"cached register0 group must also contain t5");
    memory.Put<uint8_t>(Layout+0x4C3+0x100,Group);memory.Put<uint8_t>(bundleEntry+4,1);
    Require(!detail::DecodeCandidate(Route::Cached,cachedFrame,Registry,Memory::Read,&memory,cached),"texture bundle entry is not a buffer handle");
    memory.Put<uint8_t>(bundleEntry+4,2);cachedFrame.rdx=2;
    Require(!detail::Relevant(Route::Cached,cachedFrame),"cached compute group ignored");
    memory.Put<uint16_t>(GroupEntry()+10,7);Slot invalid;
    Require(!detail::ReadSlot(Memory::Read,&memory,State,invalid),"unexpected finite table extent rejected");
    memory.Put<uint16_t>(GroupEntry()+10,8);memory.Put<uint8_t>(GroupEntry()+14,22);
    Require(!detail::ReadSlot(Memory::Read,&memory,State,invalid),"wrong root parameter rejected");
    memory.Put<uint8_t>(GroupEntry()+14,23);memory.Put<uint32_t>(State+0x80+Group*4,0x80000000);
    Require(!detail::ReadSlot(Memory::Read,&memory,State,invalid),"unknown cached mode rejected");
    memory.Put<uint32_t>(State+0x80+Group*4,UINT32_MAX);
    const auto record=Registry+0x5C0AF8+0xB0;memory.Zero(record,0xB0);
    memory.Put<uint32_t>(record,1048576);memory.Put<uint8_t>(record+6,0x3B);
    memory.Put<uint64_t>(record+8,0x100000);memory.Put<uint64_t>(record+0x10,0x9000);memory.Put<uint64_t>(record+0x48,0xA000);
    std::array<uint8_t,0xB0> raw{};uint64_t address{};geometry::NativeBuffer native;
    Require(detail::ReadRecord(Memory::Read,&memory,Registry,Context,2,raw,address,native) && native.kind==3 &&
        native.handle==2 && address==record && (raw[6]&15)==0xB,"fresh indexed B0 does not inherit CBV low type");
    memory.Put<uint8_t>(record+6,0x46);
    Require(!detail::ReadRecord(Memory::Read,&memory,Registry,Context,2,raw,address,native),"class4 cannot fabricate ordinary backing");
    memory.Put<uint8_t>(record+6,0x3B);memory.unstable=record;memory.unstableReads=0;
    Require(!detail::ReadRecord(Memory::Read,&memory,Registry,Context,2,raw,address,native),"changed raw record rejected");
    memory.unstable=0;Require(detail::ReadRecord(Memory::Read,&memory,Registry,Context,2,raw,address,native),"fresh buffer record restored");
    struct Map {uint64_t buckets;uint32_t count,bucketCount;uint64_t data;uint32_t capacity,stride;};
    memory.Put<uint64_t>(Context+0x30,0x7000);memory.Put<uint32_t>(Context+0x534,0);
    memory.Put(Context+0x568,Map{0x80000,1,1,0x90000,1,16});memory.Put<uint32_t>(0x80000,0);
    memory.Put<uint32_t>(0x90000,UINT32_MAX);memory.Put<uint32_t>(0x90004,2);memory.Put<uint32_t>(0x90008,2);
    memory.Put<uint32_t>(0x9000C,0xC0);
    auto state=geometry::ReadNativeState(native,0x7000,Memory::Read,&memory);
    Require(state.status==2 && state.value==0xC0 && state.kind==3,"current native buffer map provides actual state");
    memory.Put<uint32_t>(Context+0x534,1);state=geometry::ReadNativeState(native,0x7000,Memory::Read,&memory);
    Require(state.status==4,"pending native barriers are not a shader-read state");
    memory.Put<uint32_t>(Context+0x534,0);memory.Put<uint32_t>(0x9000C,0x80000000);
    state=geometry::ReadNativeState(native,0x7000,Memory::Read,&memory);
    Require(state.status==5 && !state.value,"unknown native map state is never inferred from the C0 request");
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_UNKNOWN;view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Buffer.NumElements=8192;view.Buffer.StructureByteStride=128;
    uint64_t offset{},bytes{};
    Require(detail::ViewRange(native,0x100000,1048576,view,offset,bytes) && !offset && bytes==1048576,"exact committed structured view range");
    view.Buffer.FirstElement=1;Require(!detail::ViewRange(native,0x100000,1048576,view,offset,bytes),"structured view cannot exceed native logical range");
    view.Buffer.FirstElement=UINT64_MAX;Require(!detail::ViewRange(native,0x100000,1048576,view,offset,bytes),"structured offset multiplication overflow");
    view.Buffer.FirstElement=0;view.Buffer.StructureByteStride=64;
    Require(!detail::ViewRange(native,0x100000,1048576,view,offset,bytes),"different structured contract rejected");
    Require(detail::ViewRange(native,0x100000,1048576,view,offset,bytes,6) && bytes==524288,"t6 uses its reflected64-byte structure");
    view.Buffer.StructureByteStride=0;view.Format=DXGI_FORMAT_R32_TYPELESS;view.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
    Require(detail::ViewRange(native,0x100000,1048576,view,offset,bytes,4) && bytes==32768,"t4 raw elements are32-bit words");
    view.Format=DXGI_FORMAT_R32_UINT;Require(!detail::ViewRange(native,0x100000,1048576,view,offset,bytes,4),"typed view cannot masquerade as raw");
    for(const UINT reg:{4u,6u}){
        Ordinary(memory);memory.Put<uint8_t>(Layout+0x4C3+0x100+reg*2,Group);memory.Put<uint64_t>(Table+(100+reg)*8,0xA000+reg*32);
        frame.rdx=reg;CandidateInput input;Slot slot;
        Require(detail::DecodeCandidate(Route::Individual,frame,Registry,Memory::Read,&memory,input,reg) && input.shaderRegister==reg &&
            detail::ReadSlot(Memory::Read,&memory,State,slot,reg) && slot.offset==reg && slot.cpu==0xA000+reg*32,"independent material buffer register/slot");
        cachedFrame.rdx=1;const auto entry=Registry+0xB50B00+2*0x44+reg*8;
        memory.Zero(entry,8);memory.Put<uint32_t>(entry,2+reg);memory.Put<uint8_t>(entry+4,2);
        Require(detail::DecodeCandidate(Route::Cached,cachedFrame,Registry,Memory::Read,&memory,input,reg) && input.handle==2+reg &&
            input.bundleEntry==entry && input.shaderRegister==reg,"cached nomination uses the requested material buffer");
    }
    Require(detail::ValidXstate(7,0xE7,832) && !detail::ValidXstate(0x207,7,2048) && !detail::ValidXstate(7,7,MaxXstateBytes+1),
        "full enabled state must fit the fixed thunk bound");
    std::puts("PASS current PS t4/t5/t6 nomination, exact slot/mode/indexed-record joins and raw/structured logical bounds; no copy grant");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
