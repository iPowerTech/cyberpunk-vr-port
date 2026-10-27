#pragma once
#include <cstdint>
namespace cvr::camera::sites {
// CP2077 2.5, SHA256 d36e74c47592e9c656ae250078cb40b2be63c782af5bfbeff2280d43e653fab4 (call sites re-found by hand;
// 2.31 values in git history).
// Camera setup, descriptor construction, and copy ancestry traced in PID12092.
// 2.5 grew the render camera descriptor (0x3A0 -> 0x3E0: +398/399/39C, two sub-views at +3D0/+3D8) and its input
// (0x60 -> 0x98: the handle at +50 moved to +88, new +50/+54/+60/+70); DescriptorBuild, CameraClone, RenderBuild
// and CameraCopy were recompiled around that, same arguments and same leading fields (rotation +0, position +10).
struct Site { uintptr_t rva;const char* bytes; };
inline constexpr Site Serialize{0x5FC318,"48895c2408488974"};
inline constexpr Site GenericSerialize{0x7E8530,"48895c24084889742410574883ec40"};
inline constexpr Site RttDescriptorCall{0xD29C71,"488d9590000000f30f1185d8000000488d8d40080000e860994eff"};
inline constexpr Site Blend{0x5FB8F0,"488bc4488958205556574156"};
inline constexpr Site BlendFinalize{0x5FBE64,"4883ec2883790800"};
inline constexpr Site BlendFinalizeCall{0x5FBE21,"488d4c2430e839000000"};
inline constexpr Site WorldBlendSourceRead{0xEBE4C3,"488b8fc0000000"};
inline constexpr Site WorldBlendStore{0xEBE7C2,"f20f1006488d9fe0feffff"};
inline constexpr Site WorldBlendNotifyCall{0xEBE7F6,"ff9040020000"};
inline constexpr Site SetupCopy{0x2A5C80,"f20f1002f20f1101"};
inline constexpr Site SetupDefault{0x2A5D40,"33c048c7411c0000"};
inline constexpr Site MainRead{0x2A59B8,"488bc44889580848"};
inline constexpr Site RttBuild{0xD29AA8,"48895c2410555657"};
inline constexpr Site RttRefresh{0x490300,"48895c24084889742410574883ec40"};
inline constexpr Site PanzerAimUpdate{0x2BB99F8,"48895c240855488bec4881ec80000000"};
inline constexpr Site PanzerAimCall{0x2BB9839,"e8ba010000"};
inline constexpr Site PanzerWeaponAim{0x2B97BA8,"488bc4488958084889781055488da828"};
inline constexpr Site VehicleCameraPosition{0x2BAE290,"40534883ec404c8bc1488bda8b89a000"};
inline constexpr Site VehicleCameraRotation{0x2BAE35C,"40534883ec30488bda488bd18b89a000"};
inline constexpr Site PanzerWeaponPositionCall{0x2B97CC8,"e8c3650100"};
inline constexpr Site PanzerWeaponRotationCall{0x2B97CFD,"e85a660100"};
inline constexpr Site VehicleWeaponLaunch{0x2B96D44,"488bc448895808488970104889781855"};
inline constexpr Site VehicleWeaponTargetedLaunchCall{0x24CC50C,"e833a86c00"};
inline constexpr Site DescriptorBuild{0x2135EC,"488bc44889580848"};
inline constexpr Site CameraClone{0x2130D0,"48895c2410488974"};
inline constexpr Site RenderBuild{0x42579C,"48895c2408488974"};
inline constexpr Site CameraCopy{0x21297C,"48895c2410574883"};
inline constexpr Site RenderCall{0x42493F,"4c8d8680020000488d55f0488d8d90000000e8460e0000"};
}
