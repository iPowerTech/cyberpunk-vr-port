#pragma once
// THE DESKTOP WINDOW'S VIEW OF THE BACKBUFFER (TofuExpress fork, 2026-10-16).
//
// The backbuffer is the VR render size -- tall on every headset now (Quest 3 2648x2864) -- and the
// window was sized to the whole monitor, so DXGI_SCALING_STRETCH squashed the picture. The window is
// now placed on the monitor it is on at the render's own aspect -- the whole frame, as large as the
// monitor holds, never upscaled (2026-10-18; it used to fill the monitor's width and lose the height).
// Only a window held at another shape gets the centre crop at its aspect, right before Present. The
// OpenXR capture has already copied the full frame by then, so the headset keeps the whole image.
//
// Everything that turns a desktop mouse position into a game one (Hooks/OsInput.cpp, the overlay's
// WndProc and VR UI pointer) maps through the same DesktopCoverTransform, so the cursor lands where
// it is drawn.

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

struct DesktopCoverTransform {
    // The visible part of the source, in source pixels.
    double sourceX = 0.0;
    double sourceY = 0.0;
    double sourceWidth = 0.0;
    double sourceHeight = 0.0;
    UINT destinationWidth = 0;
    UINT destinationHeight = 0;
};

// Centre crop of a source to the destination's aspect ("cover"). False on a zero size.
bool ComputeDesktopCoverTransform(
    UINT sourceWidth, UINT sourceHeight,
    UINT destinationWidth, UINT destinationHeight,
    DesktopCoverTransform& out);

// The real client rect (bypassing the GetClientRect hook that reports the virtual size) and its
// screen origin.
bool GetDesktopPhysicalClientRect(HWND hwnd, RECT& clientRect, POINT& clientOrigin);
// The outer window rect for the automatic desktop window on the monitor the window is on.
bool GetDesktopMonitorWindowRect(
    HWND hwnd, RECT& windowRect,
    RECT* monitorRect = nullptr,
    RECT* targetClientRect = nullptr);

void DesktopMirrorSetDeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
void DesktopMirrorRender(IDXGISwapChain* swapChain, HWND hwnd);
void DesktopMirrorInvalidate();
