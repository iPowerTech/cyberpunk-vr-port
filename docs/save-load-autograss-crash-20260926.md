# Save replacement: VRCAM AutoGrass buffer not ready

The user reported a crash while loading another save from gameplay:
`Cyberpunk2077-20260926-002445-20652-45212`. The earlier report
`Cyberpunk2077-20260925-231225-24328-35504` has the same fault and predates the
scanner HUD changes.

## Captured native failure

Both exceptions are at EXE RVA `1F51F5` in `1F51C4`, the native resource-state
transition helper. The input resource handle is zero (saved in EBX). The
helper executes `dec edx`, turning zero into `FFFFFFFF`, and multiplies by
`B0`; R15 becomes `AFFFFFFF50`. The subsequent resource-table read faults.
RDX at the exception is the decremented index, not the original handle.

The verified native callers are `774384` and `77B638`
(`PrepareAutoSpawnOnTerrain`). The latter passes its shared terrain state's
field `+7B0` to the helper. Reconstructing its saved stack/home slots gives:

| Report PID | Work context | Terrain state | View key | Buffer+7B0 | Native count+79C |
| --- | --- | --- | --- | --- | --- |
|20652|49E9CFF220|22713F49B90|77AD6D6871650500|0|16|
|24328|7C5E0FF200|252AFFF4060|77AD6D6871650500|0|68|

The work-context field+20 points to a shared state owner, whose field+98 is
the terrain state. Both view keys match the configured
`vrcam_feed_2560x2560`; both native RT IDs are33. Adjacent handles+7A4/+7A8/
+7AC/+7B4 are live. A nonzero native work count is therefore insufficient to
establish that the additional AutoGrass buffer is usable after a save change.

The port grants RenderMask/AutoGrass to VRCAM so both eyes render terrain
scatter. The granted path reaches a native bind that assumes this buffer
already exists. This is a CPU access violation, not an out-of-VRAM report.

## Change and checks

For the VRCAM AutoGrass descriptor only, the existing per-node feature check
now verifies the current work context's terrain buffer before allowing use.
Main and other render categories retain their existing decisions. It does
not clear the persistent render mask, retain a buffer pointer, or cache a
world's readiness: the next query allows grass as soon as the new buffer is
present. Missing context/state/handle and invalid zero/all-ones handles fail
closed. Diagnostic counters use the existing debug gate.

The test reproduces the captured zero-handle state and table-index underflow,
then exercises ready/teardown/recreation, replacement-world identity, invalid
sentinels and missing reads. It passes, as does the Release build. Candidate
DLL SHA256 `81976c1c25496aa53c34c2a29a89b94c5ce9b5b77ef3c1ca3d4510d06de23c9d`.
Live repeated-save verification is pending. Artifacts are in
`build/save-load-crash-20260926/`.

## Second-load stall and VRCAM lifecycle

Candidate81976 avoided the access violation, but the user's second load stopped
near70 percent. PID19876 remained responsive to CET and kept rendering the
loading screen; thread snapshots and an x64dbg attachment did not show a
stopped process. The live screen was `inkFastTravelLoadingControllerSupervisor`.
The readiness probe had allowed6,956 checks and rejected12 missing-buffer
checks before the loading transition stopped advancing.

A bounded test used the existing VRCAM bridge to disable the second camera for
2.5 seconds and restore the original request. The loading screen subsequently
closed and native menu mode returned to0 without restarting the game. The
debugger was detached and the bridge's original request was restored.

The selector previously kept `prevPlayer` and considered the selection applied
while name/wanted-state/EntityID matched. Its load reset depended on GetPlayer
becoming nil. That is insufficient for a replacement that already exposes a
player or reuses EntityID1. It also leaves a reference to the old player until
a later apply actually happens.

`vrcam_select.lua` now checks the native `inkLoadingLayer` on the script/UI
thread. A visible Initial/FastTravel loading supervisor suspends VRCAM, releases
the cached player's enabled RTT components and drops the old player/applied
identity. Newly restored player components are kept disabled during loading.
When the layer releases the view, selection is reapplied to the current player.
The requested on/off state and selected resolution are retained. Ordinary ESC
menus do not trigger this lifecycle transition. A no-player transition also
releases the previous player and resets the retry delay.

The production-module Lua fixture passes initial load, same-EntityID second
load, no-player transitions, transient layer errors, user-disabled preference,
fresh CET wrappers, and braindance/replacer handoff. It is lexical and uses no
live widgets. The installed Lua SHA256 is
`f68b23f1d23883bef4eeba978b761bd9877eff3d72270a4dbebbf69c3260baeb`.
Backup: `build/save-load-crash-20260926/vrcam_select.before.lua`.

The same component/cache lifecycle guard was hot-applied to PID19876, retaining
the original tick's sandbox I/O. The installed module publishes an empty
active-name diagnostic while loading; the live wrapper keeps the previous
name, which also retains the native selector key while the components are off.

The user confirmed three further save loads completed. The187.7-second trace
`live-lifecycle.json` records three new VRCAM component identities, return to
gameplay after each transition,26,296 additional Ready checks and zero new
missing-buffer checks. No newer crash report appeared. Diagnostics were
restored after collection. The correction is deployed.
