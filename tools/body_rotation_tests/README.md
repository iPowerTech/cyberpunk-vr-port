# Tracked body yaw tests

Build after configuring the main project (for OpenXR headers):

```powershell
cmake -S tools/body_rotation_tests -B build/body-rotation-tests
cmake --build build/body-rotation-tests --config Release --parallel 4
ctest --test-dir build/body-rotation-tests -C Release --output-on-failure
build/body-rotation-tests/Release/body_rotation_tests.exe benchmark
```

The tests run the production estimator on independently specified body/head
trajectories. Controller transforms are generated in tracking space, localized
to each head sample and then consumed through the production decoder. Ground
truth body yaw is specified separately from the head direction and arm gestures.

25 groups cover more than 3000 trajectories: ten hand configurations, both directions,
multiple rates/speeds, tiny turns, repeated full turns, head-only motion, wrist
aiming, walking, asymmetric reaching, changing neck geometry, independent
position/orientation noise, missing devices, invalid values, time/order/origin
changes and native 32-bit sequence wrapping. Noise uses reproducible seeds.

`baselines` demonstrates why HMD-only and a plain hand-line direction are
insufficient: 90 degrees of head/body disagreement and 51 degrees of rifle-pose
bias. `lifecycle` also checks the existing native-heading cancellation algebra.
The CPU benchmark excludes runtime calls and rendering.

These fixtures test the algorithm and its input contract; they do not measure a
person’s true waist orientation or exercise the game renderer. See
`docs/body-rotation-20260924.md` for researched sources, limits and live validation.

## Live simulator matrix

Run only with the game already loaded in the OpenXR Simulator, the headset at
neutral yaw/pitch/roll, and no competing simulator command producer. Python needs
`capstone` for resolving the singleton's address from the matching native code.

```powershell
python tools/body_rotation_tests/live_types.py --dll <matching-dll> --out <types.json>
python tools/body_rotation_tests/live_matrix.py --pid <current-pid> --dll <matching-dll> --types <types.json> --out <new-report.json> --case matrix
```

`matrix` runs ten postures × five speeds × two directions, with a return sweep
in each case. `negative` covers head-only movement, one-hand gestures, wrist
rotations and idle jitter in every posture. `mixed` covers walking, reaching,
counterlooking and head pitches close to vertical. `smoke --poses low_ready`
runs a short initial check. The bounded `hold` mode waits at 60 degrees for
external ordinary weapon-switch requests through WolvenKit, writes `.ready`,
and returns on `.stop` or after 90 seconds.

The harness saves the original setting/HMD/controller poses before driving and
restores them in `finally`. Native process access is VM_READ only. Exact DLL/PDB
identity is checked; reports retain raw input tuples, native body yaw, estimator
output, frame age, active cone and IPC timings. The script never starts the game.

The simulator uses an unusual opposite yaw sign for controller **positions**
relative to its quaternion, so the harness explicitly inverts that transform.
Its file IPC changes one controller at a time. These live traces consequently
include transport skew and limited command frequency; they do not represent
synchronous 90 Hz hardware accuracy. Noise is independent for all three devices
(2 mm position standard deviation, 0.1 degree yaw standard deviation).

For coherent live input, `prepare_simulator_batch.py` copies the existing simulator
checkout into a new test-only directory and adds one three-device command and
same-XrTime snapshot cache. Build/test that copy, save the original runtime
manifest before selecting it, and restore that manifest once the game has loaded
the temporary DLL. Pass its exact path as `--batch-runtime` to the matrix harness.
The harness checks the loaded module hash, restores all three device poses in one
batch, disables the override, and restores the original body-mode setting. Never
interpret the old sequential-IPC gesture failures as coherent hardware results.

Hybrid routing adds 160 posture/hand-pose/rate/direction cycles, 3000 boundary
noise samples, relative acquisition/recovery and the actual BendTracker cue.
`live_hybrid.py` checks installed hybrid defaults and records its native phase,
active cone and published pitch/bend. With the ordinary simulator it uses small
pose steps with settle intervals; these are state/endpoint checks, not continuous
motion latency measurements. `--batch-runtime` enables the coherent test runtime
when already loaded. Device poses and the tracked-only switch are restored,
leaving the configured hybrid setting unchanged.
