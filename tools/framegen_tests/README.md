# FPS statistics, depth inputs and stereo depth checks

The frame generator is gone; what is left under `src/Framegen` is the depth input
capture the stereo depth layer uses, the presentation bookkeeping (real frame or
repeat) and the FPS overlay with its statistics. These cases cover that code.

Runtime counters and exported JSON require launcher DEBUG or the individual
`CyberpunkVR_RuntimeDiagnostics` gate. `live_probe.py` reports the gate state;
`motion_probe.py` refuses to run while it is disabled. The `debug_gate` case checks
that FPS metrics stay enabled independently of diagnostic exports.

```powershell
cmake -S tools/framegen_tests -B build/framegen-tests -A x64
cmake --build build/framegen-tests --config Release
ctest --test-dir build/framegen-tests -C Release --output-on-failure
```

The GPU checks use WARP, except `hardware`, which reads the NVIDIA adapter's
telemetry. The D3D12 debug layer's errors fail the tests.

The cases cover real-frame vs repeat counting, settings round trips, unique-frame
vs repeat counters, the stereo depth layer's identity/FOV/plane checks and its
per-eye GPU output, depth and motion input copies, and the stats overlay's single
BOTH-eye layer, zero-timeout acquire/wait ownership and limited refresh rate.

`metrics_disabled` verifies that turning off the FPS overlay stops counters and
hardware monitoring. The XR overlay test covers disabling with an acquired image
that is not ready yet: it becomes invisible immediately and retires its resources
after a successful wait/release.

These tests do not validate the game's resource pairing or physical-headset
performance. Those require the freshly deployed plugin, a matching PDB and live
input-stage/report counters, followed by the user's visual check. Never use
RenderDoc replay while the game is running.
