# Engine stereo: framegen per eye, foveated DLSS per eye (2026-10-24)

Engine stereo (`xr_engine_stereo=1`, render feature 98) renders both eyes side by side in MAIN's one frame. The
port's frame generation and foveated DLSS were built for MAIN + VRCAM, one view per eye. Checked on AutoSave-8
(street), OpenXR simulator, Quest 3 profile, `xr_framegen=1` backend 1 (NVIDIA OFA + FidelityFX) and 2 (DLSS-G),
with every upscaler 2.5 has on NVIDIA: DLSS Balanced, TAA (Off), FSR 2.1 Quality, XeSS 2 Quality.

The upscaler is `/graphics/presets ResolutionScaling` in UserSettings.json, value AND index (the list is
sub_1420716C0: Off 0, DLSS 1, FSR2 2, FSR4 only when the driver offers it, XeSS 3 on this machine). A value with
another index, or "FSR3" (sub_142071250's list, Off/DLSS/FSR3/FSR4/XESS, is the frame generation one), falls back
to Off: TAA at the full 5296x2864.

## Framegen: both eyes were generated from the whole wide frame

`Framegen/Inputs.cpp` keeps one input record per view (pool 0 = MAIN, pool 1 = VRCAM). Under engine stereo there
is only MAIN, so both eyes acquired pool 0 (`Runtimes/OpenXRCapture.cpp`): depth and motion of BOTH eyes (3072x1661
under DLSS) and the head centre's camera, for each eye's generation at one eye's colour (2648x2864). Each eye was
interpolated on the two eyes' motion squeezed into one, with a camera half an IPD off.

Fix:

* `RecordInputs`: under engine stereo MAIN's inputs are recorded as two records (`RecordSide`), the left half into
  pool 0 (view 1) and the right half into pool 1 (view 2) -- the Streamline tags (DLSS) and the native consumers'
  (TAA, FSR, XeSS; a native tag is the whole resource) alike. Each half gets its eye's camera (`CameraOfEye`): the
  centre's moved sep/2 along its right vector, the eye's own lens window (aspect, NDC shift; `EyeWindow`), the motion
  scale of a half-width input (x2: the engine normalises the vectors to the whole width), the jitter in the eye's
  own pixels (the eye builds turn it into NDC with the view's width: x0.5 with `EyeJitterWidth` 0).
* The eye's pixel size comes from `engine_stereo::EyeSize` (half the size the view prepare hands the centre), not
  from the tags: the engine tags one resource per `slSetTag` call (colour first), and the record and its camera
  are made by the first call -- the first build took the jitter unscaled.
* `Evaluate` completes both eyes' records on MAIN's evaluate.
* `OpenXRCapture`: eye e (which shows half e) acquires view e+1; a menu's squeezed window acquires none (framegen
  does not generate under a menu).

Checks (`tools/framegen_tests/capture_sweep.py` captures 3 real frames per eye; scratchpad `mvcheck.py` projects
every depth pixel through the recorded cameras and compares with the engine's vectors):

| upscaler (inputs) | per-eye depth / motion | vectors against camera geometry (yaw, both eyes) |
|---|---|---|
| DLSS (Streamline tags) | 1536x1661 / 1536x1661 | ratio 1.000, median error 0.00 - 0.01 px |
| TAA (native source 1) | 2648x2864 / 1324x1432 (TXAA's velocity is half size) | 1.000 - 1.005, 0.03 - 0.06 px |
| FSR 2.1 (native source 2) | 1765x1908 / 1765x1908 | 0.999 - 1.004, 0.01 px |
| XeSS 2 (native source 5) | 1558x1685 / 1558x1685 | 0.999 - 1.002, 0.01 - 0.02 px |

Jitter x within +-0.25 eye pixels in every case. Live DLSS-G (backend 2, XeSS run): one NGX feature per eye
(1558x1685 -> 2648x2864, 479 MiB each), 45 -> 90, 4.1 ms. Translation (strafe) checked under DLSS: 0.03 px.

Replay of an even yaw capture (build/estereo-perf-20261024/captures/20261024-212738, `framegen_tests replay`),
PSNR whole / moving region:

| | eye 0 | eye 1 |
|---|---|---|
| repeat frame 2 | 26.09 / 13.22 dB | 25.62 / 13.92 |
| repeat frame 0 | 26.89 / 16.35 | 26.42 / 16.72 |
| FidelityFX FI | 29.42 / 17.64 | 29.37 / 18.76 |
| DLSS-G | 30.30 / 19.94 | 30.29 / 21.11 |

## Foveated DLSS per eye

It was switched off under engine stereo ("one DLSS over both eyes"). Per-eye DLSS (Stereo/EngineStereoDlss.cpp)
already evaluates each eye on its own viewport with its half as the whole input and output (the right eye on its
own copies at (0,0)), which is what the foveated path needs; only both evaluates carry MAIN's view. Now
`engine_stereo::dlss::EvaluatingEye()` (thread-local, set around each eye's evaluate) tells `FoveatedDlss` the eye:
own state, own pair of NGX features per eye. Live: both eyes "Active", 1141 evaluations each, 0 failures, centre
1091x1093 -> 1881x1885 at 382,706 of 2648x2864, 1.36 ms per eye after the pipeline; the boundary sits at the same
place of both eye images. Still off when per-eye DLSS is off (one evaluate over both eyes).

The centre region is placed as in the other modes: at the user's offsets from the IMAGE centre. With asymmetric
lens windows the straight-ahead point is ~12 % of the width towards the nose (Quest 3: shift +-0.2425 NDC), the same
in the MAIN/VRCAM mode; not changed here.

## Frame capture: device removed under TAA

`Framegen/FrameCapture.cpp` cleared its frames when a frame did not follow the last one BEFORE checking that their
copies had finished. Under TAA a real frame shown twice restarted the sequence while the previous frame's readback
copies ran: the GPU wrote into freed buffers (Aftermath: page fault, write, copy engine; DXGI_ERROR_DEVICE_HUNG),
reproduced twice. Now a repeated frame is skipped and nothing is released before the fence passed.

## Capture tool

`capture_sweep.py` sent absolute head poses with y = 0: the head went to the floor (the simulator stands at
y = 1.7). It now sweeps around the simulator's current pose and puts it back, streams the poses without waiting for
each acknowledgement, and prints the motion per frame interval: the simulator takes head commands at its preview
rate (30 Hz), so at 45 real frames one interval of three often holds no motion and the replay is then not a fair
midpoint test (retake).

## Jitter width (not changed)

`CyberpunkVR_EngineStereoEyeJitterWidth` 1 (each eye's jitter covers a whole eye pixel, consistent with what a
whole-frame upscaler is told) is no better on still frames (scratchpad still_ab.py: 3 captures, frame-to-frame
difference and Laplacian variance of the world beyond 3 m, per eye):

| | shimmer 0 / 1 | sharpness 0 / 1 |
|---|---|---|
| TAA | 0.65 / 0.77 | 705, 837 / 601, 729 |
| FSR 2.1 | 0.63 / 0.66 | 597-616, 713-725 / 593, 707 |

Default stays 0. FSR 2.1 and XeSS run ONE dispatch over both eyes here (no per-eye split like DLSS's); their
second-eye contexts (Stereo/UpscalerViews.cpp) stay unused.
