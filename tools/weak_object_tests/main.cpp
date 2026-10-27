#include "Utils/WeakObjectSlot.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <iostream>
#include <string>
struct Object {std::atomic<unsigned>* destroyed;~Object(){++*destroyed;}};
struct Strong {
    std::shared_ptr<Object> pointer;
    explicit operator bool()const{return bool(pointer);}
    Object* GetPtr()const{return pointer.get();}
};
struct Weak {std::weak_ptr<Object> pointer;Strong Lock()const{return {pointer.lock()};}};
using Slot=cvr::WeakObjectSlot<Weak>;
void Check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
uintptr_t Address(const std::shared_ptr<Object>& p){return reinterpret_cast<uintptr_t>(p.get());}
void Expired(){
    std::atomic<unsigned> destroyed=0;Slot slot;auto owner=std::make_shared<Object>(&destroyed);const auto id=Address(owner);
    slot.Bind(id,1,Weak{owner});owner.reset();
    Check(destroyed==1,"weak cache retained a dead component");
    Check(!slot.Lock(id),"cached address resurrected a freed VRCAM");
}
void Scope(){
    std::atomic<unsigned> destroyed=0;Slot slot;auto owner=std::make_shared<Object>(&destroyed);const auto id=Address(owner);
    slot.Bind(id,1,Weak{owner});
    {auto live=slot.Lock(id);Check(bool(live),"could not pin live target");owner.reset();
     Check(destroyed==0 && live.GetPtr()->destroyed==&destroyed,"component freed during notification");}
    Check(destroyed==1 && !slot.Lock(id),"pin leaked beyond call scope");
}
void Replacement(){
    std::atomic<unsigned> destroyed=0;Slot slot;auto a=std::make_shared<Object>(&destroyed),b=std::make_shared<Object>(&destroyed);
    const auto old=Address(a),now=Address(b);slot.Bind(old,1,Weak{a});slot.Bind(now,2,Weak{b});
    Check(!slot.Lock(old)&&bool(slot.Lock(now)),"old selection survived replacement");
    slot.Clear();Check(!slot.Lock(now),"cleared target remained callable");
}
void Mismatched(){
    std::atomic<unsigned> destroyed=0;Slot slot;auto owner=std::make_shared<Object>(&destroyed);
    slot.Bind(1234,1,Weak{owner});Check(!slot.Lock(1234),"metadata identity overrode actual handle identity");
    slot.Bind(Address(owner),2,Weak{owner});Check(!slot.Lock(0),"zero selection accepted");
}
void Concurrent(){
    std::atomic<unsigned> destroyed=0,pinned=0;Slot slot;
    for(unsigned i=0;i<2000;++i){
        auto owner=std::make_shared<Object>(&destroyed);const auto id=Address(owner);
        slot.Bind(id,i+1,Weak{owner});std::atomic<bool> go=false;
        std::thread reader([&]{while(!go.load(std::memory_order_acquire))std::this_thread::yield();
            if(auto live=slot.Lock(id)){Check(live.GetPtr()->destroyed==&destroyed,"invalid live handle");++pinned;}});
        go.store(true,std::memory_order_release);owner.reset();reader.join();
        Check(!slot.Lock(id),"destroy race left callable raw pointer");
    }
    Check(destroyed==2000,"old objects retained by weak cache");
}
int main(int argc,char** argv){Check(argc==2,"case required");std::string name=argv[1];
 if(name=="expired")Expired();else if(name=="scope")Scope();else if(name=="replacement")Replacement();
 else if(name=="mismatched")Mismatched();else if(name=="concurrent")Concurrent();else return 2;
 std::cout<<"PASS "<<name<<'\n';}
