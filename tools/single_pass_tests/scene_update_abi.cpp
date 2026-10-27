#include "Render/NativeSceneUpdate.hpp"
#include "Render/NativeSceneScopeLiveness.hpp"
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <cstddef>

using cvr::stereo::scene_update::ThunkFrame;
namespace liveness=cvr::stereo::scene_update::scope_liveness;
static_assert(offsetof(ThunkFrame,xmm)==0x40 && offsetof(ThunkFrame,caller)==0xA0 &&
    offsetof(ThunkFrame,stack)==0xA8 && offsetof(ThunkFrame,arg5)==0xB0);
extern "C" {
void SceneUpdateNative();void SceneUpdateClobber();void SceneUpdateProbe(void*,ThunkFrame*);
void CvrSceneUpdateQueueDetour();void CvrSceneUpdateProducerDetour();void CvrSceneUpdateGeneralDetour();void CvrSceneUpdatePreDetour();
void CvrSceneUpdateMembershipDetour();void CvrSceneUpdatePostDetour();void CvrSceneUpdateGeneralRunnerDetour();void CvrSceneUpdateLaneRunnerDetour();
void* CvrSceneUpdateQueueOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
void* CvrSceneUpdateProducerOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
void* CvrSceneUpdateGeneralOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
void* CvrSceneUpdatePreOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
void* CvrSceneUpdateMembershipOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
void* CvrSceneUpdatePostOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
void* CvrSceneUpdateGeneralRunnerOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
void* CvrSceneUpdateLaneRunnerOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
ThunkFrame SceneUpdateNativeInputs{};
}
namespace {
uint32_t beforeCalls{},afterCalls{},lastBefore{},lastAfter{};bool validCookie=true,scopeMode{};
uint64_t nextCookie{},expectedCookie{};liveness::Anchor scopeAnchor{},staleAnchor{};
liveness::Life currentLife{},staleLife{};bool budgetUnknown{};uint32_t attributed{};
struct NativeScopeException {};
uint64_t Thunk(uint32_t event){
    void* functions[]{reinterpret_cast<void*>(&CvrSceneUpdateQueueDetour),reinterpret_cast<void*>(&CvrSceneUpdateProducerDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateGeneralDetour),reinterpret_cast<void*>(&CvrSceneUpdatePreDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateMembershipDetour),reinterpret_cast<void*>(&CvrSceneUpdatePostDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateGeneralRunnerDetour),reinterpret_cast<void*>(&CvrSceneUpdateLaneRunnerDetour)};
    return event<8?reinterpret_cast<uintptr_t>(functions[event]):0;
}
__declspec(noinline) void BoundsStyleProbe(){
    if(liveness::Inspect({&staleAnchor,1})[0]==liveness::Life::Live)++attributed;
}
}
extern "C" uint64_t CvrSceneUpdateBefore(uint32_t event,const ThunkFrame* frame)noexcept{
    ++beforeCalls;lastBefore=event;
    if(!frame || frame->rcx!=0x22222222 || frame->rdx!=0x33333333 || frame->r8!=0x44444444 || frame->r9!=0x55555555)validCookie=false;
    if(event==0 && frame->arg5!=0x99999999)validCookie=false;
    expectedCookie=scopeMode?0x123456000+ ++nextCookie:0xABC000+event;
    if(scopeMode && frame)scopeAnchor=liveness::Capture(expectedCookie,frame->stack,Thunk(event));
    SceneUpdateClobber();return expectedCookie;
}
extern "C" void CvrSceneUpdateAfter(uint32_t event,uint64_t cookie,const ThunkFrame* frame)noexcept{
    ++afterCalls;lastAfter=event;
    if(cookie!=expectedCookie || !frame || frame->rax!=0xAAAAAAAA)validCookie=false;
    SceneUpdateClobber();
}
extern "C" __declspec(noinline) uint64_t SceneUpdateScopeLive(){
    const std::array anchors{scopeAnchor,staleAnchor};const auto evidence=liveness::Inspect(anchors);
    currentLife=evidence[0];staleLife=evidence[1];budgetUnknown=liveness::Inspect({&scopeAnchor,1},0,0)[0]==liveness::Life::Unknown;
    return 0xAAAAAAAA;
}
__declspec(noinline) void SceneUpdateScopeThrow(){
    currentLife=liveness::Inspect({&scopeAnchor,1})[0];throw NativeScopeException{};
}
static void Require(bool value){if(!value)throw std::runtime_error("Scene-update native ABI changed");}
int main()try{
    ThunkFrame baseline{};SceneUpdateProbe(reinterpret_cast<void*>(&SceneUpdateNative),&baseline);const auto input=SceneUpdateNativeInputs;
    void* detours[]{reinterpret_cast<void*>(&CvrSceneUpdateQueueDetour),reinterpret_cast<void*>(&CvrSceneUpdateProducerDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateGeneralDetour),reinterpret_cast<void*>(&CvrSceneUpdatePreDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateMembershipDetour),reinterpret_cast<void*>(&CvrSceneUpdatePostDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateGeneralRunnerDetour),reinterpret_cast<void*>(&CvrSceneUpdateLaneRunnerDetour)};
    for(uint32_t event=0;event<8;++event){ThunkFrame observed{};beforeCalls=afterCalls=0;validCookie=true;
        SceneUpdateProbe(detours[event],&observed);
        Require(!std::memcmp(&observed,&baseline,offsetof(ThunkFrame,caller)));
        Require(!std::memcmp(&SceneUpdateNativeInputs,&input,offsetof(ThunkFrame,caller)));
        Require(beforeCalls==1 && lastBefore==event && validCookie);
        Require(afterCalls==(event?1u:0u));if(event)Require(lastAfter==event);
        // Shared scheduler tail jump preserves native caller RA and fifth arg.
        if(!event)Require(SceneUpdateNativeInputs.caller==input.caller && SceneUpdateNativeInputs.arg5==input.arg5);
    }
    scopeMode=true;CvrSceneUpdateGeneralOriginal=reinterpret_cast<void*>(&SceneUpdateScopeThrow);
    ThunkFrame ignored{};beforeCalls=afterCalls=0;validCookie=true;bool caught{};
    try{SceneUpdateProbe(reinterpret_cast<void*>(&CvrSceneUpdateGeneralDetour),&ignored);}catch(const NativeScopeException&){caught=true;}
    Require(caught && beforeCalls==1 && !afterCalls && currentLife==liveness::Life::Live && validCookie);
    staleAnchor=scopeAnchor;Require(liveness::Inspect({&staleAnchor,1})[0]==liveness::Life::Absent);
    BoundsStyleProbe();Require(!attributed);
    auto mismatchedStack=staleAnchor;mismatchedStack.low^=0x1000;
    Require(liveness::Inspect({&mismatchedStack,1})[0]==liveness::Life::Unknown);
    CvrSceneUpdateGeneralOriginal=reinterpret_cast<void*>(&SceneUpdateScopeLive);
    beforeCalls=afterCalls=0;validCookie=true;SceneUpdateProbe(reinterpret_cast<void*>(&CvrSceneUpdateGeneralDetour),&ignored);
    Require(beforeCalls==1 && afterCalls==1 && validCookie && currentLife==liveness::Life::Live && staleLife==liveness::Life::Absent && budgetUnknown);
    BoundsStyleProbe();Require(!attributed);scopeMode=false;CvrSceneUpdateGeneralOriginal=reinterpret_cast<void*>(&SceneUpdateNative);
    std::puts("PASS scene-update queue native RA/fifth argument, all eight thunk GPR/XMM0-5/RFLAGS preservation and actual live/unwound-frame attribution rejection");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
