#include "Render/MonoCascadePolicy.hpp"
#include <iostream>
#include <stdexcept>
using namespace cvr::cascades;
static void Check(bool v){if(!v)throw std::runtime_error("Cascade policy failed");}
static Policy Ready(unsigned count,unsigned resolution,uintptr_t resource=1) {
    Policy p;p.Begin(count);Atlas a{resource,resolution,resolution,53,count,1,0,0,count};
    Check(p.SourceClear(a,0x3f800000));
    for(unsigned i=0;i<count;++i){p.Camera(i,true);p.Complete(i,true);}p.MainSampling(count,true);Check(p.Ready());return p;
}
int main()try{
    const Allocation committed{0,0,0,true,true},a{1,0,4096,true,false},b{1,4096,4096,true,false},overlap{1,2048,4096,true,false},other{2,0,4096,true,false};
    Check(!committed.CouldAlias({}));Check(!a.CouldAlias(b));Check(a.CouldAlias(overlap));Check(!a.CouldAlias(other));Check(a.CouldAlias({}));
    unsigned cases=0;
    for(unsigned count=1;count<=4;++count)for(unsigned resolution:{1024u,2048u,4096u}) {
        auto p=Ready(count,resolution);auto a=p.atlas;
        Check(p.MainClear(a,p.clearBits));Check(!p.Ready());
        for(unsigned i=0;i<count;++i){a.first=i;a.count=1;Check(p.Reuse(a,i));Check(!p.Reuse(a,4));Check(p.Claim(i));Check(!p.Claim(i));}
        a=p.atlas;Check(!p.MainClear(a,p.clearBits));Check(!p.reusing);
        for(unsigned nextCount=1;nextCount<=4;++nextCount)for(unsigned nextResolution:{1024u,2048u,4096u}) {
            p=Ready(count,resolution);auto changed=p.atlas;changed.width=changed.height=nextResolution;changed.slices=changed.count=nextCount;
            Check(p.MainClear(changed,p.clearBits)==(count==nextCount && resolution==nextResolution));
            p.Begin(nextCount);Check(!p.Ready());Check(!p.reusing);Check(p.SourceClear(changed,0x3f800000));
            for(unsigned i=0;i<nextCount;++i){p.Camera(i,true);p.Complete(i,true);}p.MainSampling(nextCount,true);
            Check(p.MainClear(changed,0x3f800000));++cases;
        }
        p=Ready(count,resolution);a=p.atlas;a.resource=2;Check(p.MainClear(a,p.clearBits));
        p=Ready(count,resolution);p.Invalidate();Check(!p.MainClear(p.atlas,p.clearBits));
        p=Ready(count,resolution);p.MainSampling(count,false);Check(!p.Ready());
        p=Ready(count,resolution);p.camera=0;Check(!p.Ready());
        p=Ready(count,resolution);p.complete=0;Check(!p.Ready());
        p=Ready(count,resolution);Check(!p.MainClear(p.atlas,0));
    }
    Check(!Atlas{1,1024,1024,53,4,1,0,3,2}.Valid());
    std::cout<<"PASS "<<cases<<" cascade count/resolution transitions, current-pair leases, fresh resources, coverage/camera guards and invalidation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
