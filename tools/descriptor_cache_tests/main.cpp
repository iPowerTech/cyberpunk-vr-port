#include "Render/DescriptorCache.hpp"
#include "Camera/ImagePoseLedger.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
using cvr::render::DescriptorCache;
void Check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
void Startup(){
    DescriptorCache<128> cache;const uintptr_t buffers[]{1,2,3};cache.PinResources(buffers);
    for(auto p:buffers)cache.Store(p,{p,2560,2560});
    for(uintptr_t i=4;i<300000;++i)Check(cache.Store(i,{i,512,256}),"churn insert failed");
    for(auto p:buffers)Check(cache.Read(p).has_value(),"never-recreated swapchain descriptor was evicted before first draw");
    Check(cache.Size()==128,"unbounded metadata growth");
}
void Active(){
    DescriptorCache<4> cache;cache.Store(1,{11,2560,2560});
    for(uintptr_t i=2;i<10000;++i){Check(cache.Read(1).has_value(),"live HUD target was evicted");cache.Store(i,{i+20,32,32});}
    Check(cache.Read(1)->resource==11,"active descriptor changed identity");
}
void Replacement(){
    DescriptorCache<4> cache;const uintptr_t buffers[]{11};cache.PinResources(buffers);
    cache.Store(1,{11,2560,1440});cache.Store(1,{99,800,600});auto result=cache.Read(1);
    Check(result && result->resource==99 && result->width==800 && result->height==600,"recycled descriptor retained stale resource or size");
    cache.Store(1,{});Check(!cache.Read(1),"null RTV inherited old pose target");
    Check(cache.Size()==0,"null descriptor retained metadata");
}
void Resize(){
    DescriptorCache<4> cache;const uintptr_t old[]{1,2},next[]{3,4};cache.PinResources(old);
    cache.Store(1,{1,100,100});cache.Store(2,{2,100,100});cache.PinResources(next);
    cache.Store(3,{3,200,200});cache.Store(4,{4,200,200});
    for(uintptr_t i=5;i<1000;++i)cache.Store(i,{i,10,10});
    Check(!cache.Read(1)&&!cache.Read(2),"resize left obsolete buffers permanently pinned");
    Check(cache.Read(3)&&cache.Read(4),"new swapchain buffers were not pinned");
}
void Concurrent(){
    DescriptorCache<16> cache;std::atomic<bool> done=false;std::atomic<unsigned> failures=0,reads=0;
    cache.Store(1,{1,1,~1u});std::vector<std::thread> workers;
    for(int i=0;i<4;++i)workers.emplace_back([&]{while(!done){if(auto value=cache.Read(1)){
        if(value->resource!=value->width || value->height!=~value->width)++failures;++reads;}}});
    for(uintptr_t i=2;i<100000;++i)cache.Store(1,{i,uint32_t(i),~uint32_t(i)});
    done=true;for(auto& worker:workers)worker.join();
    Check(reads>0 && failures==0,"lookup combined fields from different descriptor generations");
}
void AllPinned(){
    DescriptorCache<2> cache;const uintptr_t buffers[]{1,2};cache.PinResources(buffers);
    cache.Store(1,{1,1,1});cache.Store(2,{2,2,2});Check(!cache.Store(3,{3,3,3}),"evicted protected descriptor");
    Check(cache.Read(1)&&cache.Read(2),"failed insert corrupted existing targets");
    cache.Store(1,{});Check(cache.Store(3,{3,3,3}),"erased slot could not be reused");
}
void ImageIdentity(){
    DescriptorCache<32> cache;cvr::camera::ImagePoseLedger<uint64_t> images;
    const uintptr_t buffers[]{500};cache.PinResources(buffers);cache.Store(100,{500,2560,2560});
    for(uint64_t frame=1;frame<10000;++frame){
        for(uintptr_t i=0;i<64;++i)cache.Store(1000+frame*64+i,{100000+frame*64+i,128,128});
        images.Reset(10);auto target=cache.Read(100);
        Check(target.has_value(),"swapchain mapping lost after transient descriptor churn");
        images.Record(10,target->resource,frame,false);images.Submit(20,10,frame);
        Check(images.Read(500).pose==frame,"color retained an old pose while depth/MV advanced");
    }
}
void Reserve(){
    DescriptorCache<4> cache;cache.Store(1,{1,10,10});cache.Reserve(20000);Check(cache.Limit()==20000,"reserve not applied");
    for(uintptr_t i=2;i<=20000;++i)Check(cache.Store(i,{i,10,10}),"insert within reserved slots failed");
    Check(cache.Size()==20000,"evicted below the reserved capacity");
    Check(cache.Read(1)&&cache.Read(20000),"reserved entries lost");
    cache.Reserve(8);Check(cache.Limit()==20000,"reserve lowered the capacity");
}
int main(int argc,char** argv){Check(argc==2,"case required");std::string name=argv[1];
 if(name=="startup_churn")Startup();else if(name=="active")Active();else if(name=="replacement")Replacement();
 else if(name=="resize")Resize();else if(name=="concurrent")Concurrent();else if(name=="all_pinned")AllPinned();
 else if(name=="image_identity")ImageIdentity();else if(name=="reserve")Reserve();else return 2;
 std::cout<<"PASS "<<name<<'\n';}
