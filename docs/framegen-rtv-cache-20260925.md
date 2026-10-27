# Framegen stalled by an evicted swapchain RTV mapping

PID29800, Basilisk TPP, native framegen enabled (NVIDIA backend), FPS panel on.
Runtime status was `Waiting for matching depth and motion vectors in both eyes`:
real/output about46.5FPS, generated0. Both depth/MV streams continued arriving.

## Measured cause

The current camera constants and both input pools carried matching native
frames46549 and pose53507. Captured VRCAM color carried pose53507 too, but the
MAIN backbuffer still carried pose2045. This was an old image label, not a
paused producer or a missing optical-flow SDK.

Read-only inspection of the three actual swapchain resources retained by the
overlay found **no RTV mappings for any of them**. The broad8192-entry RTV map
had276012 ring replacements. It evicted by descriptor creation age, assuming an
old descriptor must no longer be used. Swapchain RTVs stay live without being
re-created, so OMSetRenderTargets could no longer identify the backbuffer and
its image-pose receipt stopped advancing. Framegen correctly rejected the
inconsistent color/depth/MV combination.

Evidence: `build/framegen-tpp-20260925/inputs-before.json` and
`rtv-backbuffers-before.json`. Diagnostic gate was temporarily enabled for the
identity probe and restored; no simulator/input commands were sent.

## Fix

- Bounded8192-entry descriptor metadata cache with O(1) lookup. Access refreshes
  recency; only unused, unprotected records can be evicted.
- Register actual swapchain buffer identities at hook installation, before the
  game creates their RTVs, and after successful ResizeBuffers/ResizeBuffers1.
  Their metadata remains protected even during startup allocation bursts before
  their first draw. Registration releases GetBuffer's temporary COM references;
  the cache retains no GPU resources or additional VRAM.
- Replacing a descriptor publishes resource/width/height under one mutex. The
  old atomic-handle-only protocol did not protect readers from concurrent field
  replacement. Null/unsupported RTV replacements now erase stale mappings.
- Keep framegen's exact input/frame/pose/queue matching unchanged.

Seven tests pass: almost300k startup creations with three never-recreated
backbuffers, active HUD entries, handle replacement/null descriptors, resize,
100k concurrent replacements with four readers, all-protected capacity, and
10k image-label frames under descriptor churn. Release build passes.

Installed DLL SHA2567759840628a0f8211bb54488b2a2677f32c9c7a4cc6e71abc07c17961036eaac.
Checkpoint:build/framegen-tpp-20260925/candidate-01.
Rollback/deploy report:build/roomscale-plugin-deploy-20260925-001747.
Settings and calibration preserved. After relaunch into PID34676 the user
confirmed frame generation works. A metrics probe was interrupted before a
usable report was captured; do not claim a measured generated/output FPS from
that attempt.

## Tool-process RAM pressure found during follow-up

The user's99%-RAM observation prompted a process audit. There were27 WkDaemon
instances (WolvenKit MCP sidecars under the same Codex host), about11.13GiB summed
working sets and37.49GiB private commit.25 old sidecars older than10minutes were
stopped after verifying executable command line, parent and creation time; the
latest two were retained. No game process was stopped by this cleanup. RAM use
fell from71.3% to49.4% immediately and42.4% on the next check. Reports are saved as
tool-memory-before.json andtool-memory-cleanup.json in the evidence directory.

The later VRAM question did not establish a before/after increase. The user
clarified that they had not previously watched the game's VRAM. WDDM reported
game dedicated allocation12903903232B (~12.0GiB), shared326246400B (~0.30GiB), and
nvidia-smi reported13265MiB global usage of16303MiB. No framegen-off A/B or setting
change was performed. The RTV metadata fix itself creates no GPU resources and
holds no persistent COM references.

## Measured follow-up

The final installed build is
`189c4251d137de11c8461fb4f48ddf3f9ec92d36449ca58da90b601220c9f797`.
See `vrcam-lifetime-20260925.md` for the separate VRCAM ownership/timing fix and
the scene-FPP receipt gap found while testing camera transitions. Both changes
use the actual completed MAIN pose; no approximate pose matching was added.

PID36064 confirmed generation in FPP and TPP with complete, matching 1485-square
depth/MV inputs in both eyes. At the final stationary measurement: 44.5 real FPS,
45 generated FPS, 89.5 output FPS, 0.5 repeated FPS, 4.072 ms generation time.
During the longer motion test, output ranged 80.5–89.5 FPS with no input skips.
The camera changes create no GPU textures and retain only one native weak handle.
Tracked framegen allocations were 461,658,360 bytes (~440.3 MiB); this accounting
does not include all driver-private NVIDIA Optical Flow allocations. Total GPU
usage reported by telemetry was 13,998,780,416 bytes (~13.0 GiB), not a controlled
before/after measurement of the camera changes.

Four more old WolvenKit daemon instances were cleaned up after the same parent,
command-line and creation-time checks, retaining the newest two. Their combined
resident working sets were ~3.13 GiB. Evidence: `tool-memory-cleanup-2.json`.
