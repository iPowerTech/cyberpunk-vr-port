# MAIN fog sharing without head-locked light beams

## Diagnosis

The user reported lamp light beams moving with head rotation in VRCAM only.
In PID 10876, ray tracing was off and local-shadow reuse was already 0.
Separate live trials of SwayTimeFix 4 -> 1 and local-shadow reuse 0 -> 1
did not change the symptom. Both values were restored to their originals.

FogHistorySync was active each frame. Disabling it immediately stopped the
motion, confirmed by the user. The user explicitly requested keeping MAIN fog
sharing, rather than shipping the disabled setting.

The old hook replaced the resource resolved at native return RVA 61D0C0.
Native assembly and the saved highlights capture identify this as the current
scattering volume consumed by the integration shader (key 5718E9A1). That
shader reads froxels directly by thread coordinates, without reprojection.
Substituting another camera's volume there leaves it in the wrong view grid.

The scattering shader instead samples temporal history through t1, bound at
native call 61CB63 (return 61CB68), using CameraShaderConsts' previous
view-projection matrix. This is the appropriate input for reprojected sharing.

## Change

- Keep FogHistorySync enabled by default.
- Publish MAIN's scattering volume with its unjittered view-projection matrix,
  fog dimensions/depth mapping, owner, frame and allocation identity.
- Borrow this paired source only at the scattering shader's history binding.
  Change only the previous-VP matrix; preserve VRCAM's current camera.
- Leave the integration input native, so it consumes the current VRCAM grid.
- Reject stale/incompatible sources, owner changes, recycled allocations and
  aliases with either current fog output. Missing data retains native behavior.
- Allocate camera constants through the native upload ring, with a private
  per-recording-thread CPU descriptor. Cache/AA and sky-radiance fixes remain on.

## Verification

The captured camera matrices for geometry and final lighting agreed within
each view. Both views contained the same 85 light positions/directions after
matching records by world position, not array index.

The reconstructed MAIN projection matched the next captured native previous-VP
matrix with a maximum float difference below 0.00006. Seven render-parity CTest
targets passed, including rotation invariance, camera-field preservation,
incompatible grids, stale frames and output-alias rejection. Release build passed.

Installed DLL SHA256:
831B586DFD247617E514D949CB743A608C987735CF836AF66BB2F9F9BA2F7424

Deployment backup: build/lamp-motion-20260928/deploy-224042/.
Game settings, launcher settings and VRIK calibration were unchanged.
After installing the new build, the user confirmed that the beam remains fixed
while turning the head. The game was no longer running at the final commit
check, so new-path live counter activity was not independently sampled.

Static analysis, shader blobs and bounded diagnostic snapshots are retained
under build/lamp-motion-20260928/. No GPU replay ran alongside the game.
