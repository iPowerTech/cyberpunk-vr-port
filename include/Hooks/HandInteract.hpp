#pragma once
#include <cstdint>

// HAND INTERACTION (ported from the fork): the player's look-at ray leaves a free hand instead of the head, so the
// hand pointing at a door, a crate or a person is what selects it, and mods/cet/CyberpunkVRPort_Interact turns the
// reach into the game's X (src/Hooks/HandInteract.cpp).
namespace cvr::interact {
// The player's own look-at ray, when no story mode owns it (Hooks/StoryAttention.cpp): rewritten from the pointing
// hand's palm -- position, then the quaternion the engine reads +Y of. False when no hand points.
bool AimFromHand(float* transform);
// The hand the ray last left: -1 none, 0 left, 1 right.
int PointingHand();
// From script, once a frame: whether the look-at found an object, and for which hand's ray (SetVRLookAtAcquired).
void NoteAcquired(bool found, int hand);
}
