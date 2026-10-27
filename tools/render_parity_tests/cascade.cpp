#include "Stereo/CascadeSampling.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace cvr::stereo;
using Matrices=std::array<float,CascadeMatrixFloatCount>;
static_assert(CascadeMatrixFloatCount==64);
int checks{};
void Check(bool value,const char* why){++checks;if(!value){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
Matrices Make(size_t count,float angle=0,float translation=0){
    Matrices a{};
    for(size_t i=0;i<count;++i){
        float* m=a.data()+16*i;const float s=1.f/(12.f+28.f*float(i));
        m[0]=std::cos(angle)*s;m[4]=std::sin(angle)*s;
        m[1]=-std::sin(angle)*s;m[5]=std::cos(angle)*s;
        m[10]=.001f*(1.f+float(i));m[12]=translation*s;m[13]=-translation*s;m[14]=.2f;m[15]=1;
    }
    return a;
}
int main(){
    Check(!ActiveCascadeMatrices(nullptr),"missing block");
    Matrices empty{};Check(!ActiveCascadeMatrices(empty.data()),"non-shadow/empty block");
    for(size_t count=1;count<=4;++count){
        auto a=Make(count);Check(ActiveCascadeMatrices(a.data())==count,"recognize every supported active prefix");
        for(int step=-180;step<=180;++step){
            const auto b=Make(count,float(step)*.01745329252f,float(step)*10.f);
            float worst=0;Check(SameCascadeSamplingLayout(a.data(),b.data(),&worst),"head/body rotation and translation keep layout eligible");
            Check(worst<.00001f,"rotation does not change cascade extents");
        }
        for(size_t other=1;other<=4;++other){
            auto b=Make(other);
            Check(SameCascadeSamplingLayout(a.data(),b.data())==(count==other),"settings transition cannot reuse the previous cascade count");
        }
        for(size_t changed=0;changed<count;++changed){
            auto b=a;b[16*changed]*=1.03f;
            Check(!SameCascadeSamplingLayout(a.data(),b.data()),"every active cascade participates in extent validation");
            b=a;b[16*changed]*=1.01f;
            Check(SameCascadeSamplingLayout(a.data(),b.data()),"small fitting differences stay valid at every index");
        }
        for(size_t i=0;i<count;++i){
            auto b=a;b[16*i+15]=0;
            Check(!ActiveCascadeMatrices(b.data()),"malformed active affine matrix");
            b=a;b[16*i+3]=.1f;
            Check(!ActiveCascadeMatrices(b.data()),"perspective/non-cascade block rejected");
            b=a;b[16*i]=0;
            Check(!ActiveCascadeMatrices(b.data()),"degenerate cascade basis rejected");
        }
    }
    auto gap=Make(3);std::fill_n(gap.data()+16,16,0.f);
    Check(!ActiveCascadeMatrices(gap.data()),"inactive hole cannot alias another layout");
    for(size_t i=0;i<64;++i){
        auto a=Make(3);a[i]=std::numeric_limits<float>::quiet_NaN();
        Check(!ActiveCascadeMatrices(a.data()),"NaN in active or inactive matrix rejected");
        a=Make(3);a[i]=std::numeric_limits<float>::infinity();
        Check(!SameCascadeSamplingLayout(a.data(),Make(3).data()),"infinite source rejected");
    }
    auto tiny=Make(4);for(size_t i=0;i<4;++i){tiny[16*i]=tiny[16*i+5]=tiny[16*i+10]=1.e-20f;}
    Check(ActiveCascadeMatrices(tiny.data())==4,"finite distant extents do not underflow float length tests");
    std::cout<<"PASS "<<checks<<" cascade count, settings transition and pose checks\n";
}
