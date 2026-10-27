#include "Render/NativeDescriptorArena.hpp"
#include <cstdio>
#include <stdexcept>
namespace arena=cvr::stereo::native_descriptors;
namespace flush=cvr::stereo::camera_upload::flush_state;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
    arena::detail::Ring ring{0x1000,0x5000,128,250,160,3};uint64_t absolute{};
    Require(arena::detail::ValidRing(ring,0x5000,256),"Valid descriptor ring rejected");
    auto afterRing=ring;afterRing.head=262;
    Require(arena::detail::RingResult(ring,afterRing,0,6,absolute) && absolute==256,"Wrapped native reservation mapping differs");
    afterRing.head=258;
    Require(arena::detail::RingResult(ring,afterRing,122,6,absolute) && absolute==250,"Reservation at ring end rejected");
    for(UINT field=0;field<8;++field){auto bad=afterRing;
        switch(field){case 0:bad.outstanding=0;break;case 1:++bad.heap;break;case 2:++bad.capacity;break;
            case 3:bad.head=249;break;case 4:bad.head=400;break;case 5:bad.tail=251;break;case 6:bad.tail=159;break;default:bad.address+=64;break;}
        Require(!arena::detail::RingResult(ring,bad,122,6,absolute),"Unowned/ambiguous native ring reservation accepted");}
    Require(!arena::detail::RingResult(ring,afterRing,128,1,absolute),"Native exhaustion sentinel accepted as a descriptor index");
    Require(!arena::detail::RingResult(ring,afterRing,127,2,absolute),"Native descriptor table straddled a ring wrap");
    Require(!arena::detail::RingResult(ring,afterRing,122,9,absolute),"Reservation exceeds observed producer head");
    auto removed=ring;removed.head=UINT64_MAX;Require(!arena::detail::ValidRing(removed,0x5000,256),"Invalid ring occupancy admitted");
    flush::Snapshot source;source.context={0x100,0x200,0x300};source.heap=0x400;source.base=32;source.capacity=64;source.cursor=8;source.stride=32;
    source.graphicsMask=3;source.layout=0x500;source.cpuTable=0x600;
    source.tables.push_back({2,5,4,2,0,-1,{0x11100,0x11120}});
    for(UINT count=1;count<=56;++count){arena::Range range;
        Require(arena::detail::Plan(source,count,96,32,0x10000,0x20000,range),"Valid native descriptor reservation rejected");
        Require(range.first==40 && range.before==8 && range.after==8+count && range.count==count && range.cpu.ptr==0x10500 && range.gpu.ptr==0x20500,
            "Native descriptor window arithmetic changed");
        auto after=source;after.cursor+=count;Require(arena::detail::Advanced(source,after,range),"Exact monotonic cursor increment rejected");
        for(UINT field=0;field<11;++field){auto bad=after;
            switch(field){case 0:++bad.base;break;case 1:++bad.capacity;break;case 2:++bad.cursor;break;case 3:++bad.heap;break;
                case 4:++bad.context.list;break;case 5:++bad.dirty;break;case 6:++bad.rebind;break;case 7:++bad.layout;break;
                case 8:++bad.tables[0].cached;break;case 9:++bad.tables[0].descriptors[0];break;default:++bad.cachedGpu;break;}
            Require(!arena::detail::Advanced(source,bad,range),"Foreign heap/dirty/root/cache state accepted as a reservation");}
        auto forged=range;++forged.first;Require(!arena::detail::Advanced(source,after,forged),"Different reservation base admitted");
    }
    for(UINT count:{0u,57u,8193u,UINT_MAX}){arena::Range range;
        Require(!arena::detail::Plan(source,count,96,32,0x10000,0x20000,range) && !range.count && !range.cpu.ptr,"Exhausted native arena was allowed to wrap");}
    for(UINT field=0;field<9;++field){auto bad=source;arena::Range range;
        switch(field){case 0:bad.heap=0;break;case 1:bad.context={};break;case 2:bad.stride=64;break;case 3:bad.dirty=1;break;
            case 4:bad.rebind=1;break;case 5:bad.capacity=0;break;case 6:bad.cursor=65;break;case 7:bad.base=33;break;default:bad.base=UINT_MAX;break;}
        Require(!arena::detail::Plan(bad,1,96,32,0x10000,0x20000,range),"Unsafe native descriptor state admitted");}
    arena::Range range;Require(!arena::detail::Plan(source,1,96,32,UINT64_MAX-32,0x20000,range),"CPU descriptor overflow admitted");
    Require(!arena::detail::Plan(source,1,96,32,0x10000,UINT64_MAX-32,range),"GPU descriptor overflow admitted");
    std::puts("PASS native descriptor window bounds, no rollover/rewind and exact preservation of all other native state");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
