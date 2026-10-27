#include "Render/NativeGraphParent.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace cvr::stereo;
namespace {
std::array<uint8_t,4096> memory{};
constexpr uint64_t Start=0x10000,Context=Start,Manager=Start+128,Views=Start+256;
uint64_t calls{},changeAt{};
template<class T>void Put(uint64_t address,T value){std::memcpy(memory.data()+address-Start,&value,sizeof(value));}
bool Read(uint64_t address,void* out,size_t bytes){
    if(++calls==changeAt)Put(Context+0x38,uint8_t(2));
    if(address<Start || bytes>memory.size() || address-Start>memory.size()-bytes)return false;
    std::memcpy(out,memory.data()+address-Start,bytes);return true;
}
void Require(bool yes){if(!yes)throw std::runtime_error("Graph parent assertion failed");}
template<class T,size_t N>void Field(std::array<uint8_t,N>& bytes,size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
graph_snapshot::Snapshot Graph(){
    graph_snapshot::Snapshot s;
    for(uint32_t i=0;i<4;++i){graph_snapshot::Node n;n.order=i;n.handle=100+i*7|0x20000000u;
        Field(n.bytes,0,i);Field(n.bytes,16,uint64_t(0x20000+i*64));Field(n.bytes,24,i/2);
        Field(n.bytes,28,uint32_t(i|((3-i)<<8)));n.vtable=0x30000+i*8;n.work=0x1401ec1d0;
        Field(n.definition,0,n.vtable);Field(n.definition,24,uint64_t(0x40000+i));s.nodes.push_back(n);}
    return s;
}
void Setup(){memory={};calls=changeAt=0;Put(Context+0x18,uint64_t(0xabc));Put(Context+0x20,Manager);
    Put(Context+0x38,uint8_t(3));Put(Manager+0x48,Views);Put(Manager+0x54,uint32_t(2));Put(Views,uint64_t(0xabc));Put(Views+8,uint64_t(0xdef));}
}
int main()try{
    Setup();constexpr graph_parent::Code code{0x300,4,0x4D25767F9DCE13F5ull}; // four zero bytes
    graph_parent::CodeReceipt original;Require(original.Capture(Start,Read,{&code,1}) && original.VerifiedFor(Start));
    const auto certifiedReads=calls;Put(Start+0x300,uint32_t(0xE9));
    Require(original.VerifiedFor(Start) && !original.VerifiedFor(Start+0x10000) && calls==certifiedReads);
    Require(!original.Capture(Start,Read,{&code,1}) && original.VerifiedFor(Start));
    graph_parent::CodeReceipt patched;Require(!patched.Capture(Start,Read,{&code,1}) && patched.Attempted() && !patched.VerifiedFor(Start));
    Put(Start+0x300,uint32_t(0));Require(!patched.Capture(Start,Read,{&code,1}) && !patched.VerifiedFor(Start));
    for(auto badCode:{graph_parent::Code{0x300,0,code.hash},graph_parent::Code{0x300,513,code.hash},graph_parent::Code{UINT32_MAX,4,code.hash}}){
        graph_parent::CodeReceipt bad;Require(!bad.Capture(Start,Read,{&badCode,1}) && !bad.VerifiedFor(Start));
    }
    graph_parent::CodeReceipt missing;Require(!missing.Capture(Start,nullptr,{&code,1}));
    graph_parent::CodeReceipt overflow;Require(!overflow.Capture(UINT64_MAX-1,Read,{&code,1}));
    Setup();graph_parent::Context context;Require(graph_parent::ReadContext(Read,Context,context));
    Require(context.viewIndex==0 && context.slot==3);const auto reads=calls;
    auto source=Graph();graph_parent::Receipt receipt;Require(graph_parent::Resolve(source,context,receipt));
    Require(receipt.handle==source.nodes[0].handle && receipt.ordinal==0 && receipt.name==0x40000);
    context.viewIndex=1;Require(!graph_parent::Resolve(source,context,receipt));context.viewIndex=0;
    auto bad=source;Field(bad.nodes[1].bytes,28,graph_snapshot::Field<uint32_t>(bad.nodes[0].bytes,28));Require(!graph_parent::Resolve(bad,context,receipt));
    bad=source;bad.nodes[0].definition[0]^=1;Require(!graph_parent::Resolve(bad,context,receipt));
    bad=source;bad.failure=1;Require(!graph_parent::Resolve(bad,context,receipt));
    bad=source;bad.nodes[1].handle=bad.nodes[0].handle;Require(!graph_parent::Resolve(bad,context,receipt));
    bad=source;Field(bad.nodes[1].bytes,0,uint32_t(9));Require(!graph_parent::Resolve(bad,context,receipt));
    Setup();Put(Views+8,uint64_t(0xabc));Require(!graph_parent::ReadContext(Read,Context,context));
    Setup();Put(Manager+0x54,uint32_t(65));Require(!graph_parent::ReadContext(Read,Context,context));
    Setup();Put(Context+0x20,uint64_t(UINT64_MAX-4));Require(!graph_parent::ReadContext(Read,Context,context));
    Setup();changeAt=reads/2+1;Require(!graph_parent::ReadContext(Read,Context,context));
    Setup();Put(Context+0x18,uint64_t(0xdef));Put(Context+0x38,uint8_t(1));Require(graph_parent::ReadContext(Read,Context,context));
    Require(graph_parent::Resolve(source,context,receipt) && receipt.ordinal==2);
    std::puts("PASS exact native view/order parent receipt, remapped handles, async copied context, duplicate/stale/ambiguous rejection");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
