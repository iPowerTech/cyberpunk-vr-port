#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct CpuState { uint64_t gpr[7],flags;uint8_t xmm[6][16]; };
static_assert(sizeof(CpuState)==160);
using Leaf=uintptr_t(*)(uintptr_t,uintptr_t);
uintptr_t capturedSource{},capturedDestination{};
int beforeCalls{},afterCalls{};
bool badOrder{};
extern "C" {
void* CvrPoseSetupCopyOriginal{};
void* CvrPoseSetupDefaultOriginal{};
uintptr_t CvrPoseSetupCopyDetour(uintptr_t,uintptr_t);
uintptr_t CvrPoseSetupDefaultDetour(uintptr_t,uintptr_t);
void PoseLeafProbe(Leaf,uintptr_t,uintptr_t,CpuState*);
void PoseLeafClobber();
void WorldBlendNotifyReturn();
uintptr_t WorldBlendProbeOriginal(uintptr_t,uintptr_t);
uintptr_t WorldBlendProbeHook(uintptr_t,uintptr_t);
void CvrWorldBlendBeforeNotify(uintptr_t component) {
    capturedDestination=component;++beforeCalls;PoseLeafClobber();
}
void CvrPoseSetupDefaultBefore(uintptr_t destination) {
    capturedDestination=destination;++beforeCalls;PoseLeafClobber();
}
void CvrPoseSetupCopyBefore(void* receipt,uintptr_t source) {
    capturedSource=source;++beforeCalls;
    *static_cast<uintptr_t*>(receipt)=source;PoseLeafClobber();
}
void CvrPoseSetupCopyAfter(void* receipt,uintptr_t destination) {
    capturedDestination=destination;++afterCalls;
    const auto source=*static_cast<uintptr_t*>(receipt);
    if(source!=capturedSource || std::memcmp(reinterpret_cast<void*>(source),reinterpret_cast<void*>(destination),12))badOrder=true;
    PoseLeafClobber();
}
}
// The pre-fix C++ wrapper must fail the very same register comparison.
__declspec(noinline) uintptr_t OldDefault(uintptr_t destination,uintptr_t source) {
    CvrPoseSetupDefaultBefore(destination);
    return reinterpret_cast<Leaf>(CvrPoseSetupDefaultOriginal)(destination,source);
}
void Check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
template<class T> T Read(const std::vector<uint8_t>& image,size_t offset) {
    Check(offset+sizeof(T)<=image.size(),"bad PE offset");T v{};std::memcpy(&v,image.data()+offset,sizeof(v));return v;
}
void* LoadLeaf(const std::vector<uint8_t>& image,uint32_t rva,size_t size) {
    const auto pe=Read<uint32_t>(image,0x3c);
    const auto count=Read<uint16_t>(image,pe+6),opt=Read<uint16_t>(image,pe+20);
    for(int i=0;i<count;++i) {
        const auto section=pe+24+opt+40*i;
        const auto va=Read<uint32_t>(image,section+12),rawSize=Read<uint32_t>(image,section+16),raw=Read<uint32_t>(image,section+20);
        if(rva<va || rva+size>va+rawSize)continue;
        Check(raw+rva-va+size<=image.size(),"leaf outside image");
        void* code=VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        Check(code!=nullptr,"code allocation failed");std::memcpy(code,image.data()+raw+rva-va,size);
        DWORD previous{};Check(VirtualProtect(code,size,PAGE_EXECUTE_READ,&previous)!=0,"code protection failed");
        FlushInstructionCache(GetCurrentProcess(),code,size);return code;
    }
    throw std::runtime_error("leaf RVA missing");
}
void Compare(Leaf original,Leaf detour,bool inplace=false,bool notify=false) {
    std::array<uint8_t,160> source{},destination{};
    for(size_t i=0;i<source.size();++i)source[i]=static_cast<uint8_t>(i*17+3);
    destination.fill(0xa5);if(inplace)destination=source;
    std::array<void*,73> vtable{};
    vtable[72]=reinterpret_cast<void*>(&WorldBlendNotifyReturn);
    if(notify) {
        const auto table=reinterpret_cast<uintptr_t>(vtable.data());
        std::memcpy(destination.data(),&table,sizeof(table));
    }
    const auto initial=destination;
    auto dst=reinterpret_cast<uintptr_t>(destination.data());
    auto src=reinterpret_cast<uintptr_t>(inplace ? destination.data():source.data());
    CpuState expected{},actual{};
    PoseLeafProbe(original,dst,src,&expected);const auto bytes=destination;
    destination=initial;beforeCalls=afterCalls=0;badOrder=false;
    PoseLeafProbe(detour,dst,src,&actual);
    Check(destination==bytes,"detour changed copied/default bytes");
    const char* registers[]={"RAX","RCX","RDX","R8","R9","R10","R11"};
    bool registersMatch=true;
    for(int i=0;i<7;++i)if(actual.gpr[i]!=expected.gpr[i]) {
        std::cerr<<registers[i]<<" differs: expected="<<std::hex<<expected.gpr[i]<<" actual="<<actual.gpr[i]<<'\n';
        registersMatch=false;
    }
    Check(registersMatch,"engine leaf register contract violated");
    Check(expected.flags==actual.flags,"leaf flags changed");
    Check(std::memcmp(expected.xmm,actual.xmm,sizeof(expected.xmm))==0,"leaf XMM result changed");
    Check(beforeCalls==1 && capturedDestination==dst && !badOrder,"metadata callback order/arguments changed");
    const bool copy=original==reinterpret_cast<Leaf>(CvrPoseSetupCopyOriginal);
    Check(afterCalls==(copy ? 1:0),"metadata after callback missing or duplicated");
}
int main(int argc,char** argv) {
    try {
        Check(argc>=2,"game executable path required");
        std::ifstream file(argv[1],std::ios::binary|std::ios::ate);Check(bool(file),"cannot open game executable");
        std::vector<uint8_t> image(static_cast<size_t>(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(image.data()),image.size());
        // These two verified functions are position-independent register-only
        // leaves, with no RIP-relative loads/calls; execute their actual bytes.
        CvrPoseSetupDefaultOriginal=LoadLeaf(image,0x4e96e0,0x75);
        CvrPoseSetupCopyOriginal=LoadLeaf(image,0x4e9620,0xb9);
        if(argc>2)Compare(reinterpret_cast<Leaf>(CvrPoseSetupDefaultOriginal),OldDefault);
        else {
            Compare(reinterpret_cast<Leaf>(CvrPoseSetupDefaultOriginal),CvrPoseSetupDefaultDetour);
            Compare(reinterpret_cast<Leaf>(CvrPoseSetupCopyOriginal),CvrPoseSetupCopyDetour);
            Compare(reinterpret_cast<Leaf>(CvrPoseSetupCopyOriginal),CvrPoseSetupCopyDetour,true);
            Compare(WorldBlendProbeOriginal,WorldBlendProbeHook,false,true);
        }
        std::cout<<"PASS: original and hooked leaf bytes/registers/flags/XMM agree\n";
        VirtualFree(CvrPoseSetupDefaultOriginal,0,MEM_RELEASE);VirtualFree(CvrPoseSetupCopyOriginal,0,MEM_RELEASE);
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
}
