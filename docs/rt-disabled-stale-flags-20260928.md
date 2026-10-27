# RT Off retained ray-tracing work in VRCAM

The user reported 3 FPS after switching ray tracing OFF, then confirmed that
restarting the game did not recover it. Investigated PID 27628 and the new
PID 8612, using the exact BCA3C4A0 DLL/PDB. Main-menu startup was confirmed
working by the user before this separate performance issue.

## Live Evidence

The saved game setting was RayTracing=false, DLSS Balanced, ray reconstruction
and path tracing disabled. Runtime Reflex was selected/applied 0. Broad debug,
node census, and CPU profiler were initially off and restored after each probe.

Both launches produced around 3 real FPS, zero generated FPS, and 87 repeated
XR submissions per second. Frame times were roughly 240-390 ms. FG alternated
between priming history and waiting for matching inputs; missing timely inputs
were a consequence of the slow renderer, not another motion-format rejection.

The bounded CPU node profile showed the main view mostly bypassing RT work,
while VRCAM still ran RT reflections, shadows, AO, and acceleration updates.
Reading MAIN's context between jobs often returned cleared feature words.

A PID/module/signature-checked session observer captured the actual result of
the existing FlagCompute detour, only for the identified MAIN context and key 0:

| Source | F0 | F1 |
| --- | --- | --- |
| Actual MAIN with RT Off | 3C00017FAF65FF53 | 0517F028 |
| Retained MAIN template | 3C9C3D7DAF057F53 | 0517F028 |

The old graph observer retained the template with the highest popcount. It
could remember RT-enabled/bootstrap state and never accept a later smaller
feature set, including on a fresh process. The VRCAM path fell back to that
template when the transient MAIN context words had already been cleared.

Publishing the fresh MAIN result each frame in the same process immediately
recovered 44.5 real + 44.5 generated = 89 output FPS, with 0.5 repeated FPS in
the last measurement window. GPU frame time fell to 21.16 ms; GPU utilization
rose from 23% to 87%. No game setting, resolution, RT choice, or FG choice was
changed by the fix. Values are specific to this scene and measurement window.

## Persistent Fix

Capture the producer result immediately after native FlagCompute, validating
context identity against g_main_view_ctx and requiring name key 0. Publish
the two flag words together under a small mutex. VRCAM force/build paths read
that pair, not a possibly cleared MAIN context or the historically densest graph.

Reject other unnamed views, VRCAM itself, empty results, and a cache belonging
to a different MAIN context. Before MAIN identity is available, retain only
the existing conservative bootstrap template. Existing stereo-specific flags,
upscaler selection, and the unsupported-VRS filter stay on their current paths.

Tests cover RT on -> off -> on, disabled upscaler bits, view/owner rejection,
empty results, and concurrent pair publication. The AMD/VRS and startup viewport
regressions pass too. Release build, 6 render-parity tests, 17 framegen tests,
launcher test, and LuaJIT menu/loading lifecycle test pass.

Evidence and the temporary observer are under `build/rt-toggle-20260928/`.
The observer remains active only in PID 8612 to preserve its recovered session;
it is not copied to the game's plugin directory. Later launches use the rebuilt
plugin.

## Confirmation And Deployment

The user repeated RT on/off in PID 8612 and confirmed that switching no longer
reduces FPS. A later sample was taken while the user was in a menu; its higher
menu FPS and paused-FG status are not a second gameplay performance comparison.

Installed DLL SHA-256:
`CDFA0EF000995388FF3F25FE7B0AEBEA128203CB5809EAA06C7CE559133F82EC`.
Matching PDB and the old mapped image are preserved with the deployment receipt
under `build/rt-toggle-20260928/deploy-203656/`. Runtime settings, launcher
selection, player settings, shipped preset, and calibration hashes were unchanged
by deployment. PID 8612 continues using its matched old image and session fix;
the installed DLL contains the persistent implementation for subsequent launches.
