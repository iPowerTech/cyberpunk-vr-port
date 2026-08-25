#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdint>

void OverlaySetDeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
void OverlaySetWindow(HWND hwnd);
void OverlayRender(IDXGISwapChain* swapChain);
void OverlayInvalidateSwapchainResources();
// Open the overlay's full-drain window for a few seconds. Called from anywhere the game is churning
// render resources -- a save load, a swapchain invalidate, a VRCAM component re-bind -- because that is
// the one window in which the faster pacing was measured to hang the device. See ImGuiOverlay.cpp.
void OverlayArmLoadGuard(const char* reason);
bool OverlayIsVisible();

// THE OVERLAY IN THE SECOND EYE.
//
// Eye 0 is MAIN's backbuffer, which the overlay is drawn straight into; eye 1 is the VRCAM view,
// a texture the engine renders without any knowledge of us, so everything ImGui draws was simply
// missing from that eye -- menu included. This records THIS frame's draw data a second time,
// into `target`, on the caller's command list.
//
// Contract, all of it checked rather than trusted, because every item is a device-removal class
// mistake: target must already be in RENDER_TARGET state, be the same size as the backbuffer
// (the draw data is in backbuffer pixels and is not scaled), and be in the same format family
// the ImGui pipeline state was built for. Anything else is refused with one log line.
//
// shiftPx slides the whole overlay horizontally, which is how a flat panel is given a DISTANCE:
// only this eye can move (MAIN's copy is the engine's own draw), so the full disparity goes
// here, exactly as the HUD composite does it. 0 leaves the panel at optical infinity.
//
// The background draw list is deliberately EXCLUDED -- see OverlayDebugDraw.cpp.
bool OverlayRecordIntoTarget(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* target,
                             float shiftPx);

// 1 = draw the overlay into the second eye (default). The distance in metres the panel is placed
// at in that eye; 0 = optical infinity, matching CyberpunkVR_HudDistanceM's own default.
extern "C" __declspec(dllexport) int      CyberpunkVR_OverlaySecondEye;
extern "C" __declspec(dllexport) float    CyberpunkVR_OverlaySecondEyeDistM;
extern "C" __declspec(dllexport) uint64_t CyberpunkVR_DebugOverlaySecondEyeDraws;
extern "C" __declspec(dllexport) uint64_t CyberpunkVR_DebugOverlaySecondEyeSkips;
