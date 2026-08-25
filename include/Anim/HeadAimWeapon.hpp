#pragma once

// Head aim: the weapon follows the final HMD orientation while the vanilla pose keeps its position,
// and the shot leaves the live muzzle exactly as in hand aim. See src/Anim/HeadAimWeapon.cpp.

#include <cstdint>

namespace cvr::anim {

// True while the weapon-aim toggle is OFF and a weapon with a published muzzle is out. Also the
// signal that VRIK must not drive the arms this pass -- a controller-driven arm solve and a
// head-driven weapon are two answers to one question.
bool IsHeadAimWeaponActive();

// Replaces WeaponRight's model ROTATION with the latched view orientation. Position is left to the
// game, so ADS and every authored animation still move the weapon normally.
void ApplyHeadAimWeaponOrientation(uint8_t* boneBuf);

}  // namespace cvr::anim
