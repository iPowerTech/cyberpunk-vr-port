#pragma once

// ================================================================================================
// WeaponAim -- the native hitscan from the muzzle, and the one thing outside it may call.
// ================================================================================================

#include "Anim/WeaponAimState.hpp"

// Declarations are copied from the definitions, never retyped -- see the note in Anim/VrikHook.hpp
// for what guessing a signature costs.
bool InstallWeaponAimHooks();
