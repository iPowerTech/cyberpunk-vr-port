#include "Render/NativeSceneChild.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>

using namespace cvr::stereo::scene_child;
static void Require(bool value){if(!value)throw std::runtime_error("Scene child attribution assertion failed");}
int main()try{
    std::array<detail::ChildKey,11> children;
    for(uint32_t i=0;i<11;++i)children[i]={0x1000+i*64,0x2000+i*64,0x3000+i*64,i};
    uint32_t slot{};
    for(uint32_t i=0;i<11;++i){
        Require(detail::UniqueChild(children,0,1,children[i],slot)==(i<6));
        if(i<6)Require(slot==i);else Require(slot==UINT32_MAX);
        Require(detail::UniqueChild(children,1,2,children[i],slot)==(i>=6));
        if(i>=6)Require(slot==i);else Require(slot==UINT32_MAX);
    }
    Require(!detail::UniqueChild(children,1,1,children[0],slot));
    Require(!detail::UniqueChild(children,0,2,children[6],slot));
    auto bad=children;bad[1]=bad[0];bad[1].slot=1;
    Require(!detail::UniqueChild(bad,0,1,bad[0],slot));
    bad=children;bad[0].slot=9;Require(!detail::UniqueChild(bad,0,1,bad[0],slot));
    auto observed=children[6];++observed.work;Require(!detail::UniqueChild(children,1,2,observed,slot));
    Require(!detail::UniqueChild(std::span(children).first(10),0,1,children[0],slot));
    std::puts("PASS exact source child selection, ambiguity refusal and native section boundaries");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
