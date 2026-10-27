# Reflex and GPU utilization

PID9260, DLL429ab2cd, Framegen off, OpenXR Simulator90Hz, RTX5070Ti,
2560x2560 per eye, DLSS Balanced. No game FPS cap or VSync. Original game
setting: Reflex Enabled. These comparisons use the same stationary scene;
the game was in the background throughout the decisive mode comparison.

## Measured cause

Short GameThread8656 stack samples resolve the sleep to
slReflexSleep -> NvAPI -> nvwgf2umx -> SleepEx. The engine calls the Streamline
sleep wrapper at RVA1D50A50. This is distinct from xrWaitFrame on the XR worker.

Changing ReflexMode through CET and ConfirmChanges updated the settings object
but did not update the renderer in this session. Earlier "Reflex off" tests
therefore did not actually disable the driver latency mode. Native options
were mode1, frameLimitUs0, useMarkersToOptimize1.

The renderer singleton at3427C00 holds the Streamline object at+18008. Native
option construction at78933C reads that object's mode+1040, limit+1044 and
support byte+1048. Verified vtables: renderer312AED8, Streamline2EA4C78.
The diagnostic changed only the mode scalar, restored in a finally block.

| Phase | Native Reflex | FPS | GPU utilization |
| --- | --- | ---: | ---: |
| A1 | On | 51.18 | 86.53% |
| B1 | Off | 61.74 | 94.14% |
| B2 | Off | 63.16 | 95.14% |
| A2 | On | 50.74 | 88.58% |

Each window was7 seconds after1 second settling; foreground PID23752 stayed
the same in all phases. Actual mode was re-read in every sample. This is a
22.5% throughput gain in this scene, not a promise of90FPS or100% GPU usage.
Raw samples: build/performance-20260927/reflex-native-abba.json.

Setting useMarkersToOptimize to0 while keeping Reflex On did not help:
50.76/49.89/50.76/50.60FPS in the marker ABBA. The temporary instruction byte
was restored. The final implementation retains the native marker policy.

CETBridge file polling averaged1.9ms per call, but throttling it to10Hz while
Reflex was On did not improve FPS. That temporary wrapper was removed; the
bridge files are unchanged. Empty ImGui waiting and capture-fence waiting
remain excluded by the earlier measurements.

x64dbg's detach left the debuggee's threads suspended during the investigation.
The task's breakpoints were removed, original instruction bytes verified,
the debugger detached, and its process suspension balanced. The same game
PID resumed and CET heartbeat recovered without a new crash report.

## Persistent control

STEREO / PERFORMANCE now contains NVIDIA Reflex: Game setting, Off, On, On +
Boost. xr_nvidia_reflex=-1 follows the game (the original default);0/1/2 override the native
latency mode while the XR session runs. Off may improve FPS at the cost of
additional input latency; the UI states that tradeoff. It also shows the last
successfully applied native mode, so changing a setting alone is not presented
as confirmation of activation.

Update 2026-09-28: the default is now Off (0), including fresh configuration,
missing-key fallback, and the shipped game preset. An explicit -1 still follows
the game. See `steam-frame-reverb-defaults-20260928.md`.

The byte-validated2.31 hook wraps the game's slReflexSetOptions call. It copies
the known48-byte v1 structure and changes only mode. It preserves frame limits,
marker flags, hotkey/thread fields, extension pointer, native return status and
the original slReflexSleep/PCL marker calls. Unrecognized structures, inactive
XR, invalid settings and Game setting pass through. No new GPU resources,
render passes, CPU sleeps or per-frame logs are introduced.

Tests cover structure ABI, preservation of all other bytes and the original
input, non-XR pass-through, unknown versions/types, extension pointers and
invalid configuration. Release build and deployed runtime validation are
recorded below when completed.

Release build and the options-preservation test passed. DLL
`a1aba3cc0168bbe433ba08c7f01ac6828016b750ae9ebc356da48efe94ac74a9`
was installed and hash-verified. The preceding DLL/PDB and vrport.ini are
archived in build/performance-20260927/deploy-reflex. Only the new local
`xr_nvidia_reflex=0` preference was added; game settings/calibration and
Framegen/FPS-overlay preferences were retained. Native mode, marker byte and
profiling gates from the temporary experiments were restored before PID9260
was closed for deployment. Runtime verification of the persistent hook awaits
the user's next launch; the measured gain above is from the native-mode A/B.

## Deployed verification

User launched PID20280 and loaded gameplay. Hook log says NvidiaReflex ok;
selected and successfully applied modes both read0. Installed DLL/PDB symbols
match A1ABA3CC. A second comparison changed xr_nvidia_reflex through vrport.ini,
exercising the persistent setting and hook together:

| Phase | FPS | GPU utilization |
| --- | ---: | ---: |
| Off A1 | 63.87 | 95.31% |
| On B1 | 54.76 | 90.28% |
| On B2 | 51.32 | 87.50% |
| Off A2 | 64.29 | 95.28% |

Additional mode checks: Game setting correctly passed the game's On through
(50.18FPS/88.47%); On + Boost applied2 but gave51.04FPS/88.06%; returning to Off
gave63.15FPS/94.17%. There is no reason to choose Boost over Off for throughput
in this scene. Every sample checked actual applied mode, gameplay and FG off.
Although the user reported focus, Windows foreground PID was Firefox23752 in
all these measured windows. They are a consistent background comparison, not
a claim to have measured foreground peak performance or headset input latency.

Final state: xr_nvidia_reflex0, native applied0, FG0, FPS overlay0; profiler
restored off. No new crash report; depth snapshot capture remains active in
gameplay. Raw data: reflex-deployed-abba-20280.json and
reflex-deployed-variants-20280.json under build/performance-20260927.

NVIDIA's integration contract:
https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideReflex.md
https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_reflex.h
