#pragma once

// ================================================================================================
// WeaponAim -- the aiming detours, and the three things outside them may call.
//
// Of the 16 functions the old header defined, three are reached from outside. The rest were
// reachable only because a header cannot keep a secret.
// ================================================================================================

#include "Anim/WeaponAimState.hpp"

// Declarations are copied from the definitions, never retyped -- see the note in Anim/VrikHook.hpp
// for what guessing a signature costs.
bool InstallWeaponAimHooks();
void Wa_StartTrace(uintptr_t addr, int gated, int writeOnly);
void Wa_StopTrace();
