#pragma once
// THE SIGHT LINE, for the shot taken with the eye on a sight.
//
// A sight is mounted above the bore -- measured 10.2 cm on an Ajax with a long scope, 8.1 cm on the
// Osprey -- and every sight the port draws looks PARALLEL to the barrel: a scope's crosshair and a
// camera scope's image centre lie on the sight's own axis, a collimator's mark at infinity along the
// bore, the holographic dot on the same line. A bullet leaving the muzzle along the barrel therefore
// lands one sight height below the mark at every range, and a magnifying sight multiplies the miss:
// 10 cm low at any distance, 1.5 deg of the scope picture at 10 m behind a 3x glass.
//
// The flat game has no such gap -- its shots leave the camera, which IS the line of sight. So while
// the eye is on a sight the optics mod publishes the offset from the bore to the sight line (world
// metres, perpendicular to the barrel) and the shot starts on that line instead of at the muzzle,
// still along the barrel. The mark is then exact at every range. Off the sight the muzzle is the
// origin as before. The value carries its publish time and is ignored once it stops being refreshed,
// so a reloaded mod or a holstered weapon cannot leave a shot lifted.
#include <windows.h>
#include <cmath>
#include <cstdint>

extern "C" __declspec(dllexport) float    CyberpunkVR_SightLineOffset[3];   // defined in Natives/OrientationProvider.cpp
extern "C" __declspec(dllexport) uint64_t CyberpunkVR_SightLineTick;
extern "C" __declspec(dllexport) int32_t  CyberpunkVR_HitscanFromSightLine;

namespace cvr::sight {

// The bore-to-sight-line offset for a shot along `forward`, re-orthogonalised against it (the mod's
// value is up to a frame old). False while no sight is being looked through.
inline bool SightLineOffset(const float* forward, float* off) {
    if (!CyberpunkVR_HitscanFromSightLine) return false;
    const uint64_t tick = CyberpunkVR_SightLineTick;
    if (tick == 0 || GetTickCount64() - tick > 250) return false;
    float x = CyberpunkVR_SightLineOffset[0], y = CyberpunkVR_SightLineOffset[1],
          z = CyberpunkVR_SightLineOffset[2];
    const float fl2 = forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2];
    if (!std::isfinite(fl2) || fl2 < 0.25f) return false;
    const float along = (x * forward[0] + y * forward[1] + z * forward[2]) / fl2;
    x -= along * forward[0]; y -= along * forward[1]; z -= along * forward[2];
    const float l2 = x * x + y * y + z * z;
    if (!std::isfinite(l2) || l2 > 0.3f * 0.3f) return false;
    off[0] = x; off[1] = y; off[2] = z;
    return true;
}

}  // namespace cvr::sight
