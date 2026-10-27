#pragma once
#include <openxr/openxr.h>
#include <cstdint>

namespace cvr::input {
// Profile names from Valve's Steam Frame custom-engine documentation. This
// extension adds no ABI types, so it also works with the pinned OpenXR loader.
inline constexpr char SteamFrameExtension[]="XR_VALVE_frame_controller_interaction";
inline constexpr char SteamFrameProfile[]="/interaction_profiles/valve/frame_controller_valve";

// Adapted from Crazymoniker/cyberpunk-vr-port-frame (87c14877). Frame's right X/Y
// and left D-pad cannot share Touch's per-hand primary/secondary actions.
struct SteamFrameActions {
    XrAction x{},y{},up{},down{},left{},right{},view{},bumper{};

    template<class MakeAction> void Create(MakeAction&& make) {
        make(x,XR_ACTION_TYPE_BOOLEAN_INPUT,"frame_x","Frame X Button",false);
        make(y,XR_ACTION_TYPE_BOOLEAN_INPUT,"frame_y","Frame Y Button",false);
        make(up,XR_ACTION_TYPE_BOOLEAN_INPUT,"frame_dpad_up","Frame D-pad Up",false);
        make(down,XR_ACTION_TYPE_BOOLEAN_INPUT,"frame_dpad_down","Frame D-pad Down",false);
        make(left,XR_ACTION_TYPE_BOOLEAN_INPUT,"frame_dpad_left","Frame D-pad Left",false);
        make(right,XR_ACTION_TYPE_BOOLEAN_INPUT,"frame_dpad_right","Frame D-pad Right",false);
        make(view,XR_ACTION_TYPE_BOOLEAN_INPUT,"frame_view","Frame View Button",false);
        make(bumper,XR_ACTION_TYPE_BOOLEAN_INPUT,"frame_bumper","Frame Bumper",true);
    }

    template<class ReadBool> uint16_t GlobalButtons(ReadBool&& read) const {
        uint16_t buttons{};
        const auto add=[&](XrAction action,uint16_t bit){if(action!=XR_NULL_HANDLE && read(action))buttons|=bit;};
        add(x,0x4000);add(y,0x8000);
        add(up,0x0001);add(down,0x0002);add(left,0x0004);add(right,0x0008);
        add(view,0x0020);
        return buttons;
    }
};
}
