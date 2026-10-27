#include "Render/StereoInstanceData.hpp"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace cvr::stereo;
static void Require(bool v){if(!v)throw std::runtime_error("instance batch validation failed");}
int main() try {
    native_probe::DrawRecord a{};a.instances=3;a.firstInstance=2;a.vertexMask=1u<<7;
    a.instanceResource=1;a.instanceGpuBase=0x1000;a.instanceBytesTotal=1024;a.instanceHeapType=D3D12_HEAP_TYPE_UPLOAD;
    a.vertices[7]={0x1100,240,48};uint64_t at{};size_t bytes{};
    Require(instance_data::Range(a,at,bytes) && at==352 && bytes==144);
    auto b=a;b.firstInstance=7;b.vertices[7]={0x1200,480,48};
    Require(instance_data::Range(b,at,bytes) && at==848 && bytes==144);
    std::vector<uint8_t> x(144,1),y=x;
    Require(instance_data::Equal(a,x,b,y));y.back()=2;Require(!instance_data::Equal(a,x,b,y));y=x;
    Require(!instance_data::Equal(a,std::span(x).first(64),b,std::span(y).first(64)));
    Require(!instance_data::Equal(a,{},b,{}));b.instances=2;Require(!instance_data::Equal(a,x,b,y));
    b=a;b.vertices[7].SizeInBytes=239;Require(!instance_data::Range(b,at,bytes));
    b=a;b.instanceBytesTotal=495;Require(!instance_data::Range(b,at,bytes));
    b=a;b.firstInstance=UINT_MAX;Require(!instance_data::Range(b,at,bytes));
    b=a;b.instances=UINT_MAX;Require(!instance_data::Range(b,at,bytes));
    b=a;b.vertices[7].BufferLocation=0xFFF;Require(!instance_data::Range(b,at,bytes));
    b=a;b.instanceHeapType=D3D12_HEAP_TYPE_DEFAULT;Require(!instance_data::Range(b,at,bytes));
    b=a;b.vertices[7].StrideInBytes=32;Require(!instance_data::Range(b,at,bytes));
    b=a;b.instanceResource=0;Require(!instance_data::Range(b,at,bytes));
    auto observed=a;observed.instanceHeapType=D3D12_HEAP_TYPE_DEFAULT;observed.instances=1;
    auto reader=[](uint64_t gpu,uint64_t resource,uint64_t offset,void* output,size_t bytes) {
        Require(gpu==0x1000 && resource==1 && offset==352);std::memset(output,0x6b,bytes);return true;
    };
    Require(instance_data::CopyObservedDefault(observed,nullptr,reader));
    Require(observed.instanceDataBytes==48 && observed.instanceData[47]==0x6b && observed.instanceData[48]==0);
    observed.instances=3;Require(!instance_data::CopyObservedDefault(observed,nullptr,reader));Require(observed.instanceDataBytes==0);
    std::vector<uint8_t> batch;Require(instance_data::CopyObservedDefault(observed,&batch,reader) && batch.size()==144 && batch.back()==0x6b);
    auto missing=[](uint64_t,uint64_t,uint64_t,void* output,size_t bytes){std::memset(output,1,bytes);return false;};
    Require(!instance_data::CopyObservedDefault(observed,&batch,missing)&&batch.empty());
    observed.instances=1;Require(!instance_data::CopyObservedDefault(observed,nullptr,missing));
    Require(observed.instanceDataBytes==0 && observed.instanceData[0]==0);
    std::puts("PASS full instance arrays, changed last instance, truncated/unreadable data, range limits and different upload offsets");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
