#include "Render/OwnedNativeGraph.hpp"
#include <cstdio>
#include <fstream>
#include <stdexcept>
using namespace cvr::stereo;
static void Require(bool yes){if(!yes)throw std::runtime_error("Owned native graph assertion failed");}
template<class T,size_t N>static void Put(std::array<uint8_t,N>& b,size_t at,T value){std::memcpy(b.data()+at,&value,sizeof(value));}
template<class T>static std::vector<T> Read(const char* file){
    std::ifstream stream(file,std::ios::binary|std::ios::ate);Require(bool(stream));const auto size=stream.tellg();
    Require(size>=0 && size%sizeof(T)==0 && size<=16*1024*1024);std::vector<T> out(size_t(size)/sizeof(T));
    stream.seekg(0);stream.read(reinterpret_cast<char*>(out.data()),size);Require(bool(stream));return out;
}
static std::array<OwnedNativeGraph::Edges,2> Edges(const graph_snapshot::Snapshot& s){
    std::array<OwnedNativeGraph::Edges,2> result;
    for(const auto& e:s.edges)result[graph_snapshot::Field<uint32_t>(e.bytes,4)].emplace(
        graph_snapshot::Field<uint32_t>(e.bytes,8)&0xDFFFFFFFu,graph_snapshot::Field<uint32_t>(e.bytes,12)&0xDFFFFFFFu);
    return result;
}
int main(int argc,char** argv)try{
    Require(argc==1 || argc==3);graph_snapshot::Snapshot source;
    for(uint32_t i=0;i<8;++i){graph_snapshot::Node n;n.handle=(100+i*9)|0x20000000u;n.order=i;
        Put(n.bytes,0,i);Put(n.bytes,8,uint32_t(4));Put(n.bytes,16,uint64_t(0x100000+i*64));Put(n.bytes,24,i&1);
        Put(n.bytes,28,uint32_t(0xa5b60000));n.vtable=0x140000000+i*16;n.work=0x1401ec1d0;
        Put(n.definition,0,n.vtable);source.nodes.push_back(n);}
    std::array<OwnedNativeGraph::Edges,2> edges;
    for(uint32_t i=1;i<8;++i)edges[0].emplace(100+(i-1)*9,100+i*9);
    edges[1]={{109,100},{100,127},{118,127},{127,136},{118,163}};
    OwnedNativeGraph graph;Require(!graph.SortableData());Require(graph.Create(source,edges));Require(!graph.Create(source,edges));
    Require(!graph.ValidateCompiled() && graph.CompileReference() && graph.ValidateCompiled());
    const auto snapshot=graph.Snapshot();Require(snapshot.nodes.size()==8 && snapshot.edges.size()==12 && !snapshot.failure);
    Require(Edges(snapshot)==edges && snapshot.nodes[7].bytes[28]==7);
    for(size_t i=0;i<8;++i){Require(snapshot.nodes[i].bytes[30]==0xb6 && snapshot.nodes[i].bytes[31]==0xa5);
        Require(snapshot.nodes[i].definition==source.nodes[i].definition && snapshot.nodes[i].work==source.nodes[i].work);}
    auto* header=static_cast<uint8_t*>(graph.SortableData());auto* nodes=reinterpret_cast<uint8_t*>(graph_snapshot::Field<uint64_t>(graph.Header(),24));
    nodes[100*48+28]^=1;Require(!graph.ValidateCompiled());Require(graph.CompileReference());
    nodes[100*48+16]^=1;Require(!graph.ValidateCompiled());nodes[100*48+16]^=1;
    header[0x68]=0;Require(!graph.ValidateCompiled());Require(graph.CompileReference());
    auto* order=reinterpret_cast<uint32_t*>(graph_snapshot::Field<uint64_t>(graph.Header(),40));
    const auto savedOrder=order[0];order[0]=0x2000ffff;Require(!graph.CompileReference() && !graph.ValidateCompiled());order[0]=savedOrder;Require(graph.CompileReference());
    OwnedNativeGraph emptyEdges;Require(emptyEdges.Create(source,{}) && emptyEdges.CompileReference());
    auto* emptyOrder=reinterpret_cast<uint32_t*>(graph_snapshot::Field<uint64_t>(emptyEdges.Header(),40));
    auto* emptyNodes=reinterpret_cast<uint8_t*>(graph_snapshot::Field<uint64_t>(emptyEdges.Header(),24));
    const auto emptyFirst=emptyOrder[0];
    // No edges can make ReferenceRanks reject the missing original ID first.
    // An unused but allocated sparse slot makes the pre-fix write observable without UB.
    emptyNodes[99*48+28]=0xa5;emptyNodes[99*48+29]=0x5a;emptyOrder[0]=99|0x20000000u;
    Require(!emptyEdges.CompileReference() && !emptyEdges.ValidateCompiled());
    Require(emptyNodes[99*48+28]==0xa5 && emptyNodes[99*48+29]==0x5a);
    emptyOrder[0]=0x2000ffff;Require(!emptyEdges.CompileReference() && !emptyEdges.ValidateCompiled());
    emptyOrder[0]=emptyFirst;Require(emptyEdges.CompileReference() && emptyEdges.ValidateCompiled());
    auto invalid=source;invalid.nodes[1].handle=invalid.nodes[0].handle;OwnedNativeGraph duplicate;Require(!duplicate.Create(invalid,edges));
    auto cyclic=edges;cyclic[0].emplace(163,100);OwnedNativeGraph cycle;Require(!cycle.Create(source,cyclic));
    cyclic=edges;cyclic[1].emplace(100,0xffff);OwnedNativeGraph foreign;Require(!foreign.Create(source,cyclic));
    OwnedNativeGraph original;Require(original.Create(snapshot,edges) && original.CompileReference() && original.SameSourceOrders());
    if(argc==3){graph_snapshot::Snapshot saved;saved.nodes=Read<graph_snapshot::Node>(argv[1]);saved.edges=Read<graph_snapshot::Edge>(argv[2]);
        const auto original=saved;OwnedNativeGraph owned;Require(owned.Create(saved,Edges(saved)) && owned.CompileReference() && owned.SameSourceOrders());
        Require(saved.nodes==original.nodes && saved.edges==original.edges);
        std::printf("PASS saved native graph: %zu nodes, %zu edges, all original phase ranks preserved, definition bytes untouched\n",saved.nodes.size(),saved.edges.size());}
    std::puts("PASS owned native graph: complete dual adjacency, native counting-rank reference, sparse tagged handles, immutable source and corruption rejection");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
