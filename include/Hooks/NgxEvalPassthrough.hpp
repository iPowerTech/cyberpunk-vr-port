#pragma once

// Bisection step 1: a pure pass-through MinHook on the core NGX
// NVSDK_NGX_D3D12_EvaluateFeature. It installs the detour, counts calls, and
// does nothing but re-enter the original. Its only purpose is to answer one
// question: does merely hooking that function cause the "surfaces changing
// tonality" artifacts, before any resource is read or written?
//
// Call TryArm() from the Present path each frame until it returns true; the
// module is loaded lazily so the first attempts fail cheaply.
bool NgxPassthroughTryArm();

// Bumped once per Present so the NR path has a frame boundary that is the SAME
// for both eyes of a rendered frame. Used by the alternate-eye NR mode to pick
// which single eye is denoised this frame (even frame = left, odd = right).
void NgxPassthroughOnPresent();
