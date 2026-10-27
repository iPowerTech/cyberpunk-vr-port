#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cvr::ladder {
inline constexpr const char* HandNames[]={"left","right"};
inline constexpr const char* FingerNames[]={"thumb","index","middle","ring","pinky"};
inline constexpr const char* AdjustmentNames[]={"base","middle","tip","spread","roll","cup"};
enum FingerAdjustment { BaseBend,MiddleBend,TipBend,Spread,Roll,PalmBend,AdjustmentCount };
inline bool HasAdjustment(int finger,int channel) { return finger!=0 || channel!=TipBend; }
inline float AdjustmentLimit(int finger,int channel) {
    if(channel==Spread)return 40;
    if(channel==PalmBend)return finger==0 ? 60.0f:35.0f;
    return channel==MiddleBend ? 75.0f:60.0f;
}
inline float ClampAdjustment(int finger,int channel,float degrees) {
    const float limit=AdjustmentLimit(finger,channel);
    return std::clamp(std::isfinite(degrees) ? degrees:0.0f,-limit,limit);
}
inline bool ParseFingerAdjustment(const char* line,int& side,int& finger,int& channel,float& degrees) {
    char hand[6]{},name[8]{},axis[8]{};float value{};
    if(std::sscanf(line,"xr_ladder_rung_%5[a-z]_%7[a-z]_%7[a-z] = %f",hand,name,axis,&value)!=4)return false;
    side=finger=channel=-1;
    for(int i=0;i<2;++i)if(!std::strcmp(hand,HandNames[i]))side=i;
    for(int i=0;i<5;++i)if(!std::strcmp(name,FingerNames[i]))finger=i;
    for(int i=0;i<AdjustmentCount;++i)if(!std::strcmp(axis,AdjustmentNames[i]))channel=i;
    if(side<0 || finger<0 || channel<0 || !HasAdjustment(finger,channel))return false;
    degrees=ClampAdjustment(finger,channel,value);return true;
}
// Measured rig axes: flexion is -Z, spread is Y, finger roll is X.
// Thumb opposition/spread/roll belong to its metacarpal; other digits spread
// and roll at the first phalanx. Angles are local offsets in degrees.
inline void FingerAdjustmentAngles(const char* suffix,int group,const float* settings,float* xyz) {
    xyz[0]=xyz[1]=xyz[2]=0;if(!settings)return;
    const bool palm=std::strncmp(suffix,"InHand",6)==0;
    const int joint=palm ? 0:suffix[std::strlen(suffix)-1]-'0';
    if(palm)xyz[2]=-ClampAdjustment(group,PalmBend,settings[PalmBend]);
    else if(joint>=1 && joint<=3)xyz[2]=-ClampAdjustment(group,joint-1,settings[joint-1]);
    if((group==0 && palm) || (group!=0 && joint==1)) {
        xyz[0]=ClampAdjustment(group,Roll,settings[Roll]);
        xyz[1]=ClampAdjustment(group,Spread,settings[Spread]);
    }
}
}
