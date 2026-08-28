#pragma once

// The one lever that decides what the game is looking at. See src/Hooks/LookAtRayFromHand.cpp for
// what it patches and why that site and no other.

// The switch lives in LiveControls (xrHandInteract, xrHandInteractAxis) and is drawn in the
// overlay's Controls tab -- there is no separate mode global to keep in step. Which HAND points is
// not a setting at all any more: it is measured, below.

// Diagnostics, printed by the camera panel. `patched` counts rays actually rewritten; `skipped`
// counts calls that reached the hook and declined -- the two apart are the difference between "the
// hook never ran" and "the hook ran and refused", which look identical from inside a headset.
extern volatile int g_VRLookAtPatched;
extern volatile int g_VRLookAtSkipped;

// WHICH HAND POINTS, decided in ONE place. The ray, the collision test that judges the hand to be
// on the object, and the grip that becomes X must all name the same hand -- and the way that broke
// was two of them working it out separately: this side from the port's weapon flag, the CET side
// from GetActiveWeapon(). Those two disagree, and when they do the ray leaves one hand while the
// reach is measured against the other. Published to shared slot kPointingHand so Lua reads the
// answer instead of recomputing it.
// -1 = no hand is free to point (driving, or both hands on the weapon), 0 = left, 1 = right.
// Read it anywhere; it is only DECIDED in VRUpdatePointingHand below.
int  VRPointingHand();
bool VRPointingHandIsRight();

// Called once per frame from the input poll, and nowhere else: the choice carries hysteresis, which
// is a memory, and a memory computed in two places is two answers.
void VRUpdatePointingHand();

bool InstallLookAtRayFromHandHook();
