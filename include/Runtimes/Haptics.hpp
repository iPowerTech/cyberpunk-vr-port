#pragma once

// Haptics -- the controllers' vibration, one profile per thing the hand should feel.
//
// Callers on any thread ask for an EVENT; the XR frame thread plays it (Tick, after xrSyncActions),
// so xrApplyHapticFeedback only ever runs where the session's actions are synced. Each event is a
// short envelope of up to three segments (amplitude, milliseconds), so a shotgun can thump and decay
// where a pistol only snaps. Profiles, the master switch and the scale are exports
// (CyberpunkVR_HapticProfiles, CyberpunkVR_Haptics, CyberpunkVR_HapticScale); the last two are also
// ini keys (xr_haptics, xr_haptic_scale).

namespace cvr::haptics {

// Hand mask bits for Play.
inline constexpr int kLeft  = 1;
inline constexpr int kRight = 2;

// Event numbers, shared with the scripts (VRHapticEvent). Shots are not here: RecoilOnShot picks
// the shot profile from the weapon class itself.
enum Event : int {
    kBeltTouch     = 1,    // a hand reached a tactical-belt element
    kGrenadePin    = 2,    // the pin came out -- the hand that pulled it
    kGrenadePinHold = 3,   // ...and the hand holding the grenade, weaker
    kMeleeBlock    = 10,   // a melee hit taken on the block
    kMeleeParry    = 11,   // a melee hit parried (deflect timing)
    kBulletBlock   = 12,   // a bullet stopped by the blade
    kBulletDeflect = 13,   // a bullet deflected or parried back
};

// One round left the barrel. weaponClass is CyberpunkVR_WeaponClass (1 handgun, 2 rifle/SMG/LMG,
// 3 shotgun, 4 sniper/precision); the trigger hand is the right one, and the left feels it too when
// the weapon is in a two-handed grip (CyberpunkVR_TwoHandActive) -- one hand on it, one hand felt.
void Shot(int weaponClass, bool supportHandOnGun);

// A scripted event on the hands in handMask.
void Play(int event, int handMask);

// XR frame thread only: start whatever segment is due.
void Tick();

}  // namespace cvr::haptics
