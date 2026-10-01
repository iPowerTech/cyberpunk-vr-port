# Native frame-generation checks

Runtime counters and exported JSON require launcher DEBUG or the individual
`CyberpunkVR_RuntimeDiagnostics` gate. `live_probe.py` reports the gate state;
`motion_probe.py` refuses to run while it is disabled. The `debug_gate` case checks
that FPS metrics and framegen remain enabled independently of diagnostic exports.

Build the pinned FidelityFX SDK via the main project first, then:

```powershell
cmake -S tools/framegen_tests -B build/framegen-tests -A x64
cmake --build build/framegen-tests --config Release
ctest --test-dir build/framegen-tests -C Release --output-on-failure
build/framegen-tests/Release/framegen_tests.exe gpu_nvidia
build/framegen-tests/Release/framegen_tests.exe gpu_nvidia_flow
```

The last two commands require NVIDIA OFA hardware and exercise it directly. The
other GPU checks use WARP. The moving-box fixture supplies real synthetic motion
vectors/depth for two different eye images. It checks the interpolated centroid,
eye isolation, initial reset, and refusal to free a consumer-owned output.
The D3D12 debug layer's errors fail the tests. The NVIDIA case caught an actual
teardown crash: registered buffers must be released before destroying the OFA
context. The original dump/log are kept under `build/framegen-native`.

Other cases cover native frame gaps and recenter/cut rejection, midpoint/endpoint
ordering when a newer frame arrives or generation is disabled, settings round
trips, unique-frame vs repeat counters, and the stats overlay's single BOTH-eye
layer, zero-timeout acquire/wait ownership and limited refresh rate.

These tests do not validate the game's resource pairing or physical-headset
performance. Those require the freshly deployed plugin, a matching PDB and live
input-stage/report counters, followed by the user's visual check. Never use
RenderDoc replay while the game is running.

The NVIDIA path uses OFA plus FidelityFX Frame Interpolation. Its experimental
custom interpolator was removed after the user found severe gameplay ghosting;
small fixture passes had not established real-scene image quality.
`gpu_nvidia` tests engine MV/depth at moving occlusion edges;
`gpu_nvidia_flow` tests visible motion on a plane with zero engine motion vectors,
as happens with some shading effects. Both report image error against the known
midpoint as well as per-eye centroids. The corresponding FidelityFX cases use
the complete AMD pipeline. GPU fixtures also toggle telemetry off/on while
generation keeps running and verify that GPU timing resumes. These small fixtures establish correctness only;
their costs and errors are not an in-game quality/performance ranking.

`pacing` migrates16 Present calls between workers. The target150ms run took6ms
with a thread-local deadline; the shared render-stream schedule fixes that.

`metrics_disabled` verifies that turning off the FPS overlay stops counters and
hardware monitoring even while frame generation remains enabled. The XR overlay
test covers disabling with an acquired image that is not ready yet: it becomes
invisible immediately and retires its resources after a successful wait/release.
