#pragma once
#include <algorithm>
#include <cmath>
namespace driving_test {
inline double Duration(int profile,double frequency) {return profile==1 ? 4/frequency:profile==2 ? 5.0:profile==3 ? 4.0:4.0;}
inline double Angle(int profile,double t,double amplitude,double frequency) {
    if(t<0 || t>Duration(profile,frequency))return 0;
    if(profile==1)return amplitude*std::sin(6.283185307179586*frequency*t);
    if(profile==2) {
        // Deliberate stops, immediate reversal, then return to neutral.
        const double times[]{0,.4,1.0,1.8,2.4,2.8,3.2,3.6,4.0,4.4,5.0};
        const double values[]{0,1,1,-1,-1,0,0,1,-1,0,0};
        for(int i=1;i<11;++i)if(t<=times[i])return amplitude*(values[i-1]+(values[i]-values[i-1])*(t-times[i-1])/(times[i]-times[i-1]));
    }
    if(profile==3) {
        const double times[]{0,.8,1.1,1.9,2.1,2.9,3.2,4.0};
        const double values[]{0,140,140,0,0,-140,-140,0};
        for(int i=1;i<8;++i)if(t<=times[i])return values[i-1]+(values[i]-values[i-1])*(t-times[i-1])/(times[i]-times[i-1]);
    }
    if(profile==4)return .2*std::sin(t*6.283185307179586*9)+.1*std::sin(t*6.283185307179586*17);
    return 0;
}
}
