#include "Render/NativeDrawOrigin.hpp"
#include "Render/NativeEarlyDepth.hpp"
#include "Stereo/DetourRegistry.hpp"
#include <windows.h>

extern "C" {
void* CvrNativeDrawOriginal{};
uint64_t CvrNativeDrawExpectedCaller{};
void CvrNativeDrawDetour();
void CvrNativeDrawBefore(const cvr::stereo::packets::DrawEntryFrame* frame){
    using namespace cvr::stereo;
    packets::EnterDrawOrigin(early_depth::Drawing()?packets::OriginFromEntry(*frame,reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))):packets::DrawOrigin{});
}
void CvrNativeDrawAfter(){cvr::stereo::packets::LeaveDrawOrigin();}
}
namespace cvr::detail {
namespace {
bool OriginCompatible(){
    if(!GetEnvironmentVariableW(L"CVR_STEREO_PAIRED_ROOT",nullptr,0))return false;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));std::array<uint8_t,0xF0> bytes{};SIZE_T copied{};
    bool valid=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+0x1F1FA8),bytes.data(),bytes.size(),&copied) && copied==bytes.size();
    uint64_t hash=14695981039346656037ull;for(auto byte:bytes)hash=(hash^byte)*1099511628211ull;
    valid=valid && hash==0x3EFA042B17CB96ABull;
    std::array<uint8_t,0x168> caller{};copied=0;
    valid=valid && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+0x1F191C),caller.data(),caller.size(),&copied) && copied==caller.size();
    hash=14695981039346656037ull;for(auto byte:caller)hash=(hash^byte)*1099511628211ull;
    valid=valid && hash==0x39E2FE24CD281FF6ull;CvrNativeDrawExpectedCaller=valid?base+0x1F19DA:0;
    CyberpunkVR_GeometryOriginCompatible=valid?1:2;return valid;
}
CVR_DETOUR_IF("[single-pass] scoped native draw origin",0x1F1FA8,CvrNativeDrawDetour,CvrNativeDrawOriginal,OriginCompatible)
}
}
