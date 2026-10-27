#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
struct Registers { std::array<uint64_t,8> gpr;std::array<uint8_t,96> xmm; };
extern "C" {
void TouchOriginal();void CvrWorldTouchTriangleThunk();void TouchProbe(void(*)(),Registers*);
void* CvrWorldTouchTriangleOriginal=reinterpret_cast<void*>(&TouchOriginal);
uint32_t TouchError{},TouchCalls{};
}
int main(){
    Registers original{},hooked{};
    TouchProbe(&TouchOriginal,&original);TouchProbe(&CvrWorldTouchTriangleThunk,&hooked);
    if(TouchError || TouchCalls!=1 || std::memcmp(&original,&hooked,sizeof(original))){
        std::printf("FAIL world touch leaf ABI error=%u callbacks=%u\n",TouchError,TouchCalls);return 1;
    }
    std::puts("PASS world touch: seven args, stack alignment, callback and all post-call volatile registers/flags");
}
