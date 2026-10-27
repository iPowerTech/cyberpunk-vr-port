# Native roomscale checks

```powershell
cmake -S tools/roomscale_tests -B build/roomscale-tests -A x64
cmake --build build/roomscale-tests --config Release
ctest --test-dir build/roomscale-tests -C Release --output-on-failure
```

The model tests use the production accounting code. They simulate mismatched
XR/game cadence, a blocked capsule, simultaneous WASD and velocity feedback,
rather than invoking any game function or changing game memory. The PE check
only reads the installed executable; override `CP2077_EXE` when configuring.

In-game acceptance and remaining contact-attribution limits are documented in
`docs/roomscale-native-implementation.md`.

## Background OpenXR Simulator probe

`simulator_probe.py --out <fresh-json>` sends a bounded2cm head translation and
restores the original pose via the simulator's file-command channel. It never
changes window focus or sends keys. It requires neutral angles and a running
OpenXR Simulator session. To also record game results, resolve the plugin's
`CyberpunkVR_RoomscaleDebug` and `CyberpunkVR_RoomscaleDebugSeq` exports afresh
in x64dbg, then supply `--pid`, `--debug-address` and `--sequence-address`.
The reader uses only QueryLimitedInformation + ReadProcessMemory and an even,
unchanged publication sequence around each168-byte diagnostic snapshot.

`simulator_sweep.py` adds bounded continuous XY/vertical/turn trajectories and
optional read-only native animation observations. `--native-pulse` is a separate
keyboard test: it uses the existing focus-preserving native input helper, then
requires release cleanup. Pose-only tests never change focus.

`simulator_recenter.py` drives the existing VRIK recenter command file while the
head is held translated; it checks capsule/body continuity and resets of the
tracking generation. `capture_preview.py` captures the simulator HWND without
activation. `verify_live_session.py` verifies saved observations and writes a
hash-indexed acceptance report; `analyze_sweep.py` summarizes failed probes.

For the HMD-only turn regression, `simulator_camera_probe.py` records fixed-point
camera centres, model-bake publication and settled head/hips snapshots. Its
`--mouse-x` option uses real mouse input with the native helper's focus protocol;
the simulator-turn mode does not change focus. `analyze_turn_comparison.py`
verifies the pre-fix frame error; `verify_turn_fix.py --out <fresh-json>` validates
the saved PID19332 after-results against the body-local anatomical offset.

`body_temporal_probe.py` records dense CCT/camera/body and native/Lua source
packets during fast physical translation or a bounded native `D` pulse.
`analyze_body_temporal.py` reports moving-phase variation rather than only settled
endpoints; `verify_temporal_before.py` validates the saved pre-correction timing
evidence. Its address JSON contains build/PID-specific private symbols: resolve
them anew through the built-in debugger MCP before reuse.

The common pose-chain regressions `eye_float_alias`, `render_roundoff`, and
`native_camera_pair` use captured PID34628 values. `hand_view_sample` checks
that HMD motion cancels for a stationary controller using the same head
reference, and `frame_aim` checks concurrent publication of the OpenXR target
time, publication stamp and epoch as one tuple.

Run `python tools/roomscale_tests/regression_probe.py` to reintroduce the old
float-centre comparison, bitwise quaternion comparison and world-float
subtraction in build-only header copies. Each of the three matching tests must
fail with its expected assertion. The probe also restores the former 10-micron
label tolerance; `render_quantized_labels` must detect that fourth regression.
The original headers and game DLL remain
unchanged. Results are saved in `build/pose-chain-regression-probe/results.json`.

PID15084 added a captured VRCAM rotation at -40 degrees pitch (0.001405-degree
matrix round-trip difference), plus coincident camera transforms whose head
labels differ by 0.222mm. Label equivalence uses the camera world's float ULPs,
bounded to 0.25mm in vector length; origin, eye and fixed-point position remain
exact. Tests reject a real 1mm displacement, false equivalence near world zero,
and diagonal error exceeding the cap.
