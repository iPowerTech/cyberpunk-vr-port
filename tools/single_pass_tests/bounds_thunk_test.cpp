#include <cstdint>
#include <cstring>
#include <cstdio>
extern "C" {
void CvrBoundsTarget();
void CvrBoundsDetour();
void CvrParticleDetour();
void* CvrBoundsOriginal=reinterpret_cast<void*>(&CvrBoundsTarget);
void* CvrParticleOriginal=reinterpret_cast<void*>(&CvrBoundsTarget);
uint64_t BoundsRegs[10]{},BoundsArgs[4]{},BoundsExpectedStack{},BoundsExpectedReturn{},BoundsExpectedFlags{};
uint64_t BoundsStackArgument{};
alignas(16) uint8_t BoundsVectors[96]{};
alignas(16) uint8_t BoundsPattern[16]{1,3,5,7,9,11,13,15,2,4,6,8,10,12,14,16};
uint64_t RunBoundsThunk(void*);
}
int main(){
    for(unsigned kind=0;kind<2;++kind){
    if(RunBoundsThunk(kind?reinterpret_cast<void*>(&CvrParticleDetour):reinterpret_cast<void*>(&CvrBoundsDetour))!=0x12345678)return 1;
    for(unsigned i=0;i<7;++i)if(BoundsRegs[i]!=0x11*(i+1))return 2;
    if(BoundsRegs[7]!=BoundsExpectedStack || BoundsRegs[8]!=BoundsExpectedReturn || BoundsRegs[9]!=BoundsExpectedFlags)return 3;
    if(BoundsArgs[0]!=0x22 || BoundsArgs[1]!=0x33 || BoundsArgs[2]!=0x44 || BoundsArgs[3]!=(kind?BoundsExpectedStack:BoundsExpectedReturn) || BoundsStackArgument!=0xA5)return 4;
    for(unsigned i=0;i<6;++i)if(std::memcmp(BoundsVectors+i*16,BoundsPattern,16))return 5;
    }
    std::puts("PASS native entry observers preserve volatile GPRs, XMM0-5, flags, stack, fifth argument and return address");return 0;
}
