#pragma once
// WALKING, MEASURED (src/Hooks/WalkDiag.cpp). Walking slowly forward looks like an advance in micro-steps of different
// speed while an NPC passing a standing player is smooth (2026-11-03): the player's own motion, not the presentation.
// With the XR frame-path report on (F10 Diagnostics), once a second while the player moves, one [walk] line: the
// player entity's step per entity tick (its speed, how evenly the ticks come), the stick the game was handed, and the
// engine camera's step per rendered frame. Off, nothing is recorded.
namespace cvr::walkdiag {
// The pad's left stick as the XInput merge hands it to the game (Hooks/XInput.cpp), -1..1.
void NoteStick(float x, float y);
// The player entity's world position at an entity tick (Natives/CameraPair.cpp), on that tick's thread.
void NoteEntity(float x, float y, float z);
// The engine camera's world position as a locate entered (Hooks/LocateCamera.cpp), before the port's own writes.
void NoteCamera(float x, float y, float z);
// The position the frame is drawn from: the main view's world camera once the view is prepared
// (Stereo/AsymProjection.cpp), everything the port writes included. With these three, a second [walk] line,
// [walkstep]: each one's step per sample in millimetres while the stick is pushed, sample by sample.
void NoteRendered(float x, float y, float z);
}  // namespace cvr::walkdiag
