#include "Render/NativeInstanceWitness.hpp"
#include <cstdio>
#include <stdexcept>
namespace witness=cvr::stereo::instance_witness;
static void Require(bool value){if(!value)throw std::runtime_error("Witness patch plan assertion failed");}
int main()try{
    constexpr std::array<uint8_t,81> original{
        0x48,0x89,0x5c,0x24,0x08,0x4c,0x8b,0xd1,0x8b,0xda,0x4d,0x8b,0x4a,0x40,0x41,0x8b,0x4a,0x10,
        0x4d,0x8b,0xd9,0x44,0x8d,0x41,0xff,0x45,0x23,0xc1,0x41,0x8d,0x04,0x10,0x3b,0xc1,0x77,0x1b,
        0x4a,0x8d,0x0c,0x1b,0x49,0x8b,0xc1,0xf0,0x49,0x0f,0xb1,0x4a,0x40,0x4c,0x3b,0xc8,0x75,0xd4,
        0x48,0x8b,0x5c,0x24,0x08,0x41,0x8b,0xc0,0xc3,0x4d,0x8d,0x59,0xff,0x45,0x33,0xc0,0x4c,0x03,
        0xd9,0x48,0xf7,0xd9,0x4c,0x23,0xd9,0xeb,0xd3};
    std::array<uint8_t,5> patch{};constexpr uintptr_t site=0x100000000;
    Require(witness::detail::CallPatch(site,site+5+INT32_MAX,patch));
    Require(patch==std::array<uint8_t,5>{0xe8,0xff,0xff,0xff,0x7f});
    Require(!witness::detail::CallPatch(site,site+6+INT32_MAX,patch));
    Require(witness::detail::CallPatch(site,site+5-uint64_t(INT32_MAX)-1,patch));
    Require(patch==std::array<uint8_t,5>{0xe8,0,0,0,0x80});
    Require(!witness::detail::CallPatch(site,site+4-uint64_t(INT32_MAX)-1,patch));
    Require(!witness::detail::CallPatch(UINTPTR_MAX-4,site,patch));
    Require(!witness::detail::CallPatch(0,site,patch));
    Require(witness::detail::CallPatch(site,site+100,patch));
    Require(witness::detail::Body(original,original,patch,false));
    auto current=original;const auto offset=witness::SiteRva-witness::AllocatorRva;
    for(size_t i=0;i<patch.size();++i)current[offset+i]=patch[i];
    Require(witness::detail::Body(original,current,patch,true));
    Require(!witness::detail::Body(original,current,patch,false));
    for(size_t index=0;index<current.size();++index){auto changed=current;changed[index]^=1;
        Require(!witness::detail::Body(original,changed,patch,true));}
    auto unknown=original;unknown[0]^=1;Require(!witness::detail::Body(unknown,unknown,patch,false));
    Require(witness::detail::PatchBoundary(site,site-1));Require(witness::detail::PatchBoundary(site,site));
    for(uintptr_t index=1;index<5;++index)Require(!witness::detail::PatchBoundary(site,site+index));
    Require(witness::detail::PatchBoundary(site,site+5));Require(!witness::detail::PatchBoundary(UINTPTR_MAX,0));
    std::puts("PASS actual CAS body/site guard, CALL displacement bounds, whole-body mutation and suspended instruction boundaries");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
