#pragma once

// The VR menu (mods/cet/CyberpunkVRPort_Menu): while it is open the triggers, grips and face buttons
// belong to it, not to the game. The sticks stay the game's -- the player can still walk and turn.
namespace cvr::vrmenu {
bool CapturesButtons();
// Rendered index tip, including the last phalanx; false when tracking/rig is unavailable.
bool FingerWorld(int side, float* world);
// Refreshed by the wait transition; expires if its Lua owner stops updating.
float BlinkClosure();
}
