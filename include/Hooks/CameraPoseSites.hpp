#pragma once
#include <cstdint>
namespace cvr::camera::sites {
// CP2077 2.31, SHA256 a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991.
// Camera setup, descriptor construction, and copy ancestry traced in PID12092.
struct Site { uintptr_t rva;const char* bytes; };
inline constexpr Site Serialize{0x127F58,"48895c2408488974"};
inline constexpr Site GenericSerialize{0x7FFBD0,"48895c24084889742410574883ec40"};
inline constexpr Site RttDescriptorCall{0xAC2CD0,"488d5580f30f1145c8488d8db0070000e82bb27cff"};
inline constexpr Site Blend{0x12752C,"488bc4488958205556574156"};
inline constexpr Site BlendFinalize{0x127A98,"4883ec2883790800"};
inline constexpr Site BlendFinalizeCall{0x127A58,"488d4c2430e836000000"};
inline constexpr Site WorldBlendSourceRead{0xE58827,"488b8fc0000000"};
inline constexpr Site WorldBlendStore{0xE58B26,"f20f1006488d9fe0feffff"};
inline constexpr Site WorldBlendNotifyCall{0xE58B5A,"ff9040020000"};
inline constexpr Site SetupCopy{0x4E9620,"f20f1002f20f1101"};
inline constexpr Site SetupDefault{0x4E96E0,"33c048c7411c0000"};
inline constexpr Site MainRead{0x4E9398,"488bc44889580848"};
inline constexpr Site RttBuild{0xAC2BA4,"48895c2410555657"};
inline constexpr Site RttRefresh{0xAC31C4,"48895c24084889742410574883ec40"};
inline constexpr Site PanzerAimUpdate{0x261D5EC,"48895c240855488bec4881ec80000000"};
inline constexpr Site PanzerAimCall{0x261D42D,"e8ba010000"};
inline constexpr Site PanzerWeaponAim{0x25FA8E4,"488bc4488958084889781055488da828"};
inline constexpr Site VehicleCameraPosition{0x2611A30,"40534883ec404c8bc1488bda8b89a000"};
inline constexpr Site VehicleCameraRotation{0x2611AFC,"40534883ec30488bda488bd18b89a000"};
inline constexpr Site PanzerWeaponPositionCall{0x25FAA04,"e827700100"};
inline constexpr Site PanzerWeaponRotationCall{0x25FAA39,"e8be700100"};
inline constexpr Site VehicleWeaponLaunch{0x25F9A7C,"488bc448895808488970104889781855"};
inline constexpr Site VehicleWeaponTargetedLaunchCall{0x1ECD51C,"e85bc57200"};
inline constexpr Site DescriptorBuild{0x28DF10,"40534883ec508b42"};
inline constexpr Site CameraClone{0x28DB28,"48895c2408574883"};
inline constexpr Site RenderBuild{0x4E4030,"40534883ec208b42"};
inline constexpr Site CameraCopy{0x28D4B8,"40534883ec20f20f"};
inline constexpr Site RenderCall{0x4E50EA,"4c8d83a0200000488d5580488d4de0e832efffff"};
}
