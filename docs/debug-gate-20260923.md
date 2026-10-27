# Runtime diagnostic cleanup

Checkpoint before cleanup: `1fdd829e` (native framegen, performance overlay and
delayed HUD/menu follow). The commit has no Co-authored-by trailer.

`CyberpunkVR_RuntimeDiagnostics` defaults to zero and is registered with the
existing launcher DEBUG gate. It controls diagnostic counter writes and exported
snapshots, rather than only hiding the log output. `CVR_DIAGNOSTIC` avoids
evaluating its arguments while disabled and preserves surrounding if/else flow.

Coverage includes framegen input-stage counters and JSON reports, world-marker
counts, pose-ID observations, native hand publication counts, HUD/roomscale/blend
snapshots, camera/capture/render counters, XR cycle timing and cadence sampling,
queue/NGX call counts, diagnostic fine eye age, and diagnostic ImGui counter rows.
Diagnostic throttled logs also stop reading clocks and incrementing skipped-call
counts when both runtime diagnostics and verbose logging are off. Existing
explicit profiler/census switches remain available. The stale-eye diagnostic
cannot silently re-arm the RTV probe during a normal session.

The FPS overlay is a user feature: its metrics, graphs, hardware monitoring and
GPU queries remain controlled solely by the FPS overlay checkbox. Framegen core
status, hook failures and bounded error reporting remain available. Normal frame
IDs, pose/resource ledgers, reference counts, fences, sequence locks, tracking
origins, hand ownership and pacing are not diagnostics and continue to run.

Some legacy exports named `Debug*` carry operational data (for example the RTT
component pointer, cloud selection, carry reach and coarse stereo eye age).
They must not be gated by name. The fallback view-key hook previously inferred
readiness from diagnostic counts; it now uses a dedicated atomic observed flag.

Live report readers expose whether the gate is enabled. HUD/framegen motion
probes refuse to start with diagnostics disabled, before issuing simulator pose
commands, instead of interpreting frozen zero counters as healthy data. For a
focused investigation, the exported runtime gate can be enabled independently
without turning on every heavy launcher probe. Frozen exports after disarming
are not current measurements.

The specifically requested periodic channels are all gated before their clocks,
scans or formatting: `[viewparams]`, `[complend]`, `[wide]`, `[temporal]`,
`[cascrec]`, `[eyefin]` and `[rmask]`. `WideCensus` was missing from the launcher
table and is now registered. Cascade record copies that actually implement the
shadow correction remain functional; only their comparison/report is gated.

Validation: 30 HUD checks, 15 framegen checks and the production launcher-gate
table test pass (46 total), including production
HUD snapshot gating, JSON publication on/off, no evaluation of disabled diagnostic
arguments, continued framegen/product metrics with DEBUG off, and correct
WideCensus/XR probe values through DEBUG on/off. All three edited Python probe
scripts parse successfully. Release build succeeded; build log:
`build/framegen-native/debug-gate-build.log`.

Installed DLL SHA256:
`bbc2a96472c314ac8ac437930ab48eaef624077554936840d6d74d10afc88b09`.
Deployment record: `build/roomscale-plugin-deploy-20260923-221425/deployment.json`.
The previous game process was closed for DLL replacement. Settings and calibration
hashes were preserved. The user starts the next session; no live validation of
this installed build has been claimed. Cleanup is separate, uncommitted work
after the requested checkpoint.
