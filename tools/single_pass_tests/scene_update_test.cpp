#include "Render/NativeSceneUpdate.hpp"
#include <cstdio>
#include <map>
#include <stdexcept>

using namespace cvr::stereo;
using namespace cvr::stereo::scene_update;
namespace {
constexpr uint64_t Manager=0x10000,Payload=0x20000,Begin=0x30000,End=0x30100,Later=0x30200,Chain=0x40000,Table=0x50000;
std::map<uint64_t,uint8_t> memory;std::map<uint64_t,int32_t> refs,pending;uint32_t reads{},retains{},releases{};
void Require(bool value){if(!value)throw std::runtime_error("Scene update receipt assertion failed");}
template<class T>void Put(uint64_t address,const T& value){const auto* bytes=reinterpret_cast<const uint8_t*>(&value);for(size_t i=0;i<sizeof(T);++i)memory[address+i]=bytes[i];}
void Erase(uint64_t address,size_t bytes){for(size_t i=0;i<bytes;++i)memory.erase(address+i);}
bool ReadMemory(uint64_t address,void* out,size_t bytes){++reads;if(address>UINT64_MAX-bytes)return false;
    for(size_t i=0;i<bytes;++i){const auto found=memory.find(address+i);if(found==memory.end())return false;static_cast<uint8_t*>(out)[i]=found->second;}return true;}
void Retain(void* value){const auto p=reinterpret_cast<uintptr_t>(value);Require(refs.contains(p));++refs[p];++retains;Put(p+0x1C,refs[p]);}
void Release(void** value){const auto p=reinterpret_cast<uintptr_t>(*value);Require(refs.at(p)>1);--refs[p];++releases;Put(p+0x1C,refs[p]);*value=nullptr;}
bool Done(void* value){return !pending.at(reinterpret_cast<uintptr_t>(value));}
const JobLeaseApi Api{Retain,Release,Done};
void Group(uint64_t address,int32_t work=0){refs[address]=1;pending[address]=work;Put(address+0x18,work);Put(address+0x1C,int32_t(1));}
void Pending(uint64_t address,int32_t work){pending[address]=work;Put(address+0x18,work);}
void Prefix(uint64_t payload,uint64_t chain=Chain){Put(payload,Manager);Put(payload+8,chain);}
void Fresh(){memory.clear();refs.clear();pending.clear();reads=retains=releases=0;Group(Begin);Group(End);Group(Later);Prefix(Payload);}
QueueBinding Binding(Kind kind=Kind::General,uint64_t payload=Payload,uint64_t begin=Begin,uint64_t end=End){return {kind,0,0xC1A168,0xCA0E50,payload,begin,end};}
Receipt Create(){return Receipt({1,Manager,0x293B33,77,0},Api);}
void Page(uint64_t page,uint32_t capacity,uint64_t next=0){
    for(uint32_t i=0;i<capacity+16;++i)memory[page+i]=0;
    Put(page+14,int16_t(16));Put(page+16,Table);Put(page+30,int16_t(16));Put(page+32,Table+16);
    Put(page+capacity,uint32_t(46));Put(page+capacity+8,next);
    Put(Table,uint64_t(0x61000));Put(Table+8,uint64_t(0x62000));Put(Table+16,uint64_t(0x63000));Put(Table+24,uint64_t(0x64000));
}
void Membership(){
    std::array<uint8_t,56> bytes{};Put(Payload,bytes);Put(Payload,Manager);
    Put(Payload+8,uint64_t(0x70000));Put(Payload+16,uint32_t(2));Put(Payload+20,uint32_t(1));
    Put(Payload+24,uint64_t(0x71000));Put(Payload+32,uint32_t(2));Put(Payload+36,uint32_t(32));Put(0x70000,uint32_t(0));
    for(uint32_t i=0;i<2;++i){std::array<uint8_t,32> node{};Put(0x71000+i*32,node);
        Put(0x71000+i*32,i?UINT32_MAX:uint32_t(1));Put(0x71008+i*32,uint64_t(0x90000+i*0x100000));
        Put(0x71010+i*32,uint64_t(0x72000+i*0x100));Put(0x71018+i*32,uint32_t(2));Put(0x7101C+i*32,uint32_t(2));
        for(uint32_t j=0;j<2;++j){std::array<uint8_t,16> update{};update[12]=uint8_t(j+1+i*2);Put(0x72000+i*0x100+j*16,update);}}
}
void CheckRefs(){Require(retains==releases);for(const auto& [_,count]:refs)Require(count==1);}
}
int main()try{
    Fresh();
    {auto receipt=Create();uint32_t first{},second{};
        Require(receipt.Register(Binding(),ReadMemory,first) && first==0 && refs[Begin]==2 && refs[End]==2 && !receipt.GroupsClosed());
        Pending(End,1);Require(receipt.Enter(first,Kind::General,Payload,End,ReadMemory));
        // The callback may finish before producer return; this is still not closure.
        receipt.Returned(first);Pending(End,0);Require(!receipt.GroupsClosed());
        Prefix(Payload+0x100);Pending(End,3);
        Require(receipt.Register(Binding(Kind::Pre,Payload+0x100,End,Later),ReadMemory,second) && second==1);
        Pending(Later,1);Require(receipt.Enter(second,Kind::Pre,Payload+0x100,Later,ReadMemory));receipt.Returned(second);Pending(Later,0);
        receipt.CloseProducer();Require(!receipt.GroupsClosed());Pending(End,0);Require(receipt.GroupsClosed() && receipt.Rejection()==Failure::None);
        Require(receipt.Queues().size()==2 && receipt.Queues()[0].binding.completion==End && receipt.Queues()[1].binding.completion==Later);}
    CheckRefs();
    Fresh();
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(Kind::General,Payload,0,End),ReadMemory,index));
        Require(refs[End]==2 && refs[Begin]==1);Pending(End,1);Require(receipt.Enter(index,Kind::General,Payload,End,ReadMemory));
        Erase(Payload,16);const auto before=reads;receipt.Returned(index);receipt.CloseProducer();Pending(End,0);
        Require(receipt.GroupsClosed() && reads==before && receipt.Queues()[0].binding.prerequisite==0);}
    CheckRefs();
    Fresh();
    {std::array<uint8_t,56> empty{};Put(Payload,empty);Put(Payload,Manager);
        auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(Kind::Membership),ReadMemory,index) && receipt.Scenes().empty());
        Require(receipt.Enter(index,Kind::Membership,Payload,End,ReadMemory));receipt.Returned(index);receipt.CloseProducer();Require(receipt.GroupsClosed());}
    CheckRefs();
    Fresh();Membership();
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(Kind::Membership),ReadMemory,index));
        Require(receipt.Scenes().size()==2 && receipt.Scenes()[0].scene==0x90000 && receipt.Scenes()[1].scene==0x190000);
        Require(receipt.Scenes()[0].pool==0x90000+0x7E0F0 && receipt.Scenes()[0].operations[1]==1 && receipt.Scenes()[1].operations[4]==1);
        Pending(End,1);Require(receipt.Enter(index,Kind::Membership,Payload,End,ReadMemory));
        receipt.Mutation(index,0x90000+0x7E0F0,0xA0000,0x1DA293,77);Require(receipt.Mutations().size()==1);
        Erase(Payload,56);Erase(0x71000,64);const auto before=reads;receipt.Returned(index);receipt.CloseProducer();Pending(End,0);
        Require(receipt.GroupsClosed() && reads==before && receipt.Rejection()==Failure::None);}
    CheckRefs();
    Fresh();Membership();Put(0x71000,uint32_t(0));
    {auto receipt=Create();uint32_t index{};Require(!receipt.Register(Binding(Kind::Membership),ReadMemory,index) && receipt.Rejection()==Failure::Payload && receipt.Queues().empty() && receipt.Scenes().empty());}
    CheckRefs();
    Fresh();Membership();
    {Receipt receipt({1,Manager,0x293B33,77,0},{nullptr,Release,Done});uint32_t index{};
        Require(!receipt.Register(Binding(Kind::Membership),ReadMemory,index) && receipt.Scenes().empty() && receipt.Queues().empty());}
    CheckRefs();
    for(const auto kind:{Kind::General,Kind::Pre,Kind::Post}){
        Fresh();Page(Chain,kind==Kind::General?0x4000:0x800);
        {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(kind),ReadMemory,index));Pending(End,1);
            Require(receipt.Enter(index,kind,Payload,End,ReadMemory) && receipt.Inventory(index,Manager,Chain,kind==Kind::Pre?1:0,ReadMemory));
            Require(receipt.Commands().size()==2 && receipt.Commands()[0].command==Chain+16 && receipt.Commands()[0].execute==0x62000 && receipt.Commands()[1].destroy==0x63000);
            Require(receipt.Commands()[0].offset==14 && receipt.Commands()[0].bytes==16 && receipt.Commands()[1].offset==30 && receipt.Queues()[0].inventory);
            Require(receipt.Rejection()==Failure::None);receipt.Returned(index);receipt.CloseProducer();Pending(End,0);Require(receipt.GroupsClosed());}
        CheckRefs();
    }
    Fresh();Page(Chain,0x800);Put(Chain+14,int16_t(-1));
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(Kind::Pre),ReadMemory,index));Require(receipt.Enter(index,Kind::Pre,Payload,End,ReadMemory));
        Require(!receipt.Inventory(index,Manager,Chain,1,ReadMemory) && receipt.Commands().empty() && receipt.Rejection()==Failure::Inventory);}
    CheckRefs();
    Fresh();Page(Chain,0x800,Chain);
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(Kind::Post),ReadMemory,index));Require(receipt.Enter(index,Kind::Post,Payload,End,ReadMemory));
        Require(!receipt.Inventory(index,Manager,Chain,0,ReadMemory) && receipt.Commands().empty());}
    CheckRefs();
    Fresh();
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(),ReadMemory,index));Put(Payload,uint64_t(Manager+8));
        Require(!receipt.Enter(index,Kind::General,Payload,End,ReadMemory) && receipt.Rejection()==Failure::Changed && !receipt.Queues()[0].entered);}
    CheckRefs();
    Fresh();
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(),ReadMemory,index));
        Require(!receipt.Enter(index,Kind::General,Payload,Later,ReadMemory) && receipt.Rejection()==Failure::Binding);}
    CheckRefs();
    Fresh();
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(),ReadMemory,index));Require(!receipt.Register(Binding(),ReadMemory,index) && receipt.Queues().size()==1);}
    CheckRefs();
    Fresh();
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(),ReadMemory,index));Require(receipt.Enter(index,Kind::General,Payload,End,ReadMemory));
        receipt.CloseProducer();Require(receipt.Register(Binding(Kind::Opaque,Payload+0x800,0,Later),ReadMemory,index) && receipt.OpaqueQueues()==1);
        receipt.Returned(0);Pending(Later,1);Require(!receipt.GroupsClosed());Pending(Later,0);Require(receipt.GroupsClosed());
        Require(!receipt.Enter(index,Kind::Opaque,Payload+0x800,Later,ReadMemory));}
    CheckRefs();
    Fresh();
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(),ReadMemory,index));Pending(End,1);
        Require(receipt.Enter(index,Kind::General,Payload,End,ReadMemory));receipt.CloseProducer();receipt.CancelCallback(index);
        Require(receipt.Rejection()==Failure::Scope && receipt.Queues()[index].cancelled && !receipt.Queues()[index].returned);
        Require(!receipt.GroupsClosed() && refs[Begin]==2 && refs[End]==2);
        receipt.Mutation(index,0xA0000,0xB0000,0x1DA293,77);Require(receipt.Mutations().empty());
        receipt.Returned(index);Require(!receipt.Queues()[index].returned);
        Pending(End,0);Require(receipt.GroupsClosed());}
    CheckRefs();
    Fresh();
    {auto receipt=Create();uint32_t index{};Require(receipt.Register(Binding(),ReadMemory,index));Pending(End,1);
        receipt.CancelProducer();Require(receipt.ProducerCancelled() && !receipt.ProducerClosed() && !receipt.GroupsClosed());
        Prefix(Payload+0x100);Require(!receipt.Register(Binding(Kind::General,Payload+0x100),ReadMemory,index));
        Require(refs[End]==2);Pending(End,0);Require(receipt.GroupsClosed() && !receipt.Queues()[0].returned);}
    CheckRefs();
    Fresh();
    {auto receipt=Create();uint32_t index{};
        for(uint32_t i=0;i<MaxQueues;++i){Prefix(Payload+uint64_t(i)*0x100);Require(receipt.Register(Binding(Kind::General,Payload+uint64_t(i)*0x100),ReadMemory,index) && index==i);}
        Prefix(Payload+uint64_t(MaxQueues)*0x100);Require(!receipt.Register(Binding(Kind::General,Payload+uint64_t(MaxQueues)*0x100),ReadMemory,index) && receipt.Rejection()==Failure::Limit);}
    CheckRefs();
    std::puts("PASS scene-update immutable payloads, pre-publication/inline/rotated group closure, all scene keys, bounded virtual input inventory, opaque descendants and balanced native leases");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
