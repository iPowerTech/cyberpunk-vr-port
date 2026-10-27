# OpenXR Simulator preview camera mismatch

The composed preview, not just the application's HUD pose diagnostic, confirmed
the user's left/right discrepancy on PID 12008. Capture:
`build/hud-cone/v3-preview.bmp`; projection metadata:
`build/hud-cone/v3-projection-log.json`.

Cyberpunk submits symmetric FOV +/-0.94719 radians for both projection images.
The configured Quest 3 simulator frusta are left -54/+40 and right -40/+54 degrees.
`presentProjection` blits the raw submitted images to each preview half, whereas
`renderQuadLayer` formerly used `GetViewFov` and the current tracking pose to draw
HUD/menu quads. This produces about 24% of an eye-width horizontal discrepancy
even when the application's two HUD rays match. The earlier angular-only tests
were valid but did not cover this mirror-composition error.

The simulator now remembers the world-space pose and FOV of each projection view
for the current frame and uses them for overlay projection in both D3D12 and
D3D11/OpenGL preview paths. Reference-space transforms are shared with quad pose
conversion. Cropped/full-texture preview rectangles adjust the frustum in tangent
space. Each new frame clears the camera selection, so quad-only menus fall back
to the runtime views. Invalid poses, frusta and rectangles also use that fallback.
No application's xrLocateViews output, headset profile, active-runtime selection
or Cyberpunk HUD quad geometry is changed by this fix.

Source project: `C:/Users/dariulone/Desktop/OpenXR-Simulator`.
Preview fix commit: `40eef97e89c16c32c8666d02bb3b4eeafbc98f82`.
The existing uncommitted simulated-input gate was preserved. An isolated worktree
at `build/hud-preview-runtime` contains HEAD 8de3457, that existing input change,
and this fix. The fix alone is retained as
`tools/hud_tests/openxr-preview-projection.patch` and was also applied to the
source project without replacing its existing edits.

Build with CMake/VS x64 Release and Vulkan headers from
`C:/Users/dariulone/Desktop/Vulkan-Headers/include`.
All seven simulator tests passed; five new cases cover asymmetric runtime versus
submitted FOV, rendered pose versus newer tracking, per-frame/eye reset, full
texture cropping, and invalid data. The tests use checks active in Release.
Logs: `build/hud-cone/preview-runtime-build.log` and `preview-runtime-tests.log`.

Deployment backup: `build/hud-preview-runtime-deploy-20260923-124318`.
Installed file:
`C:/Users/dariulone/Downloads/OpenXR-Simulator-v1.5.0-win64/openxr_simulator.dll`.
Previous SHA256: `eced7a76d3dae250abf2f0e42b97cbc164aeab564016aac6d48d22453f0385be`.
New SHA256: `b2dcfd4ccef0403e5645ee7ecc93db4c740c6a94f454786f25c6db8fc71518aa`.
The manifest, simulator settings and vrport.ini hashes were unchanged by deployment.
With the game closed, the archived `openxr_simulator.before.dll` restores the
previous runtime at the same installed path. The Cyberpunk plugin remains
0135c967a8047546dd8e5286361ad5cb44d0a29cb2fdb7e0b0a6c5286b747230.

Post-deployment verification: PID 27216 loaded the installed DLL with the new
SHA256. `build/hud-cone/v4-preview.bmp` and `v4-preview-status.json` contain the
fresh composed stereo frame (12:46:04 local time). HUD elements now occupy matching
positions relative to each eye's displayed image, including the minimap, quest
list, lower-left actions and lower-right hints. The user also confirmed that it
looks correct. The source collector still reports 23 elements, TopRightMain
width 790 and minimap x=2142 after the restart. This is simulator Preview
acceptance; no new physical-headset test was performed in this step.
