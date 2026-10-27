#pragma once
#include <cstddef>

// THE RENDER VIEW'S CAMERA DESCRIPTORS. The view producer (sub_1404240DC) builds four of them, one per
// rendering plane, from the view's camera input block, and after them the history set (a 0x190 block
// per plane; its first byte is non-zero once there is a history). A descriptor's own fields --
// position (1/131072 fixed) +0x00, orientation quaternion +0x10, fov +0x20, aspect +0x28, zoom +0x2C,
// NDC shift +0x30/+0x34, near/far +0x40/+0x44, projection +0x190, basis +0x250/+0x260/+0x270, jitter
// +0x370/+0x374, render size +0x378/+0x37C -- are where they were in 2.31: game 2.5 grew the view header
// in front of them and the descriptor at its end (fields from +0x398, read by the camera matrix builder
// sub_140296EA0). 2.31: descriptors at +0x70, stride 0x3A0, history at +0xEF0.
namespace cvr::view_layout {
inline constexpr size_t kCamera = 0xE0;
inline constexpr size_t kCameraStride = 0x3E0;
inline constexpr int kPlanes = 4;
inline constexpr size_t kHistory = kCamera + kPlanes * kCameraStride;   // 0x1060
}  // namespace cvr::view_layout
