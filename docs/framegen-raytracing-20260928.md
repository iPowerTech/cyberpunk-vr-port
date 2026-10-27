# Frame generation with ray tracing

Verified live on 28 September 2026 in PID 12128. The user's settings were
ray tracing enabled, DLSS Balanced, ray reconstruction disabled, and path
tracing disabled. These graphics settings were not changed during the fix.

## Cause

The native Streamline MotionVectors tag carried a 1485x1485 texture with
DXGI_FORMAT_R16G16B16A16_FLOAT (10). The framegen input copier accepted only
R16G16_FLOAT and R32G32_FLOAT, so it discarded every motion texture in this
mode. Both eyes received depth, but their input records had hasMotion=false.
Successful evaluation/submission counters consequently stayed unchanged and
the presenter waited for matching depth and motion vectors.

This is a texture-format mismatch, not three-dimensional motion vectors.
The captured camera constants had cameraMotion=true, motion3d=false and motion
scale (1,1). FidelityFX's frameinterpolation callbacks declare a float4 motion
texture and explicitly read its XY channels. Its DX12 backend already supports
R16G16B16A16_FLOAT, so no conversion shader or camera-flag override is needed.

## Change

Move the existing tagged-resource copy into InputCopy.cpp so the actual copy
path can be exercised by GPU tests. Accept RGBA16F motion while retaining its
format and all channels, and account for eight bytes per pixel. RG16F remains
four bytes and RG32F remains eight. The existing depth handling, native resource
state restoration, per-eye frame/pose association and submission fences stay
on their established paths.

## Verification

- Input-copy GPU regression checks native RG16F, RGBA16F and RG32F data,
  cropped extents, all copied channels, resource reuse, format transitions,
  unsupported formats, invalid bounds, depth values and allocation accounting.
- Existing planar-depth and FidelityFX interpolation cases passed.
- A new RGBA16F interpolation case passed for FidelityFX/WARP and NVIDIA OFA
  hardware. Both eyes' moving objects land at their expected midpoints. Large
  unrelated values in B/A verify that interpolation uses XY only.
- Release DLL compiled successfully.
- A signature/PID/module-checked session helper replaced only the old input
  copy function with the tested production implementation. Framegen resumed
  without changing the graphics settings or restarting the game.
- The steady RT run reported about 35.5 real + 35.5 generated FPS, with
  70.5 unique output FPS and zero repeats in the final measurement window.
  The helper recorded 7,129 RGBA copies and zero rejected motion copies at the
  recorded checkpoint. These are scene-specific measurements, not an FPS target.
- The user confirmed frame generation worked. Diagnostic sampling restored the
  original runtime diagnostic gate after every capture.

Captures and helper receipts are under `build/framegen-rt-20260928/`, including
`rt-on-before.json`, `rt-on-descriptor.json`, `rt-on-after.json`, and
`rt-on-steady.json`. Path tracing and ray reconstruction were not separately
replayed in this task.

## Deployment

Installed the rebuilt DLL and matching PDB for subsequent launches, retaining
the mapped original image and working session helper for the current process.
UserSettings.json and vrik_calibration.ini hashes are unchanged. No temporary
helper is installed in the game's mod directory.

DLL SHA-256:
`FB44BF3585DA579B222F510A138AE77A01B84A51B57FC820552C145A16ACC0B8`.
Backup and verified installation receipt:
`build/framegen-rt-20260928/deploy-190823/manifest.json`.
