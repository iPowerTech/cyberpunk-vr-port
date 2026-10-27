#include "Render/NativePairSubmission.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
namespace pair=cvr::stereo::pair_submission;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
    pair::Source peer{100,9,500,true,true,false},main{200,10,500,true,true,false};
    const std::array<uint64_t,4> batch{50,100,200,300};
    auto result=pair::Check(peer,main,500,batch,true);
    Require(result.order==pair::Order::SeparateCallsInBatch && result.producerIndex==1 && result.consumerIndex==2,"Ordered native split batch rejected");
    Require(pair::Check(peer,main,500,batch,false).order==pair::Order::None,"Unsplit ExecuteCommandLists was treated as a completion boundary");
    for(const auto& bad:{std::array<uint64_t,4>{50,200,100,300},std::array<uint64_t,4>{100,100,200,300},
        std::array<uint64_t,4>{100,200,200,300},std::array<uint64_t,4>{50,60,200,300}})
        Require(pair::Check(peer,main,500,bad,true).order==pair::Order::None,"Wrong or duplicate native submission admitted");
    for(unsigned field=0;field<8;++field){auto p=peer,m=main;
        switch(field){case 0:p.sealed=false;break;case 1:m.sealed=false;break;case 2:p.certified=false;break;case 3:m.certified=false;break;
            case 4:p.queue=501;break;case 5:m.queue=501;break;case 6:p.generation=0;break;default:m.submitted=true;break;}
        Require(pair::Check(p,m,500,batch,true).order==pair::Order::None,"Unvalidated producer/consumer admitted");}
    peer.submitted=true;const std::array<uint64_t,2> later{200,300};
    Require(pair::Check(peer,main,500,later,false).order==pair::Order::EarlierSubmission,"Already submitted producer on same queue rejected");
    Require(pair::Check(peer,main,500,batch,true).order==pair::Order::None,"Producer replay admitted");
    std::puts("PASS exact source queue ordering, split-call requirement, prior submission and replay/foreign scope rejection");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
