# Engine stereo: CPU/GPU audit, per thread (2026-10-24)

The same audit that was done for MAIN + VRCAM, for 2.5 engine stereo (`xr_engine_stereo=1`, VRCAM never
requested). Save AutoSave-8 (street, Mac'N'Cheezus), OpenXR simulator, Quest 3 profile, DLSS Balanced, the
player standing still. Tools: `tools/hitch_tests/hitch_profiler.py` + `hitch_analyze.py` (frame intervals, P99,
hitches, GameThread/worker stacks, per-thread cycles), `worker_breakdown.py`, `lua_mod_breakdown.py`,
`node_timeline.py` with `CVR_JOB_TIMELINE=1` (world-tick jobs by name), `gpu_queue_timeline.py` (GPU busy /
starved / held), `single_pass_tests/port_gpu_stages.py` (the port's own GPU passes), nvidia-smi.

## Result

| | before | TacticalBelt fixed | all fixes |
|---|---|---|---|
| frame median | 16.44 ms (60.8 FPS) | 13.53 ms (73.9) | **12.33 ms (81.1)** |
| p90 / p99 | 20.62 / 27.76 | 17.85 / 22.77 | **16.31 / 20.46** |
| frames > 28 ms in 60 s | 20 | 4 | **1** (+2 at profiler attach) |
| GPU busy (nvidia-smi) | 68 % | 84 % | **92.7 %** |
| GPU starved (queue timeline) | 4.98 ms/frame | | **0.009 ms/frame** (busy 95.9 %) |
| CET Lua share of GameThread | 31.7 % | 15.6 % | 12.5 % |

The scene went from CPU-bound (GameThread) to GPU-bound. Launch-to-launch noise is up to ~1 ms, so the last step
alone (native gates + vrcam_select) is not separated from it; the TacticalBelt step is far above the noise.

## Where the time was (before)

* **GameThread 75 % of a core, the critical path.** CET Lua 31.7 % of its samples; the world tick
  (sub_14069C788) 65 %, most of it inside its own counter wait while the GameThread helps run jobs.
* **TacticalBelt `beltParts()` walked every component of the player every frame** (281 components, a name string
  each, 2.0 ms measured in one call): the cache treated an EMPTY map as stale, and with no belt garment worn (the
  belt is not merged on 2.5, likely EquipmentEx) it never filled. 12 % of the GameThread, plus the garbage the
  Stereo GC step then had to sweep (Stereo's GC was another 6 %).
* **redDispatcher x15 ~41 % each, 64 % of samples idle**: not the limit. Port self time 2.6 % of worker samples
  (per-draw hooks ~0.66 ms/frame summed over all workers, CopyDescriptors observers 0.3 ms, world markers 0.28 ms).
* **The port's XR submit thread 15 % of a core**: 86 % waiting; its CPU is the simulator's preview blit
  (StretchDIBits), absent on a headset.
* **GPU 68 % busy, starved 5 ms/frame**: the next frame's work arrived ~8.5 ms after Present.

## The world tick now (job timeline, 9.5 ms)

0 - 6.7 ms world simulation (animation 1.6 - 6.3 ms over 16 threads, traffic, puppets, physics, quests), then
6.9 - 9.5 ms the UI system (23 virtual windows: TickLayout / Arrange / DrawElements, WidgetDraw, HUD full-screen
layers); its last job, UI/RuntimeSystemUI/PrepareRenderingState, is what the tick waits for in 159 of 163 frames.
The GameThread never waits for the renderer (scene wait 0).

## What engine stereo still ran for VRCAM

84 of the registered detours were already skipped (single-pass, timeline). Still installed and still acting:

* **Mono cascades** (`xr_mono_cascades=1`): every MAIN cascade node took the lock, waited for a VRCAM snapshot
  that never came and was cleared a second time (40 813 extra `ClearDepthStencilView`, 21 M draws counted); every
  `DiscardResource` took the same global lock (3.1 M); every resource creation got private data, every DSV
  creation/copy a map update under the lock.
* **Composition lend** (`xr_comp_lend_set=1`, frame hold 1): MAIN's UI composition object held with a count every
  frame for a consumer (the VRCAM node) that does not exist.
* **17 culling/prepare detours** on per-drawable and per-range paths: VRCAM cull reuse/replay, profilers, the
  single-pass CPU capture and preparation.
* **Single-pass CPU collector probes** (installed whenever the code matched, not when a test asked).
* **VRCAM fog history** on an SRV bind every node makes; **light block-list lend**.
* **Stereo CET `vrcam_select.lua`** walked the loading layer's controllers every frame to release a VRCAM RTT
  that is never enabled.

## Fixes

* `mods/cet/CyberpunkVRPort_TacticalBelt/init.lua` `beltParts`: an empty map is an answer; looked for again once
  a second.
* `include/Stereo/EngineStereo.hpp` `SecondViewWanted()` (= not engine stereo): the predicate of the VRCAM-only
  and single-pass detours (`CVR_DETOUR_IF`): 17 in `Stereo/CullVisibility.cpp` (all but the LOD threshold, the
  occlusion gate and the graph context, which act on MAIN and the scope camera too), the 3 composition-lend and the
  light-lend detours in `Stereo/ViewReuse.cpp`, the fog history in `Stereo/RenderParity.cpp`, both CPU collector
  probes in `Stereo/CpuCollectorProbe.cpp`.
* `Render/MonoCascades.cpp`: off in engine stereo, the descriptor/allocation tracking and the report too.
* `modules/vrcam_select.lua`: returns at once while no second view is wanted and this player's cameras are off.

**Not** under the switch, on purpose: the ViewReuse lends whose `shared_world_view` is VRCAM **or the scope
camera's `vr_optics_feed` view** (distant shadows, GI, sky, wind, clouds, view params, cascades), the render graph
cache salt (per eye and per scope view), water, RTT view creation, the graph builds (DLSS crop fix).

## Left

* The port's own GPU passes are ~0.23 - 0.28 ms/frame (eye captures 2 x 70 us, colour submits 2 x 49 us, HUD panel
  ~255 us every other frame) -- about 2 % of the GPU frame.
* The UI phase (~2.7 ms of the tick) is the GameThread's next limit in CPU-bound scenes; the port adds HUD-panel
  windows to it.
* CETBridge (the user's debug tool) polls a file on the GameThread; left as is.
* The tactical belt is not merged on 2.5 (EquipmentEx / the 2.5 wardrobe) -- separate issue.
* `[gt-queues]` (off in the ini) keeps its detour in the GameThread's spin loop: one relaxed load, live switch.
