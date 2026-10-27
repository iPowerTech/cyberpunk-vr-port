#include "Render/ConstantWriteTimeline.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
using Timeline=cvr::stereo::constants::ConstantWriteTimeline;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() try {
    Timeline t;using Status=Timeline::Status;
    auto write=[&](uint64_t list,uint64_t payload,uint64_t at=4096,uint64_t size=112,bool committed=true){
        return t.Write(t.Mark(list),at,size,committed,payload);
    };
    auto submit=[&](uint64_t list,uint64_t batch,uint64_t queue=1){std::array<uint64_t,1> ids{list};t.Submit(queue,batch,ids);};
    write(1,11);const auto early=t.Mark(1);write(1,22);const auto late=t.Mark(1);submit(1,1);
    Require(t.Resolve(early,4096,112).payload==11 && t.Resolve(late,4096,112).payload==22,"Same-list read positions lost");
    t.Clear();write(2,22);write(1,11);const auto consumer=t.Mark(3);submit(1,1);submit(2,2);submit(3,3);
    Require(t.Resolve(consumer,4096,112).payload==22,"CPU order replaced GPU submission order");
    t.Clear();write(1,11);const auto before=t.Mark(2);write(3,33);submit(1,1);submit(2,2);
    Require(t.Resolve(before,4096,112).status==Status::Pending,"Unsubmitted competing writer was guessed");
    submit(3,3);Require(t.Resolve(before,4096,112).payload==11,"Later GPU write changed earlier read");
    t.Clear();write(1,11);write(2,0,4100,4);auto read=t.Mark(3);submit(1,1);submit(2,2);submit(3,3);
    Require(t.Resolve(read,4096,112).status==Status::UnknownWrite,"Unknown partial overwrite bypassed");
    t.Clear();write(1,11);write(2,0);write(3,33);read=t.Mark(4);submit(1,1);submit(2,2);submit(3,3);submit(4,4);
    Require(t.Resolve(read,4096,112).payload==33,"Complete later overwrite failed to restore a known range");
    t.Clear();write(1,11);write(2,22);t.Reset(2);read=t.Mark(3);submit(1,1);submit(3,2);
    Require(t.Resolve(read,4096,112).payload==11,"Abandoned recording changed GPU contents");
    t.Reset(1);write(1,44);submit(1,3);Require(t.Resolve(read,4096,112).payload==11,"Recycled pointer replaced historical generation");
    t.Clear();write(1,11);read=t.Mark(2);submit(1,1,2);submit(2,2,1);
    Require(t.Resolve(read,4096,112).status==Status::CrossQueue,"Unproven queue synchronization accepted");
    t.Clear();t.Fence(100,0);write(1,11);read=t.Mark(2);submit(1,1,2);t.Signal(2,100,7);t.Wait(1,100,7);submit(2,2,1);
    Require(t.Resolve(read,4096,112).payload==11,"GPU signal/wait dependency was lost");
    t.InvalidateFence(100);Require(t.Resolve(read,4096,112).status==Status::CrossQueue,"CPU-signaled fence used as GPU dependency");
    t.Clear();t.Fence(100,0);write(1,11);read=t.Mark(2);t.Wait(1,100,7);submit(2,1,1);submit(1,2,2);
    Require(t.Resolve(read,4096,112).status==Status::Pending,"Future signal was assumed complete");
    t.Signal(2,100,7);Require(t.Resolve(read,4096,112).payload==11,"Later CPU signal was not linked");
    t.Signal(2,100,7);Require(t.Resolve(read,4096,112).status==Status::CrossQueue,"Repeated/rewound fence value accepted");
    t.Clear();t.Fence(100,0);write(1,11);read=t.Mark(2);t.Wait(1,100,7);submit(2,1,1);submit(1,2,2);t.Signal(2,100,8);
    Require(t.Resolve(read,4096,112).status==Status::Pending,"A higher signal fabricated an edge to an unobserved earlier signal");
    t.FenceProgress(100,8);Require(t.Resolve(read,4096,112).status==Status::CrossQueue,"Fence completion invented producer identity");
    t.Clear();t.Fence(100,7);write(1,11);read=t.Mark(2);submit(1,1,2);t.Signal(2,100,8);t.Wait(1,100,7);submit(2,2,1);
    Require(t.Resolve(read,4096,112).status==Status::CrossQueue,"Already-completed wait proved newer work");
    t.Clear();t.Fence(100,0);t.Fence(200,0);write(1,11);write(2,22);read=t.Mark(3);
    submit(1,1,2);t.Signal(2,100,1);t.Wait(3,100,1);submit(2,2,3);t.Signal(3,200,1);t.Wait(1,200,1);submit(3,3,1);
    Require(t.Resolve(read,4096,112).payload==22,"Transitive queue order did not choose the last writer");
    t.Clear();t.Fence(100,0);write(1,11);read=t.Mark(2);submit(1,1,2);t.Signal(2,100,1);submit(2,2,1);
    Require(t.Resolve(read,4096,112).status==Status::CrossQueue,"Unobserved completion linked unrelated queues");
    t.FenceProgress(100,1);
    Require(t.Resolve(read,4096,112).status==Status::CrossQueue,"Late completion observation changed an already submitted consumer");
    const auto cpuOrdered=t.Mark(3);submit(3,3,1);
    Require(t.Resolve(cpuOrdered,4096,112).payload==11,"CPU-observed GPU completion was not imported by the next submission");
    t.Clear();t.Fence(100,0);t.Fence(200,0);write(1,11);write(2,22);write(3,33);read=t.Mark(3);
    submit(1,1,1);t.Signal(1,100,1);submit(2,2,2);t.Signal(2,200,1);
    t.Wait(3,100,1);t.Wait(3,200,1);submit(3,3,3);
    Require(t.Resolve(read,4096,112).payload==33,"A later complete ordered write did not supersede earlier unordered writes");
    t.Clear();write(1,11);write(2,22);read=t.Mark(3);std::array<uint64_t,3> batch{2,1,3};t.Submit(1,1,batch);
    Require(t.Resolve(read,4096,112).payload==11,"Array order within one submission lost");
    submit(1,2);Require(t.Resolve(read,4096,112).status==Status::Replayed,"Replayed list accepted without a new position");
    t.Clear();t.Fence(100,0);write(1,11);write(2,22,8192);const auto infixReader=t.Mark(3);
    submit(1,1,1);t.Signal(1,100,1);t.Wait(2,100,1);submit(3,2,2);submit(2,3,1);
    Require(t.Resolve(infixReader,4096,112).payload==11,"Prefix fence lost its completed prefix");
    Require(t.Resolve(infixReader,8192,112).status==Status::CrossQueue,"Prefix fence falsely certified an unexecuted suffix");
    for(bool committed:{false,true}) {
        t.Clear();write(1,11,4096,112,committed);t.Alias(t.Mark(1));read=t.Mark(1);submit(1,1);
        Require(t.Resolve(read,4096,112).status==(committed?Status::Ready:Status::UnknownWrite),"Alias treatment ignored placement");
    }
    for(const auto area:{Timeline::MemorySpan{2,0,65536},Timeline::MemorySpan{1,65536,65536},
                         Timeline::MemorySpan{1,32768,65536},Timeline::MemorySpan{}}) {
        t.Clear();const auto id=write(1,11,4096,112,false);t.Memory(id,{1,0,65536});t.Alias(t.Mark(1),area);read=t.Mark(1);submit(1,1);
        const bool overlap=Timeline::CouldAlias({1,0,65536},area);
        Require(t.Resolve(read,4096,112).status==(overlap?Status::UnknownWrite:Status::Ready),"Alias scope ignored heap identity/range");
    }
    Require(Timeline::CouldAlias({1,UINT64_MAX-10,65536},{1,0,65536}),"Overflowing allocation treated as disjoint");
    t.Clear();write(1,11);read=t.Mark(1);t.Reset(1);
    Require(t.Resolve(read,4096,112).status==Status::Abandoned,"Abandoned consumer accepted");
    t.Clear();for(uint32_t i=0;i<=Timeline::Capacity;++i)write(1,i+1);read=t.Mark(1);submit(1,1);
    Require(t.Resolve(read,4096,112).status==Status::Overflow,"Overflow silently dropped a write");
    std::puts("PASS command positions, GPU order, unknown writes, pending producers, Reset generations, replay and aliases");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
