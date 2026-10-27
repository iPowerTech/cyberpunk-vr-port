#pragma once

// THE WRIST ROTATIONS THE VR HANDS ARE BUILT ON -- right pitch, yaw, roll, then left pitch, yaw, roll,
// in degrees (hand-local XYZ, the convention of SetVRHandOffset). Baked from the user's own tuned
// calibration on 2026-10-17 (the rolls were -21 and -15 on top of the old 0 / -90 / 0 and -180 / -90 / 0).
//
// vrik_calibration.ini (version 4) and the overlay's wrist sliders hold OFFSETS from these, so the
// tuned hands read 0 everywhere. m_calib[8..13] stays absolute: the solver reads it unchanged, and a
// version 3 file (absolute angles) still loads as it was written.
namespace cvr::calib {
inline constexpr float kWristBase[6] = { 0.0f, -90.0f, -21.0f, -180.0f, -90.0f, -15.0f };
}
