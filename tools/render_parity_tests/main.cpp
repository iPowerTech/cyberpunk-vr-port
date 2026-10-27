#include "Stereo/RenderParity.hpp"
#include <cstdlib>
#include <iostream>
#include <unordered_set>
using namespace cvr::stereo;
int checks{};
void Check(bool value,const char* why){++checks;if(!value){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
int main() {
    Check(IsMainAaContext(100,0,100),"MAIN is identified by its camera context");
    Check(!IsMainAaContext(200,0,100),"other key-zero view is not MAIN");
    Check(!IsMainAaContext(100,1,100),"named view cannot masquerade as MAIN");
    Check(!IsMainAaContext(0,0,0),"missing MAIN context stays unclaimed");
    const uint64_t view=0x77AD6D6871650500ull;
    std::unordered_set<uint64_t> hashes;
    for(uint64_t i=0;i<4096;++i) {
        const auto salted=VrcamGraphHash(i,view);
        Check(salted!=i,"VRCAM does not reuse MAIN's key");
        Check(hashes.insert(salted).second,"distinct graph keys stay distinct in tested range");
        Check(salted!=VrcamGraphHash(i,view+1),"resolution/name change partitions graph cache");
        const auto native=i*0x100010001ull,desired=~native;
        const auto merged=PreserveViewBit33(desired,native);
        Check((merged&~(1ull<<33))==(desired&~(1ull<<33)),"keep other forced feature bits");
        Check((merged&(1ull<<33))==(native&(1ull<<33)),"preserve native view's bit 33");
    }
    RenderOwner owner{1,2,3,4,5,view,2560,2560};
    AaSample aa{owner,500,4,true};uint32_t mode{};
    Check(aa.Read(owner,500,mode)&&mode==4,"MAIN-first AA mode");
    Check(aa.Read(owner,501,mode)&&mode==4,"VRCAM-first AA uses preceding observed setting");
    Check(!aa.Read(owner,502,mode),"AA publication stops");
    Check(!aa.Read(owner,499,mode),"AA frame reset");
    auto changed=owner;changed.eyeCamera++;
    Check(!aa.Read(changed,501,mode),"AA rebind invalidation");
    aa.frame=0xffffffff;
    Check(aa.Read(owner,0,mode),"AA native frame wrap");
    FogSample fog{owner,{10,11,12,13},20,30,1000000,500,40};
    Check(fog.Eligible(owner,20,500,30,1000010),"same-pair fog input");
    Check(fog.Eligible(owner,20,501,30,1000010),"VRCAM-first reads preceding MAIN history");
    Check(fog.Eligible(owner,20,501,31,1000010),"head motion does not invalidate preceding history");
    Check(!fog.Eligible(owner,20,502,30,1000010),"never carry history across a skipped frame");
    Check(!fog.Eligible(owner,20,499,30,1000010),"never reuse future frame");
    Check(!fog.Eligible(owner,21,500,30,1000010),"registry replacement");
    Check(!fog.Eligible(owner,20,500,31,1000010),"head pose pair mismatch");
    Check(!fog.Eligible(owner,20,500,30,1100001),"long stall expires handle");
    Check(!fog.Eligible(owner,20,500,30,999999),"clock reset expires handle");
    for(int field=0;field<8;++field) {
        changed=owner;
        switch(field) {
        case 0:++changed.renderer;break;case 1:++changed.player;break;
        case 2:++changed.mainCamera;break;case 3:++changed.eyeCamera;break;
        case 4:++changed.origin;break;case 5:++changed.eyeName;break;
        case 6:++changed.width;break;case 7:++changed.height;break;
        }
        Check(!fog.Eligible(changed,20,500,30,1000010),"load/rebind/recenter/resize invalidation");
    }
    fog.handle=0;Check(!fog.Eligible(owner,20,500,30,1000010),"missing GPU handle");
    fog.handle=0xffffffff;Check(!fog.Eligible(owner,20,500,30,1000010),"invalid GPU handle sentinel");
    fog.handle=40;fog.allocation={};Check(!fog.Eligible(owner,20,500,30,1000010),"missing allocation");
    const FogAllocation allocation{10,11,12,13};
    Check(allocation!=FogAllocation{10,21,12,13},"recycled D3D resource cannot use the cached handle");
    Check(allocation!=FogAllocation{10,11,22,13},"recycled engine wrapper rejected");
    Check(allocation!=FogAllocation{10,11,12,23},"recycled SRV rejected");
    std::cout<<"PASS "<<checks<<" cache partition, AA flags and fog lifetime checks\n";
}
