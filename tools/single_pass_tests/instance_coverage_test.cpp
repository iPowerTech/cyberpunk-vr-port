#include "Render/InstanceCoverage.hpp"
#include "Render/InstanceRebase.hpp"
#include <stdexcept>
#include <cstdio>
#include <string>
#include <array>
using namespace cvr::stereo;
void Check(bool value){if(!value)throw std::runtime_error("instance coverage failed");}
std::vector<uint8_t> Records(std::initializer_list<uint8_t> values){std::vector<uint8_t> result;for(auto v:values)result.insert(result.end(),48,v);return result;}
template<class T>void Put(std::vector<uint8_t>& data,size_t at,T value){std::memcpy(data.data()+at,&value,sizeof(value));}
std::vector<uint8_t> Signature(uint32_t system){
    std::vector<uint8_t> data(36+8+8+32+12);std::memcpy(data.data(),"DXBC",4);Put(data,24,uint32_t(data.size()));Put(data,28,1u);Put(data,32,36u);
    std::memcpy(data.data()+36,"ISG1",4);Put(data,40,uint32_t(data.size()-44));Put(data,44,1u);Put(data,56,40u);Put(data,64,system);
    std::memcpy(data.data()+84,"POSITION",9);return data;
}
int main()try{
    auto a=Records({1,2,3}),b=Records({4,5}),peer=Records({1,2,3,4,5});std::vector<bool> ca(3),cb(2);
    std::array<InstanceSource,2> sources{{{a,&ca},{b,&cb}}};InstanceCoveragePlan plan;
    Check(PlanInstanceCoverage(sources,peer,48,plan));Check(plan.matches.size()==5 && plan.runs.size()==1 && plan.runs[0].count==5 && !plan.unmatched);
    Check(!ca[0] && !cb[0]);Check(CommitInstanceCoverage(sources,plan));Check(ca[2] && cb[1]);
    Check(!CommitInstanceCoverage(sources,plan));
    ca.assign(3,false);cb.assign(2,false);peer=Records({1,9,2,3,4,8,5});
    Check(PlanInstanceCoverage(sources,peer,48,plan));Check(plan.matches.size()==5 && plan.unmatched==2 && plan.runs.size()==3);
    Check(plan.runs[0].first==0 && plan.runs[1].first==2 && plan.runs[1].count==3 && plan.runs[2].first==6);
    a=Records({1,1,2});sources[0].bytes=a;peer=Records({1,1,1,2});
    Check(PlanInstanceCoverage(sources,peer,48,plan));Check(plan.matches.size()==3 && plan.unmatched==1);
    std::vector<uint8_t> combined(682*48,7),largePeer(341*48,7);std::vector<bool> combinedCoverage(682);
    const InstanceSource joined{combined,&combinedCoverage};
    Check(PlanInstanceCoverage({&joined,1},largePeer,48,plan));Check(plan.matches.size()==341);
    Check(CommitInstanceCoverage({&joined,1},plan));
    Check(PlanInstanceCoverage({&joined,1},largePeer,48,plan));Check(plan.matches.size()==341 && plan.matches.front().instance==341);
    auto identical=Records({7,7,8});auto maskedPeer=Records({7,8});std::vector<bool> maskedCoverage(3);
    const uint8_t masks[]{2,3,2};const InstanceSource masked{identical,&maskedCoverage,masks};
    Check(PlanInstanceCoverage({&masked,1},maskedPeer,48,plan));
    Check(plan.matches.size()==1 && plan.matches[0].instance==1 && plan.matches[0].peer==0 && plan.unmatched==1);
    Check(CommitInstanceCoverage({&masked,1},plan) && !maskedCoverage[0] && maskedCoverage[1] && !maskedCoverage[2]);
    InstanceCoveragePlan forged;forged.matches.push_back({0,0,0});Check(!CommitInstanceCoverage({&masked,1},forged));
    Check(PlanInstanceCoverage({&masked,1},maskedPeer,48,plan) && plan.matches.empty() && plan.unmatched==2);
    const uint8_t invalidMasks[]{2,0,3};const InstanceSource invalid{identical,&maskedCoverage,invalidMasks};
    Check(!PlanInstanceCoverage({&invalid,1},maskedPeer,48,plan) && !CommitInstanceCoverage({&invalid,1},forged));
    auto sig=Signature(D3D_NAME_UNDEFINED);Check(HasIndependentInstanceOrdinal({sig.data(),sig.size()}));
    sig=Signature(D3D_NAME_INSTANCE_ID);Check(!HasIndependentInstanceOrdinal({sig.data(),sig.size()}));
    sig=Signature(D3D_NAME_UNDEFINED);Put(sig,56,UINT32_MAX);Check(!HasIndependentInstanceOrdinal({sig.data(),sig.size()}));
    Check(!HasIndependentInstanceOrdinal({nullptr,0}));
    std::puts("PASS split/merged batches, exact multiplicity, holes, transactional coverage and shader InstanceID contract");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
