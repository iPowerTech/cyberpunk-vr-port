#include "Render/SinglePassTrace.hpp"
#include <array>
#include <thread>
#include <vector>
#include <stdexcept>
#include <cstdio>
using namespace cvr::stereo::trace;
static void Require(bool v){if(!v)throw std::runtime_error("trace assertion failed");}
int main() try {
    std::array<uint8_t,848> data{};data[0]=37;
    Camera(1,0,2,reinterpret_cast<void*>(1));Require(CyberpunkVR_SinglePassTraceCount==0);
    CyberpunkVR_SinglePassTraceRequest=2;FrameBoundary();Require(CyberpunkVR_SinglePassTraceState==1);
    Camera(1,0,2,data.data());data[0]=99;
    List(3,1,reinterpret_cast<void*>(8));List(3,1,reinterpret_cast<void*>(8));
    Require(CyberpunkVR_SinglePassTraceCount==2 && CyberpunkVR_SinglePassTraceRecords[0].camera[0]==37);
    FrameBoundary();List(3,1,reinterpret_cast<void*>(8));Require(CyberpunkVR_SinglePassTraceCount==3);
    FrameBoundary();Require(CyberpunkVR_SinglePassTraceState==2);
    Camera(1,0,2,reinterpret_cast<void*>(1));Require(CyberpunkVR_SinglePassTraceCount==3);
    CyberpunkVR_SinglePassTraceRequest=1;FrameBoundary();Require(CyberpunkVR_SinglePassTraceCount==0);
    CyberpunkVR_SinglePassTraceNodeFilter=7;
    Camera(1,0,2,data.data());List(1,0,reinterpret_cast<void*>(8));Require(CyberpunkVR_SinglePassTraceCount==0);
    CyberpunkVR_SinglePassTraceNodeFilter=0;
    std::vector<std::thread> threads;
    for(unsigned t=0;t<8;++t)threads.emplace_back([t]{std::array<uint8_t,848> bytes{};bytes.fill(uint8_t(t));for(unsigned i=0;i<50;++i)Camera(t,t%2,i,bytes.data());});
    for(auto& t:threads)t.join();Require(CyberpunkVR_SinglePassTraceCount==400);
    for(unsigned i=0;i<400;++i){const auto& row=CyberpunkVR_SinglePassTraceRecords[i];Require(row.sequence==i+1);for(auto b:row.camera)Require(b==row.node);}
    for(unsigned i=0;i<200;++i)Camera(9,1,0,data.data());
    Require(CyberpunkVR_SinglePassTraceCount==Capacity && CyberpunkVR_SinglePassTraceState==3);
    Require(!(CyberpunkVR_SinglePassTraceSeq.load()&1));
    std::puts("PASS disabled, copied data, deduplication, bounded frames, concurrent writers, capacity and publication");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
