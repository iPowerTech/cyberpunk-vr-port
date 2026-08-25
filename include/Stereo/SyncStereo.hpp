// Path B (Sync Sequential) native hooks.
//
// M-B1: observational hook on CALLER1 (RenderFull, sub_140292A54 @ image base
// 0x140000000, verified against game build 2.31). We hook it, call the
// original, and record mgr/vtable/call-frequency into atomics. NO LIGHT call
// yet вЂ” that is M-B2.
//
// Everything is opt-in via IPC so a fresh boot installs no engine hooks.

#pragma once
#include <cstdint>

// True while the render graph is executing a node belonging to the VRCAM view on the
// CALLING thread. The engine camera hooks use this to stay out of the second view --
// see the definition in sync_stereo.cpp for why they otherwise corrupt MAIN's pose.
extern "C" __declspec(dllexport) int CyberpunkVR_IsVrcamViewActive();

// Exact view identity for the same hooks. MAIN is view key 0; VRCAM has its own key; the
// engine's other views (distant geometry, shadows, reflections) have theirs. "Not VRCAM"
// is therefore NOT "is MAIN", and writing the head camera into one of the others makes
// its content slide with the head instead of staying world-locked.
extern "C" __declspec(dllexport) int CyberpunkVR_IsMainViewActive();
// 0 = the caller is not inside a view-carrying node dispatch (view genuinely unknown).
extern "C" __declspec(dllexport) int CyberpunkVR_GetActiveViewKey(unsigned long long* out);

// CName hash of the VRCAM camera component. The camera object stores its own component
// name at obj+0x40, so comparing against this (and against cname_hash("camera") for the
// player camera) identifies the view exactly, per instance.
extern "C" __declspec(dllexport) unsigned long long CyberpunkVR_VrcamCamNameHash();

namespace cvr {

// Lightweight: just remembers the exe base. Safe to call from DllMain.
void sync_stereo_init();

// Called from the IPC worker immediately after DllMain returns. Installs only
// the early native request-capture hook, before renderer graph registration.
void sync_stereo_install_early_hooks();

// Early (pre-device) probe: MinHook d3d12!D3D12CreateDevice, then patch the
// device vtable slot 14 (CreateDescriptorHeap) so we can log every heap desc
// (Type/Flags/NumDescriptors + caller RVA) and optionally enlarge the
// shader-visible CBV_SRV_UAV heap. Idempotent; safe to call from DXGI exports.
void sync_stereo_ensure_descriptor_probe();


} // namespace cvr
