#include "Render/PreparedMembershipBank.hpp"
#include <cstdio>
#include <stdexcept>

using namespace cvr::stereo::prepared_membership;
static void Require(bool value){if(!value)throw std::runtime_error("prepared membership bank assertion failed");}
int main()try{
    std::array<UINT,1024> bits{};UINT count{},maximum=UINT_MAX;
    Require(detail::Mark(bits,0,32768,count,maximum) && count==1 && maximum==0 && bits[0]==1);
    Require(detail::Mark(bits,31,32768,count,maximum) && count==2 && maximum==31 && bits[0]==0x80000001u);
    Require(detail::Mark(bits,32,32768,count,maximum) && count==3 && maximum==32 && bits[1]==1);
    Require(detail::Mark(bits,32767,32768,count,maximum) && count==4 && maximum==32767 && bits.back()==0x80000000u);
    Require(detail::Mark(bits,32767,32768,count,maximum) && count==4);
    const auto unchanged=bits;
    Require(!detail::Mark(bits,32768,65536,count,maximum) && bits==unchanged && count==4);
    Require(!detail::Mark(bits,500,500,count,maximum) && bits==unchanged);
    Require(!detail::Mark(bits,0,0,count,maximum) && bits==unchanged);
    std::puts("prepared membership bitmap bounds passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
