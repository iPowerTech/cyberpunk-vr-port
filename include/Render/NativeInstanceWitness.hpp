#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cvr::stereo::instance_witness {
inline constexpr uint32_t AllocatorRva=0x95B308,AllocatorBytes=81,SiteRva=0x95B33E;
inline constexpr uint64_t AllocatorFnv=0x4251259E7ABD2CB6ull;
inline constexpr std::array<uint8_t,5> OriginalSite{0x48,0x8B,0x5C,0x24,0x08};
namespace detail {
inline uint64_t Fnv(const uint8_t* bytes,size_t count)noexcept {
    uint64_t hash=14695981039346656037ull;for(size_t i=0;i<count;++i)hash=(hash^bytes[i])*1099511628211ull;return hash;
}
inline bool CallPatch(uintptr_t site,uintptr_t relay,std::array<uint8_t,5>& output)noexcept {
    output={};if(!site || !relay || site>UINTPTR_MAX-5)return false;const auto next=site+5;uint32_t relative{};
    if(relay>=next){if(relay-next>INT32_MAX)return false;relative=uint32_t(relay-next);}
    else{if(next-relay>uint64_t(INT32_MAX)+1)return false;relative=0u-uint32_t(next-relay);}
    output[0]=0xE8;for(size_t i=0;i<4;++i)output[i+1]=uint8_t(relative>>(8*i));return true;
}
inline bool Body(const std::array<uint8_t,AllocatorBytes>& original,const std::array<uint8_t,AllocatorBytes>& current,
    const std::array<uint8_t,5>& patch,bool installed)noexcept {
    constexpr size_t offset=SiteRva-AllocatorRva;
    if(Fnv(original.data(),original.size())!=AllocatorFnv || (installed && patch[0]!=0xE8))return false;
    for(size_t i=0;i<OriginalSite.size();++i)if(original[offset+i]!=OriginalSite[i])return false;
    for(size_t i=0;i<current.size();++i)if(current[i]!=(installed && i>=offset && i<offset+patch.size()?patch[i-offset]:original[i]))return false;
    return true;
}
inline bool PatchBoundary(uintptr_t site,uintptr_t instruction)noexcept {
    return site && site<=UINTPTR_MAX-OriginalSite.size() && !(instruction>site && instruction<site+OriginalSite.size());
}
}
// Prepares retained code storage and XSTATE before any game entry detours.
bool Prepare(uintptr_t gameBase)noexcept;
// With false, obtains a balanced suspension of every other process thread.
bool Install(uintptr_t gameBase,bool threadsQuiescent=false)noexcept;
bool Installed()noexcept;
}
extern "C" {
extern uint64_t CvrNativeInstanceReservationXstateMask;
extern uint32_t CvrNativeInstanceReservationXstateMode,CvrNativeInstanceReservationXstateReady;
}
