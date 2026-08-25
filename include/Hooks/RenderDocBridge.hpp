#pragma once

#include <windows.h>

// Coexistence with a RenderDoc capture layer. See src/Hooks/RenderDocBridge.cpp for why this exists --
// short version: a MinHook trampoline built from a plain GetProcAddress bypasses RenderDoc's patch, so
// our detour hands back an UNWRAPPED DXGI factory, and the unwrapped device that follows makes the game's
// NVAPI init fail (reported on screen as ray tracing failing to load).

namespace cvr {

// Use this for any detour target that RenderDoc may also hook: it returns RenderDoc's serializer when a
// capture layer is resident and has hooked the function, and the loader's export otherwise.
void* RenderDocResolveHookTarget(HMODULE module, const char* functionName);

// The loader's export regardless of the capture layer. Only for a detour that must reach the real entry
// point -- a deliberate choice, hence the separate name.
void* RenderDocResolveRawTarget(HMODULE module, const char* functionName);

// Allow the game's NVAPI through the capture layer. RenderDoc refuses NVAPI by default, which makes
// Cyberpunk fail its NVIDIA-side initialisation and report that ray tracing could not load -- even with
// ray tracing switched off. Call this BEFORE anything touches NVAPI; it is a no-op when renderdoc.dll is
// not resident. Returns true when the option was accepted.
// Put our detour BEHIND RenderDoc's hook for one function, so the order becomes
//     game -> RenderDoc's hook -> our detour -> the real export
// Pass MinHook's original-function pointer as `trampoline`; anything else rebuilds the recursion this
// exists to avoid. Returns false when there is no capture layer or it does not hook that function.
bool RenderDocChainBehindHook(const char* module, const char* functionName, void* trampoline);

bool RenderDocAllowNvApi();

}  // namespace cvr
