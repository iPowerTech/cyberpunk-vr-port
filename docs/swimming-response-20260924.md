# Earlier swimming input and stroke-speed boost

The user reported delayed propulsion after a hand stroke and requested normal
or fast swimming according to gesture speed. The work started with PID20940
underwater: Swimming=2, fast=0, valid tracked hands and native input bound.

Previously, the detector waited for both hands to pull 16 cm, mean travel 20 cm,
outward travel 4.5 cm and at least 120 ms before starting an 80 ms attack. Every
full impulse then requested ToggleSprint regardless of hand speed.

## Gesture behavior

- The existing ready pose and two-hand ownership remain. Forward input can begin
  after both hands travel back 3.5 cm, mean travel 5 cm and outward 2 cm, with at
  least 40 ms of motion and bilateral positive velocity. The attack is 35 ms.
- Early input expires 120 ms after the last advancing evidence. An aborted or
  held partial gesture does not receive a full glide. Full-stroke completion
  retains the previous geometric evidence and commits 1.15 seconds of glide.
- Hand velocity is filtered over 50 ms. At least 40 ms at mean pull speed
  0.75 m/s or higher, with both hands contributing, enables boost. Sustained
  speed below 0.45 m/s for 80 ms removes previous boost, including during an
  existing glide. The gap provides hysteresis against tracking noise.
- Gentle strokes provide full ordinary forward input without ToggleSprint.
  Strong strokes request the existing native fast-swim action. To clear the
  game's latched SprintToggled state, a gentle stroke briefly caps forward at
  0.75 until fast-state feedback clears, then returns to full ordinary input.
  Physical sprint and manual stick movement retain priority.
- Ascend takes precedence over forward, dive and boost. Its gesture thresholds,
  native vertical forces, water posture, direction and collision handling remain
  unchanged. No velocity is directly added.

The existing state bridge/API remains compatible. GetVRSwimmingDebug adds index
14 for requested gesture boost and 15 for filtered stroke speed. Controls help
and the BINDINGS guide describe gentle/fast strokes and cancellation.

## Native threshold found during live testing

The redscript fallback for minStickInputThreshold is 0.90, but the actual
underwater decision object in PID30408 returned **0.80**. A first implementation
used a 0.85 cap: the gesture dropped its boost request, but the game could remain
fast until the glide ended. A bounded observer captured native MoveY=0.85,
script MoveY=0.85, ToggleSprint=0, Sprint=0 and SprintToggled=true at threshold
0.80. This is recorded in
`build/swimming-response-20260924/native-threshold-30408.json`.

The final cap is 0.75. The regression test checks that it clears both the measured
0.80 threshold and the 0.90 fallback, rather than asserting only a chosen input
constant. A temporary script input proxy first confirmed the engine transition;
it existed only in PID30408 and was disabled before that process was closed.
Final verification below used the native DLL in a fresh process, without that
proxy or diagnostic observers.

## Automated validation

All 21 swimming CTests passed, including 144 combinations of 30/45/90/120 Hz,
four stroke durations, three pitches and three jitter seeds. Tests cover early
input, normal/fast distinction, partial-stop timeout, fast-to-gentle transition
before the previous glide expires, stationary jitter, small preparatory motion,
one-hand/common motion, tracking loss, native fast exit, water posture/feedback
and the leaf-register crash regression.

The swimming ABI target needed a rejecting stub for an unrelated world-blend
entry newly added to the shared assembly fixture; that entry is never exercised.
The Release build passed. Final evidence:

- `build/swimming-response-20260924/tests-threshold.xml`
- `build/swimming-response-20260924/tests-threshold-build.log`
- `build/swimming-response-20260924/build-threshold.log`

## Deployment and live verification

Installed DLL SHA256:
`809240b1a547bbe475b98e964b92d4faef996221eecef44b4aba24d513f0e7ba`

Deployment report:
`build/roomscale-plugin-deploy-20260924-184631/deployment.json`.
Only the plugin DLL changed in the game installation. Runtime settings and
calibration were preserved. The user relaunched the game and loaded underwater.

Final live trials used PID17216, with matching DLL/PDB, water state Diving,
valid controllers, neutral simulator HMD and no pause:

| Trial | Completed strokes | Input before completion event | Engine fast state |
| --- | ---: | ---: | --- |
| Gentle pull, 1.2 s | 1 | about 558 ms | Remained normal |
| Fast pull, 0.28 s | 1 | about 65 ms | Entered fast swimming |
| Fast 0.28 s, recovery, gentle 1.1 s | 2 | about 97 ms on first pull | Returned to normal while still moving |

In the third trial, normal-state feedback arrived about 30 ms after the gentle
intent removed boost, about 178 ms before the previous fast glide would expire.
All three trials ended with forward input zero. The post-check confirmed four
total strokes, no accidental ascents, fast=0, automatic=0 and all four synthetic
native actions at zero. Both controller poses were read back and matched the
saved positions/orientations; settings still matched the deployment hash.

Evidence is in `build/swimming-response-20260924/`:
`slow-17216.json`, `fast-17216.json`, `fast-to-gentle-17216.json` and
`verified-17216.json`. The reusable probe is
`tools/swimming_tests/live_response.py`; use `--followup-duration` for the
overlapping fast-to-gentle case. It changes simulator controller poses and reads
native state, without writing game memory, settings or direct player position.

These are sampled simulator timings with sequential controller IPC, not a
hardware latency benchmark. After installation and live verification, the user
tested swimming in a physical headset and confirmed that it works correctly.
