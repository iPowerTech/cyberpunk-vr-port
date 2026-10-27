#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

struct CpuState { uint64_t gpr[7],flags;uint8_t xmm[6][16]; };
static_assert(sizeof(CpuState)==160);
using Reader=float(*)(void*,uint64_t);
using ProbeLeaf=uintptr_t(*)(uintptr_t,uintptr_t);
struct Record { uint64_t name;uint32_t pad;float value;uint8_t tail[32]; };
struct Input { uint64_t pad;Record* records;uint32_t capacity,count; };
static_assert(sizeof(Record)==0x30 && offsetof(Record,value)==0xC && offsetof(Input,count)==0x14);
bool overrideValue{};
void* observedObject{};uint64_t observedName{};float observedValue{};int callbacks{};
extern "C" {
void* CvrSwimmingActionOriginal{};
float CvrSwimmingActionDetour(void*,uint64_t);
void PoseLeafProbe(ProbeLeaf,uintptr_t,uintptr_t,CpuState*);
void PoseLeafClobber();
// The shared assembly fixture also exports a world-blend probe; this target
// never calls it and does not link that unrelated production hook.
void CvrWorldBlendNotifyDetour() { throw std::runtime_error("unexpected world-blend probe in swimming ABI test"); }
float CvrSwimmingActionAfter(void* object,uint64_t name,float original) {
    observedObject=object;observedName=name;observedValue=original;++callbacks;
    // Deliberately destroy all volatile GPR/XMM registers and flags. A
    // regular C++ callee is permitted to do this, unlike the engine leaf.
    PoseLeafClobber();
    return overrideValue ? .85f:original;
}
}
__declspec(noinline) float BrokenWrapper(void* object,uint64_t name) {
    const float original=reinterpret_cast<Reader>(CvrSwimmingActionOriginal)(object,name);
    return CvrSwimmingActionAfter(object,name,original);
}
void Check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
template<class T>T Read(const std::vector<uint8_t>& image,size_t offset) {
    Check(offset+sizeof(T)<=image.size(),"PE offset outside image");T v;std::memcpy(&v,image.data()+offset,sizeof(v));return v;
}
void* LoadActualReader(const std::vector<uint8_t>& image) {
    constexpr uint32_t rva=0x4C6C9C,size=0x2C;
    const auto pe=Read<uint32_t>(image,0x3c);
    const auto count=Read<uint16_t>(image,pe+6),opt=Read<uint16_t>(image,pe+20);
    for(int i=0;i<count;++i) {
        const auto section=pe+24+opt+40*i;
        const auto va=Read<uint32_t>(image,section+12),rawSize=Read<uint32_t>(image,section+16),raw=Read<uint32_t>(image,section+20);
        if(rva<va || rva+size>va+rawSize)continue;
        const auto offset=raw+rva-va;
        const uint8_t prefix[]={0x48,0x8B,0x41,0x08,0x8B,0x49,0x14};
        Check(offset+size<=image.size() && !std::memcmp(image.data()+offset,prefix,sizeof(prefix)),"unexpected action reader bytes");
        // No RIP-relative access or external calls; use the actual game leaf
        // on a local action table, without loading or launching the game.
        void* code=VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        Check(code!=nullptr,"code allocation failed");std::memcpy(code,image.data()+offset,size);
        DWORD old{};Check(VirtualProtect(code,size,PAGE_EXECUTE_READ,&old)!=0,"code protection failed");
        FlushInstructionCache(GetCurrentProcess(),code,size);return code;
    }
    throw std::runtime_error("native action reader RVA missing");
}
void Compare(Input& input,uint64_t name,Reader detour,bool change) {
    CpuState expected{},actual{};callbacks=0;overrideValue=change;
    const auto object=reinterpret_cast<uintptr_t>(&input);
    PoseLeafProbe(reinterpret_cast<ProbeLeaf>(CvrSwimmingActionOriginal),object,name,&expected);
    float native{};std::memcpy(&native,expected.xmm[0],4);
    PoseLeafProbe(reinterpret_cast<ProbeLeaf>(detour),object,name,&actual);
    Check(callbacks==1 && observedObject==&input && observedName==name && observedValue==native,"callback order/arguments changed");
    const char* names[]={"RAX","RCX","RDX","R8","R9","R10","R11"};bool equal=true;
    for(int i=0;i<7;++i)if(actual.gpr[i]!=expected.gpr[i]) {
        std::cerr<<names[i]<<" expected="<<std::hex<<expected.gpr[i]<<" actual="<<actual.gpr[i]<<'\n';equal=false;
    }
    Check(equal,"engine action-reader register contract violated (crash regression)");
    Check(expected.flags==actual.flags,"action-reader flags changed");
    if(change) { const float value=.85f;std::memcpy(expected.xmm[0],&value,4); }
    Check(!std::memcmp(expected.xmm,actual.xmm,sizeof(expected.xmm)),"XMM state changed beyond the returned float");
}
int main(int argc,char** argv) {
    try {
        Check(argc>=2,"game EXE path required");std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
        Check(bool(file),"cannot read game executable");std::vector<uint8_t> image(static_cast<size_t>(file.tellg()));
        file.seekg(0);file.read(reinterpret_cast<char*>(image.data()),image.size());
        CvrSwimmingActionOriginal=LoadActualReader(image);
        Record records[]={{0x11,0,-.25f,{}},{0xE386BDF96B1AB457ull,0,.5f,{}},{0x33,0,1,{}}};
        Input input{0,records,3,3};
        if(argc>2)Compare(input,records[1].name,BrokenWrapper,false);
        else {
            for(bool change:{false,true})for(uint64_t name:{uint64_t(0x11),records[1].name,uint64_t(0x99)})
                Compare(input,name,CvrSwimmingActionDetour,change);
            input.count=0;Compare(input,0x11,CvrSwimmingActionDetour,false);
        }
        VirtualFree(CvrSwimmingActionOriginal,0,MEM_RELEASE);
        std::cout<<"PASS: actual game action reader preserves GPR/flags/XMM; only return float may change\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
