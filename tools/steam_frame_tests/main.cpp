#include "Runtimes/SteamFrameInput.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main()try {
    using namespace cvr::input;
    Check(std::string(SteamFrameExtension)=="XR_VALVE_frame_controller_interaction","wrong optional extension");
    Check(std::string(SteamFrameProfile)=="/interaction_profiles/valve/frame_controller_valve","outdated profile path");
    SteamFrameActions actions;
    unsigned reads=0;
    Check(actions.GlobalButtons([&](XrAction){++reads;return true;})==0 && reads==0,"unsupported runtime queried uncreated actions");
    std::vector<std::string> names;
    actions.Create([&](XrAction& action,XrActionType type,const char* name,const char*,bool perHand){
        Check(type==XR_ACTION_TYPE_BOOLEAN_INPUT,"extra button is not Boolean");
        names.emplace_back(name);
        Check(perHand==(names.back()=="frame_bumper"),"wrong per-hand/global action scope");
        action=reinterpret_cast<XrAction>(static_cast<uintptr_t>(names.size()));
    });
    Check(names==std::vector<std::string>{"frame_x","frame_y","frame_dpad_up","frame_dpad_down","frame_dpad_left","frame_dpad_right","frame_view","frame_bumper"},"missing or duplicate extra actions");
    const std::array<XrAction,7> global={actions.x,actions.y,actions.up,actions.down,actions.left,actions.right,actions.view};
    constexpr std::array<uint16_t,7> bits={0x4000,0x8000,0x0001,0x0002,0x0004,0x0008,0x0020};
    for(unsigned mask=0;mask<128;++mask) {
        uint16_t expected=0;
        for(unsigned i=0;i<bits.size();++i)if(mask&(1u<<i))expected|=bits[i];
        reads=0;
        const auto actual=actions.GlobalButtons([&](XrAction action){
            ++reads;Check(action!=actions.bumper,"per-hand bumper queried as a global action");
            for(unsigned i=0;i<global.size();++i)if(action==global[i])return (mask&(1u<<i))!=0;
            throw std::runtime_error("unknown action queried");
        });
        Check(actual==expected && reads==7,"physical buttons alias another XInput control");
    }
    Check(actions.GlobalButtons([](XrAction){return false;})==0,"inactive profile emits buttons");
    actions={};reads=0;
    Check(actions.bumper==XR_NULL_HANDLE && actions.GlobalButtons([&](XrAction){++reads;return true;})==0 && reads==0,"shutdown retained stale action handles");
    std::cout<<"PASS optional-profile fallback, extra action scopes, 128 button combinations, inactive profile and shutdown\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
