#include "Render/InstancePartition.hpp"
#include <stdexcept>
#include <cstdio>
using namespace cvr::stereo;
void Require(bool value){if(!value)throw std::runtime_error("instance partition failed");}
int main()try{
    std::vector<uint8_t> bytes(4*48);for(unsigned i=0;i<4;++i)std::fill_n(bytes.begin()+i*48,48,uint8_t(i+1));
    std::vector<bool> used(4);uint32_t first=999;
    Require(FindInstancePartition(bytes,std::span(bytes).first(3*48),48,used,first) && first==0);
    std::fill_n(used.begin(),3,true);
    Require(FindInstancePartition(bytes,std::span(bytes).last(48),48,used,first) && first==3);
    Require(!FindInstancePartition(bytes,std::span(bytes).first(48),48,used,first));
    Require(!FindInstancePartition(bytes,std::span(bytes).last(47),48,used,first));
    Require(!FindInstancePartition(bytes,std::span(bytes).last(48),16,used,first));
    used.assign(4,false);std::fill(bytes.begin(),bytes.end(),1);
    Require(!FindInstancePartition(bytes,std::span(bytes).first(48),48,used,first));
    Require(FindInstancePartition(bytes,bytes,48,used,first) && first==0);
    std::puts("PASS split batches, full byte identity, consumed and ambiguous spans");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
