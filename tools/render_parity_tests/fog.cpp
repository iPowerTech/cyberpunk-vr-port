#include "Stereo/FogHistory.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace cvr::stereo;
namespace {
int checks{};
void Check(bool condition,const char* message) {
    ++checks;if(!condition){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}
}
FogCamera Camera(float yaw,float x,float y,float z) {
    FogCamera c{};
    for(unsigned i=0;i<c.size();++i)c[i]=float(i)*.01f;
    std::fill_n(c.data()+32,16,0.0f);std::fill_n(c.data()+80,16,0.0f);
    c[32]=c[37]=.7815386f;c[42]=-1.25e-6f;c[43]=.020000025f;c[46]=1;
    // P20/P21 carry the jitter; camera[51].xy holds it alone
    c[34]=c[204]=.0006f;c[38]=c[205]=-.0004f;
    const auto co=std::cos(yaw),si=std::sin(yaw);
    c[80]=co;c[81]=si;c[83]=-co*x-si*y;
    c[86]=1;c[87]=-z;
    c[88]=si;c[89]=-co;c[91]=-si*x+co*y;c[95]=1;
    return c;
}
std::array<float,4> Project(const FogProjection& m,const std::array<float,4>& p) {
    std::array<float,4> out{};
    for(unsigned r=0;r<4;++r)for(unsigned k=0;k<4;++k)out[r]+=m[r*4+k]*p[k];
    return out;
}
}
int main() {
    FogCamera source=Camera(.7f,-1359,-1798,7.69f);FogProjection sourceProjection{};
    Check(FogCurrentProjection(source,sourceProjection),"MAIN projection extracted");
    auto unjittered=source;unjittered[34]=unjittered[38]=unjittered[204]=unjittered[205]=0;FogProjection expected{};
    Check(FogCurrentProjection(unjittered,expected) && expected==sourceProjection,"source froxels exclude temporal jitter");
    // The asymmetric projection's NDC shift sits in P20/P21 next to the jitter and must survive.
    auto shifted=source;shifted[34]+=.03f;shifted[38]+=.1776f;
    auto shiftedClean=unjittered;shiftedClean[34]=.03f;shiftedClean[38]=.1776f;
    FogProjection withShift{},cleanShift{};
    Check(FogCurrentProjection(shifted,withShift) && FogCurrentProjection(shiftedClean,cleanShift),"shifted projection extracted");
    for(unsigned i=0;i<16;++i)Check(std::abs(withShift[i]-cleanShift[i])<=1e-6f*std::max(1.0f,std::abs(cleanShift[i])),"jitter removed, shift kept");
    Check(withShift!=sourceProjection,"the shift reaches the borrowed projection");
    {
        const std::array<float,4> point{-1355,-1792,8,1};
        const auto a=Project(withShift,point),b=Project(sourceProjection,point);
        Check(std::abs((a[1]/a[3]-b[1]/b[3])-.1776f)<1e-4f && std::abs((a[0]/a[3]-b[0]/b[3])-.03f)<1e-4f,"NDC moves by exactly the shift");
    }
    // The borrow needs one frustum: the same window passes whatever the jitter, a mirrored one does not.
    {
        FogProjection a{},b{},c{};
        auto rejittered=shiftedClean;rejittered[34]+=.0004f;rejittered[204]+=.0004f;
        auto mirrored=shiftedClean;mirrored[34]=-.03f;
        Check(FogFrustum(shiftedClean,a) && FogFrustum(rejittered,b) && FogFrustum(mirrored,c),"frustum extracted");
        Check(SameFogFrustum(a,b),"jitter alone keeps the frustum");
        Check(!SameFogFrustum(a,c),"a mirrored window is another frustum");
    }
    auto wild=source;wild[204]=.5f;FogProjection rejected{};
    Check(!FogCurrentProjection(wild,rejected),"a jitter slot holding anything but a sub-pixel jitter is refused");
    for(int degree=-180;degree<=180;++degree) {
        auto native=Camera(float(degree)*.01745329252f,-1359.065f,-1798,7.69f);
        FogCamera patched{};Check(ApplyFogHistoryProjection(native,sourceProjection,patched),"head turn accepts valid source");
        for(unsigned i=0;i<patched.size();++i)
            Check(patched[i]==(i>=48 && i<64 ? sourceProjection[i-48]:native[i]),"only previous VP changes");
        FogProjection actual{};std::copy_n(patched.begin()+48,16,actual.begin());
        for(const auto& point:std::array<std::array<float,4>,3>{{{-1355,-1792,8,1},{-1360,-1800,10,1},{-1340,-1790,4,1}}})
            Check(Project(actual,point)==Project(sourceProjection,point),"world point stays in MAIN's grid under head rotation");
    }
    FogCamera untouched{},out{};out.fill(123);untouched=out;
    source[32]=std::numeric_limits<float>::quiet_NaN();
    Check(!ApplyFogHistoryProjection(source,sourceProjection,out) && out==untouched,"invalid camera leaves output unchanged");
    auto invalid=sourceProjection;invalid[3]=std::numeric_limits<float>::infinity();
    Check(!ApplyFogHistoryProjection(unjittered,invalid,out) && out==untouched,"invalid source leaves output unchanged");
    std::array<float,396> shared{};
    shared[43*4+1]=96;shared[43*4+2]=96;shared[43*4+3]=64;
    shared[44*4]=2;shared[44*4+1]=.5f;shared[44*4+2]=250;shared[44*4+3]=.004f;
    FogGrid grid{};Check(ReadFogGrid(shared.data(),grid),"native fog layout");
    RenderOwner owner{1,2,3,4,5,6,2560,2560};
    FogSample sample{owner,{10,11,12,13},20,30,1000000,500,40};
    FogAllocation scattering{10,21,22,23},integrated{10,31,32,33};
    auto borrow=[&](FogAllocation live,FogAllocation raw,FogAllocation final,FogGrid current,uint32_t frame=501){
        return CanBorrowFog(sample,owner,20,frame,31,1001000,live,raw,final,grid,current);
    };
    Check(borrow(sample.allocation,scattering,integrated,grid),"MAIN previous-frame source with matching layout");
    Check(!borrow(sample.allocation,sample.allocation,integrated,grid),"reject scattering UAV alias");
    Check(!borrow(sample.allocation,scattering,sample.allocation,grid),"reject integration UAV alias");
    Check(!borrow({10,99,12,13},scattering,integrated,grid),"reject recycled source");
    Check(!borrow(sample.allocation,scattering,integrated,grid,502),"reject missed frame");
    for(unsigned i=0;i<grid.size();++i) {
        auto changed=grid;changed[i]+=1;
        Check(!borrow(sample.allocation,scattering,integrated,changed),"reject changed depth/slicing layout");
        changed=grid;changed[i]=std::numeric_limits<float>::quiet_NaN();
        Check(!borrow(sample.allocation,scattering,integrated,changed),"reject nonfinite grid");
    }
    shared[43*4+1]=0;Check(!ReadFogGrid(shared.data(),grid),"reject empty fog dimensions");
    std::cout<<"PASS "<<checks<<" fog reprojection and resource pairing checks\n";
}
