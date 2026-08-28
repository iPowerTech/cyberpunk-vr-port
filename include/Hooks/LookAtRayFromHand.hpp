#pragma once

// The one lever that decides what the game is looking at. See src/Hooks/LookAtRayFromHand.cpp for
// what it patches and why that site and no other.

// The switch lives in LiveControls (xrHandInteract, xrHandInteractLeftHanded) and is drawn as a
// checkbox in the overlay's Controls tab -- there is no separate mode global to keep in step.

// Diagnostics, printed by the camera panel. `patched` counts rays actually rewritten; `skipped`
// counts calls that reached the hook and declined -- the two apart are the difference between "the
// hook never ran" and "the hook ran and refused", which look identical from inside a headset.
extern volatile int g_VRLookAtPatched;
extern volatile int g_VRLookAtSkipped;

bool InstallLookAtRayFromHandHook();
