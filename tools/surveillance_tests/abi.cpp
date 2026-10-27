#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
struct Registers {std::array<uint64_t,8> gpr;std::array<uint8_t,96> xmm;};
extern "C" {
void TestNativeInput();
void CvrSurveillanceInputDetour();
void CvrSurveillanceFinishDetour();
void NativeFinishRead();
void ProbeFinish(void(*)(),Registers*);
void ProbeInput(void(*)(),float*,Registers*);
void* CvrSurveillanceInputOriginal=reinterpret_cast<void*>(&TestNativeInput);
uint64_t CvrSurveillanceArgs[6]{};
uint32_t CvrSurveillanceError{};
float CvrSurveillanceFinishValues[6]{};
}
int main() {
    float delta[3]{};Registers direct{},hooked{};
    ProbeInput(&TestNativeInput,delta,&direct);
    const auto args=std::to_array(CvrSurveillanceArgs);
    ProbeInput(&CvrSurveillanceInputDetour,delta,&hooked);
    if(args!=std::to_array(CvrSurveillanceArgs)||CvrSurveillanceError||
       delta[0]!=1||delta[1]!=12||delta[2]!=23||std::memcmp(&direct,&hooked,sizeof(direct))) {
        std::cerr<<"input thunk lost arguments, live component, output or native registers\n";return 1;
    }
    std::cout<<"PASS native six-argument forwarding, RDI component, Euler buffer modification and volatile register/flags preservation\n";
    ProbeFinish(&NativeFinishRead,&direct);
    ProbeFinish(&CvrSurveillanceFinishDetour,&hooked);
    if(CvrSurveillanceError || std::memcmp(&direct,&hooked,sizeof(direct)) ||
       CvrSurveillanceFinishValues[0]!=0 || CvrSurveillanceFinishValues[1]!=0 ||
       CvrSurveillanceFinishValues[2]!=0 || CvrSurveillanceFinishValues[3]!=0 ||
       CvrSurveillanceFinishValues[4]!=.2f || CvrSurveillanceFinishValues[5]!=-.1f) {
        std::cerr<<"finish thunk lost stack buffers, native movss, registers or flags\n";return 1;
    }
    std::cout<<"PASS automatic-input filter stack offsets, instruction replay and register/flags preservation\n";
}
