#pragma once
#include <cctype>
#include <initializer_list>
#include <string_view>

namespace cvr::vrik {
inline bool EqualBoneName(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)
        if(std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i])))return false;
    return true;
}
inline bool StartsBoneName(std::string_view name,std::string_view prefix) {
    return name.size()>=prefix.size() && EqualBoneName(name.substr(0,prefix.size()),prefix);
}
inline bool IsPrimarySpine(std::string_view name) {
    if(!StartsBoneName(name,"Spine"))return false;
    for(char c:name.substr(5))if(c<'0'||c>'9')return false;
    return true;
}
inline bool IsRigControl(std::string_view name) {
    if(StartsBoneName(name,"Torso_") || StartsBoneName(name,"shadow_"))return true;
    for(auto suffix:{std::string_view("_setup"),std::string_view("_GRP")})
        if(name.size()>=suffix.size() && EqualBoneName(name.substr(name.size()-suffix.size()),suffix))return true;
    return false;
}
inline int ForearmTwistSlot(std::string_view name,bool left) {
    const char* wristL[]={"l_Wrist_0_JNT","l_Wrist_1_JNT","l_Wrist_2_JNT"};
    const char* wristR[]={"r_Wrist_0_JNT","r_Wrist_1_JNT","r_Wrist_2_JNT"};
    const char* twistL[]={"l_forearmTwist01_JNT","l_forearmTwist02_JNT","l_forearmTwist03_JNT"};
    const char* twistR[]={"r_forearmTwist01_JNT","r_forearmTwist02_JNT","r_forearmTwist03_JNT"};
    for(int i=0;i<3;++i)
        if(EqualBoneName(name,left ? wristL[i] : wristR[i]) ||
           EqualBoneName(name,left ? twistL[i] : twistR[i]))return i;
    return -1;
}
}
