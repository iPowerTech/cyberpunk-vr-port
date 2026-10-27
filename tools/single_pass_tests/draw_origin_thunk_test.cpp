#include "Render/NativeDrawOrigin.hpp"
#include <cstdio>
#include <cstring>
extern "C" {
void OriginTarget();
uint64_t RunOriginThunk();
void* CvrNativeDrawOriginal=reinterpret_cast<void*>(&OriginTarget);
uint64_t CvrNativeDrawExpectedCaller{},OriginWrongCaller{},OriginAfterFlags{};
std::atomic<uint32_t> CyberpunkVR_EarlyDepthState{1},CyberpunkVR_GeometryOriginMode{1};
uint64_t OriginInputs[15]{},OriginOutputs[8]{},OriginBefore[6]{},OriginBeforeCount{},OriginAfterCount{},OriginExpectedFlags{},OriginReturnFlags{};
uint64_t OriginArgument{};
alignas(16) uint8_t OriginInputVectors[96]{},OriginOutputVectors[96]{};
alignas(16) uint8_t OriginPattern[16]{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
}
int main(){
    for(unsigned mode=0;mode<5;++mode){
        const bool active=mode==1 || mode==2;
        CyberpunkVR_GeometryOriginMode=mode>=3?1:mode;CyberpunkVR_EarlyDepthState=mode==3?0:1;
        OriginWrongCaller=mode==4;OriginBeforeCount=OriginAfterCount=0;
        if(RunOriginThunk()!=0x12345678)return 1;
        for(unsigned i=0;i<7;++i)if(OriginInputs[i]!=0x11*(i+1))return 2;
        for(unsigned i=0;i<7;++i)if(OriginInputs[7+i]!=0x88+0x11*i)return 3;
        if(OriginInputs[14]!=OriginExpectedFlags || OriginArgument!=0xA5)return 4;
        if((active?OriginAfterFlags:OriginOutputs[7])!=OriginReturnFlags)return 5;
        for(unsigned i=1;i<7;++i)if(OriginOutputs[i]!=0x100+i)return 6;
        for(unsigned i=0;i<6;++i){if(std::memcmp(OriginInputVectors+i*16,OriginPattern,16))return 7;
            for(unsigned j=0;j<16;++j)if(OriginOutputVectors[i*16+j])return 8;}
        if(OriginBeforeCount!=(active?1:0) || OriginAfterCount!=(active?1:0))return 9;
        if(active && (OriginBefore[0]!=0x22 || OriginBefore[1]!=0x33 || OriginBefore[2]!=0x44 || OriginBefore[3]!=0xAA || OriginBefore[4]!=0xDD || OriginBefore[5]!=0xBB))return 10;
    }
    std::puts("PASS native scope preserves input/output registers, vectors and stack arguments");return 0;
}
