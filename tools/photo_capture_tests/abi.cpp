#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

struct Registers { std::array<uint64_t,8> gpr; std::array<uint8_t,96> xmm; };
static_assert(sizeof(Registers)==160);
extern "C" {
void NativeClamp();
void NativeReturn();
void CvrPhotoClampDetour();
bool CvrPhotoClampSkip(uint32_t*,uint32_t*);
void ProbeClamp(void(*target)(),uint32_t* width,uint32_t* height,Registers* out);
void* CvrPhotoClampOriginal=reinterpret_cast<void*>(&NativeClamp);
uint32_t CvrPhotoTestBypass{};
}
__declspec(noinline) void UnprotectedClamp(uint32_t* width,uint32_t* height) {
    if(!CvrPhotoClampSkip(width,height))
        reinterpret_cast<void(*)(uint32_t*,uint32_t*)>(CvrPhotoClampOriginal)(width,height);
}
int main() try {
    // Negative control reproduces the old ordinary-C++ leaf detour: dimensions
    // survive, but the world-picker's R8 window pointer does not.
    Registers nativeProbe{},brokenProbe{};uint32_t width=600,height=1066;
    ProbeClamp(&NativeClamp,&width,&height,&nativeProbe);
    ProbeClamp(reinterpret_cast<void(*)()>(&UnprotectedClamp),&width,&height,&brokenProbe);
    if(width!=600 || height!=1066 || nativeProbe.gpr[3]==brokenProbe.gpr[3])
        throw std::runtime_error("negative control did not reproduce the old R8 corruption");
    for(const auto size : {std::array<uint32_t,2>{456,256},{600,1066},{2560,2560},{5000,4000},{3840,2160}})
        for(uint32_t bypass : {0u,1u}) {
            Registers native{},hooked{};
            auto width=size[0],height=size[1];
            ProbeClamp(bypass ? &NativeReturn : &NativeClamp,&width,&height,&native);
            const auto expectedWidth=width,expectedHeight=height;
            width=size[0];height=size[1];CvrPhotoTestBypass=bypass;
            // The test policy deliberately destroys every volatile GPR/XMM
            // register and flags. An unprotected C++ wrapper must fail here.
            ProbeClamp(&CvrPhotoClampDetour,&width,&height,&hooked);
            if(width!=expectedWidth || height!=expectedHeight)
                throw std::runtime_error("native clamp / photo bypass changed dimensions");
            if(std::memcmp(&native,&hooked,sizeof(native)))
                throw std::runtime_error("clamp detour destroyed the native leaf register contract");
        }
    std::cout<<"PASS old R8 corruption reproduced; fixed native/bypass register equivalence: RAX, RCX, RDX, R8-R11, flags, XMM0-5; 10 cases\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
