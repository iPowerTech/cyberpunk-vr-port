# Pose identity protocol tests

The tests compile the production camera-address ledger, GPU submission ledger,
capture-buffer reservations, and eye-pose rebasing math. No game process, GPU,
OpenXR runtime, or headset is simulated as a successful integration test.

```powershell
cmake -S tools/pose_identity_tests -B build/pose-identity-tests
cmake --build build/pose-identity-tests --config Release
ctest --test-dir build/pose-identity-tests -C Release --output-on-failure
python tools/pose_identity_tests/regression_probe.py
```

Covered cases: identical transforms with different IDs, reused source addresses,
source changes during a copy, unknown-source invalidation, bounded storage,
recording vs submission, discarded recordings, independent MAIN/VRCAM images,
last-write order, destroyed/reused GPU resources, reader/writer reservations,
rebasing static eye geometry onto the image's own head pose, and carrying a
common HMD sample through native camera blending while rejecting mixed,
untracked or concurrently changed inputs.

`cached_camera_lifetime` reproduces PID25496: an unchanged VRCAM descriptor must
keep its original ID while MAIN publishes thousands of updates. Capacity limits
distinct objects; lookups refresh their use order without changing generations.
The regression probe restores the former publication-ring eviction and requires
this test to fail.

The ABI check verifies the exact installed game SHA256 and sixteen hook/call-site
signatures. The notification-thunk probe also checks volatile registers, flags
and XMM0..5 against the original virtual-call path after a deliberately clobbering
callback. The regression probe mutates only build-directory header copies:
old generations, missing reset invalidation, and overwrite of an active reader
must each trigger the corresponding assertions.

Live checks use freshly resolved symbols in `tools/vrik_tests/live_symbol_map.json`.
Capture/submit ID arrays use physical XR eye indices. Source counters use MAIN
and VRCAM order. High-bit native IDs are stored as hex strings in live reports.
