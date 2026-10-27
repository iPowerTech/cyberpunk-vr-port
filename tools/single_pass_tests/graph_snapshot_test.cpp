#include "Render/NativeGraphSnapshot.hpp"
#include <windows.h>
#include <cstdio>
#include <stdexcept>
using namespace cvr::stereo::graph_snapshot;
static void Require(bool value){if(!value)throw std::runtime_error("Native graph snapshot invariant failed");}
static bool ReadMemory(uint64_t address,void* output,size_t bytes){SIZE_T copied{};return address &&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,bytes,&copied) && copied==bytes;}
template<class T,size_t N>void Put(std::array<uint8_t,N>& data,size_t offset,T value){std::memcpy(data.data()+offset,&value,sizeof(value));}
int main()try{
    std::array<uint8_t,0x70> graph{};std::array<std::array<uint8_t,48>,4> nodes{};std::array<uint8_t,24> edge{};
    uint32_t nodeOrder[]{2,0},edgeOrder[]{0};uint64_t vtable[]{0,0x1234};const auto object=reinterpret_cast<uintptr_t>(vtable);
    Put(graph,0x10,uint32_t(2));Put(graph,0x18,reinterpret_cast<uintptr_t>(nodes.data()));Put(graph,0x28,reinterpret_cast<uintptr_t>(nodeOrder));
    Put(graph,0x40,uint32_t(1));Put(graph,0x48,reinterpret_cast<uintptr_t>(edge.data()));Put(graph,0x58,reinterpret_cast<uintptr_t>(edgeOrder));
    Put(nodes[2],16,reinterpret_cast<uintptr_t>(&object));Put(nodes[2],24,uint32_t(1));Put(edge,8,uint32_t(0x20000002));
    Snapshot snapshot;Require(snapshot.Capture(reinterpret_cast<uintptr_t>(graph.data()),ReadMemory));
    Require(snapshot.nodes.size()==2 && snapshot.nodes[0].handle==2 && snapshot.nodes[0].work==0x1234 && snapshot.edges.size()==1);
    nodes[2].fill(0);Require(Field<uint32_t>(snapshot.nodes[0].bytes,24)==1);
    nodeOrder[1]=2;Require(!snapshot.Capture(reinterpret_cast<uintptr_t>(graph.data()),ReadMemory) && snapshot.failure==3);
    nodeOrder[1]=0;Put(graph,0x10,uint32_t(9000));Require(!snapshot.Capture(reinterpret_cast<uintptr_t>(graph.data()),ReadMemory) && snapshot.failure==2);
    std::puts("PASS owned graph nodes/edges, sparse indices, copied data and bounded invalid-input rejection");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
