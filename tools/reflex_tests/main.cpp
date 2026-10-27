#include "Hooks/ReflexOptions.hpp"
#include "Hooks/ReflexTiming.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace cvr::reflex;
void Check(bool ok,const char* message) {if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
int main() {
    static_assert(DefaultMode==0,"Reflex must be disabled by default");
    TimingState timing;
    Check(timing.version==1 && timing.frames[63].version==1,"Timing ABI initialization failed");
    Options input;int extension=7;
    input.next=&extension;input.mode=2;input.frameLimitUs=11111;
    input.useMarkersToOptimize=1;input.virtualKey=124;input.idThread=8656;
    input.padding=0xaabbccdd;input.padding2=0x7e;
    const auto before=input;
    for(int mode=0;mode<=1;++mode) {
        Options result;
        Check(OverrideOptions(input,mode,true,result),"Override not applied");
        Check(result.mode==mode,"Wrong latency mode");
        result.mode=input.mode;
        Check(std::memcmp(&result,&before,sizeof(result))==0,"Native cap/markers/extensions/padding changed");
        Check(std::memcmp(&input,&before,sizeof(input))==0,"Caller's options were mutated");
    }
    Options output;
    Check(OverrideOptions(input,DefaultMode,true,output) && output.mode==0,"Default Reflex mode is not Off");
    Check(NormalizeMode(-1)==-1,"Explicit game-controlled mode must remain available");
    const int invalidModes[]={-2,-1,3};
    for(int mode:invalidModes)Check(!OverrideOptions(input,mode,true,output),"Invalid/game mode must pass through");
    Check(!OverrideOptions(input,0,false,output),"Non-XR renderer must pass through");
    Check(!OverrideOptions(input,2,true,output),"Unchanged options should retain original pointer");
    input.version=2;Check(!OverrideOptions(input,0,true,output),"Unknown ABI version must pass through");
    input=before;input.type[1]^=1;Check(!OverrideOptions(input,0,true,output),"Unknown struct must pass through");
    Check(NormalizeMode(3)==-1 && NormalizeMode(-2)==-1,"Invalid config must follow game");
    std::puts("Reflex option ABI and native field preservation passed");
}
