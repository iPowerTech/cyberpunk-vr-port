#include "Hooks/LodFovPatch.hpp"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <stdexcept>

using namespace cvr::lod_patch;
static void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }

// Map executable sections as data. This does not load/launch the game or execute
// its entry point. Only the 21-byte arithmetic tails are copied into our fixture.
struct Image {
    HANDLE file=INVALID_HANDLE_VALUE, mapping=nullptr;
    const uint8_t* base=nullptr;
    size_t size=0;
    ~Image() { if(base)UnmapViewOfFile(base);if(mapping)CloseHandle(mapping);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file); }
    void Open(const wchar_t* path) {
        file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        Require(file!=INVALID_HANDLE_VALUE,"open game image");
        mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,nullptr);
        Require(mapping!=nullptr,"map game image without execution");
        base=static_cast<const uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
        Require(base!=nullptr,"read game image");
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        Require(dos->e_magic==IMAGE_DOS_SIGNATURE,"DOS signature");
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        Require(nt->Signature==IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"PE64 signature");
        Require(nt->FileHeader.TimeDateStamp==0x6AD55DAF,"expected 2.5 image");
        size=nt->OptionalHeader.SizeOfImage;
    }
};

struct Probe { std::array<uint8_t,0x3E0> data{}; };
struct Code {
    uint8_t* p=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    ~Code(){if(p)VirtualFree(p,0,MEM_RELEASE);}
};

static float RunTail(const uint8_t* nativeTail,float tangent,float fov,uint8_t stereoFlags,
                     bool patched,int32_t& enabled) {
    Code memory;Require(memory.p!=nullptr,"fixture allocation");auto* code=memory.p;size_t n=0;
    auto bytes=[&](std::initializer_list<uint8_t> v){for(auto x:v)code[n++]=x;};
    auto imm=[&](uint64_t v){std::memcpy(code+n,&v,8);n+=8;};
    constexpr uint64_t sentinel=0x1122334455667788ull;
    bytes({0x53,0x56,0x48,0x89,0xD3});                // preserve RBX/RSI; RBX = probe (RDX)
    bytes({0xBE,0x37,0,0,0,0x48,0xB8});imm(sentinel);// SIL = camera kind; RAX must survive
    bytes({0xF6,0x83,0x99,0x03,0,0,1});             // native 2.5 stereo flag TEST
    bytes({0xF3,0x0F,0x59,0xC0});                    // square incoming tangent
    bytes({0x40,0x88,0xB3,0x98,0x03,0,0});          // native 2.5 stereo kind MOV
    const size_t tail=n;
    std::memcpy(code+n,nativeTail,21);n+=21;          // actual game MULSS/MAXSS/store
    const float one=1.0f;
    std::memcpy(code+0x300,&kScale,4);std::memcpy(code+0x304,&one,4);
    const int32_t toScale=int32_t(0x300-(tail+8)),toFloor=int32_t(0x304-(tail+16));
    std::memcpy(code+tail+4,&toScale,4);std::memcpy(code+tail+12,&toFloor,4);
    bytes({0x0F,0x94,0x83,0x9A,0x03,0,0});          // save ZF from the stereo TEST
    bytes({0x48,0x89,0x83,0xA0,0x03,0,0});          // save RAX
    bytes({0x5E,0x5B,0xC3});                         // restore RSI/RBX and return XMM0
    if(patched) {
        Require(EmitThreshold(code+0x100,reinterpret_cast<uintptr_t>(code+0x100),
                    reinterpret_cast<uintptr_t>(&enabled),reinterpret_cast<uintptr_t>(code+0x300),
                    reinterpret_cast<uintptr_t>(code+tail+8))==54,"production trampoline emission");
        code[tail]=0xE9;const int32_t jump=int32_t(0x100-(tail+5));
        std::memcpy(code+tail+1,&jump,4);code[tail+5]=code[tail+6]=code[tail+7]=0x90;
    }
    Require(FlushInstructionCache(GetCurrentProcess(),code,4096)!=0,"flush fixture");
    Probe probe;std::memcpy(probe.data.data()+0x20,&fov,4);probe.data[0x399]=stereoFlags;
    using Fn=float(*)(float,Probe*);
    const float value=reinterpret_cast<Fn>(code)(tangent,&probe);
    float stored{},renderFov{};uint64_t savedRax{};
    std::memcpy(&stored,probe.data.data()+0x24,4);std::memcpy(&renderFov,probe.data.data()+0x20,4);
    std::memcpy(&savedRax,probe.data.data()+0x3A0,8);
    Require(std::memcmp(&value,&stored,4)==0,"threshold stored to +24");
    Require(renderFov==fov,"render FOV unchanged");
    Require(probe.data[0x398]==0x37 && probe.data[0x399]==stereoFlags,"native stereo writes intact");
    Require(probe.data[0x39A]==((stereoFlags&1)==0),"stereo TEST flags survive trampoline");
    Require(savedRax==sentinel,"RAX preserved");
    return value;
}

int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc==2,"pass Cyberpunk2077.exe path");Image image;image.Open(argv[1]);
        Require(ConstantsMatch(image.base,image.size),"native LOD constants");
        Require(Matches(image.base,image.size,kMain),"guarded MAIN entry");
        Site oldWrongSite=kMain;oldWrongSite.rva=0x422C7A;
        Require(!Matches(image.base,image.size,oldWrongSite),"reject the old accidental plane-builder match");
        size_t cases=0;
        for(const auto& site:kTails) {
            Require(Matches(image.base,image.size,site),site.name);
            for(float fov:{0.0f,30.0f,75.0f,98.25f,100.3f,110.0f,140.0f}) {
                const float tangent=std::tan(fov*0.0087266462f);
                for(uint8_t flags:{uint8_t(0),uint8_t(1)}) {
                    int32_t enabled=0;
                    const float native=RunTail(image.base+site.rva,tangent,fov,flags,false,enabled);
                    const float off=RunTail(image.base+site.rva,tangent,fov,flags,true,enabled);
                    Require(std::memcmp(&native,&off,4)==0,"disabled mode equals native instructions bit-for-bit");
                    enabled=1;
                    Require(RunTail(image.base+site.rva,tangent,fov,flags,true,enabled)==1.0f,"enabled LOD floor");
                    cases+=2;
                }
            }
        }
        std::array<uint8_t,64> rangeTest{};rangeTest.fill(0xCC);
        Require(EmitThreshold(rangeTest.data(),0x1000,0x2000,0x3000,0x100000000ull)==0,"reject unreachable continuation");
        Require(rangeTest[0]==0xCC,"out-of-range emission writes nothing");
        std::printf("PASS: MAIN + 3 native site guards; %zu on/off cases; stereo flags, kind, FOV and RAX preserved\n",cases);
        return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what());return 1; }
}
