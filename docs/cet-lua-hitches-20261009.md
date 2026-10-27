# Periodic hitches, the CET Lua cost and the engine workers (2026-10-09)

Request: find the cause of the stutter in the dense city (DLSS Balanced, 2x2560^2) and take load off
the CPU. No quality, resolution or render-node changes. Same city spot and save for every number below.

## What the frame looked like

- 58-61 FPS at 90 Hz, GPU 85-93% busy, 1% low 23-29 FPS. The lows were the visible problem.
- GameThread never idles: a third of it ran CET's Lua (`onUpdate` of ~20 mods, ~5.4 ms a frame), the
  rest spun in `sub_14014AB30` -- redJobs2 `FlushCounter`, the main thread waiting for the tick's job
  graph. Adding 2.5 ms of Lua cost 0.7 ms of frame and 4.5 ms cost 1.9 ms: the engine overlaps most
  of the Lua.

## Cause of the hitches: LuaJIT garbage collection

Long frames (30-56 ms) came every 0.8-0.9 s, with GameThread in CET and the workers idle. Per-frame
`collectgarbage("count")` inside CET put every one of them on a heap drop, and LuaJIT's `GCState`, read
from the game's memory (`global_State` = `L->glref`, the main `lua_State` found from the interpreter's
DISPATCH register), showed the mechanism:

- the mods produced ~174 KB of garbage per frame;
- the finalize phase lowers LuaJIT's live-heap estimate by a fixed cost per finalized object, and CET
  makes a finalizable userdata for every game object a mod touches, so the next threshold (2 x the
  estimate) landed below the heap;
- the next cycle therefore started at once with a 1.5-2.6 MB debt, and LuaJIT paid it with a step on
  every allocation: propagate, atomic and a 2-5 MB sweep inside ONE frame, 13-15 ms of GameThread.

## Fixes (mods/cet)

| Change | Why |
|---|---|
| HandCollision: with `COLL_ON = false` only check that the weapon answers (first slot) instead of reading all 58 slot transforms; debug notes formatted only while DEBUG is on; one cached `Vector4` for the hand release; closure-free `pcall`; the empty body-capsule scan retried once a second | the 58-slot read fed only the debug note |
| Reload: the same 58-slot read reduced to the first answering slot (nothing in the mod reads the arrays); cached slot CNames; closure-free `pcall` | the same read, a second time every frame |
| reload.lua `slotModel`: cached CNames, closure-free `pcall` (no new upvalues in `M.frame`) | called several times a frame |
| Weapon: the "left slot is clean" safety check (pulls the player's whole ItemAttachmentSlots array through CET twice) runs every frame only while this mod has displaced the slot, otherwise every 2 s; slot TweakDBIDs cached; the weapon id read once a frame instead of twice; `IsMelee` asked once per weapon; the aim-dot raycast without a closure and with a cached CName | per-frame RTTI marshalling |
| TacticalBelt: belt-piece names built once; closure-free `IsEnabled`/`Toggle`/entity-hash/chunk-mask/palm reads; hips slot CName cached | per-frame closures and strings |
| Stereo hud_panel: CNames made once, no closure per frame | per-frame allocations |
| QuickBoot: the launch-readiness report (loading-layer walk every frame, JSON status file twice a second) stops at `world_ready` unless the launch is tool-owned (`requireNativeReady`) | a CET `io.open` costs 0.3 ms (existing file) to 1.15 ms (missing file) on the game thread |
| Stereo: CET's collector run from here (below) | the hitch itself |

### The collector, run from the Stereo mod

LuaJIT's own trigger is switched off (`collectgarbage("stop")`, set again after every step because a
step re-arms it) and every frame takes a 64 KB step (more after a burst of allocation, up to 1 MB), so
a cycle is always spread over frames and no debt builds (max debt seen: 87-243 KB). `onUpdate` runs in
menus and loading screens as well, and a mod reload builds a fresh Lua state.

Resting between cycles was measured live (`Stereo.gc.restGrowth`, tunable through
`GetMod('CyberpunkVRPort_Stereo').gc`): the atomic phase is not incremental and walks every userdata made
since the last cycle, so it grows with the rest -- rest 0 -> ~3.3 ms, 1.05 -> 4.4, 1.2 -> 7.9, 1.35 ->
11.5, a 200-frame rest -> 17 ms (a 40 ms frame every 5 s). Frame rate was equal within noise; rest 0 had
the flattest frame times and is the default.

## Result

| | before | after |
|---|---|---|
| garbage per frame | 174 KB | 72 KB after the reload/collision fixes (TacticalBelt/Weapon fixes on top, not re-measured) |
| hitch frames per 60 s (exact present intervals, under the profiler) | 71 (2.1%) | 2 (one is the profiler starting) |
| p99 frame | 36.6 ms | 23 ms |
| largest single heap release | 4.9 MB | ~1 MB |
| Lua on GameThread | ~32% (~5.4 ms a frame) | ~19% (~3.1 ms) |
| FPS / 1% low (no profiler) | 58-61 / 23-29 | 62.8 / 44.7 with the per-frame step; 65-66 FPS in the final GC sweep (lows not re-measured) |
| GPU busy | 85-93% | 94-95% |

This spot is now GPU-bound. TacticalBelt went 3.56% -> ~2.9% of GameThread and Weapon 2.80% -> ~1.5%; what
is left in both is their real per-frame work (muzzle pose, aim raycast, belt state).

## The engine workers

- 15 redDispatcher threads: 61% of their time blocked, 99% of that in the job dispatcher's idle loop
  (`exe+143060`) -- no work to take, not waiting on GPU fences or locks. Engine code 25%, driver/D3D12 7%,
  the port's own code 2.8%.
- Concurrency across the frame is even (5-9 busy workers, 6.9 on average; no single-thread tail), so the
  workers do not hold the frame here.
- Render-graph recording (port profiler, `node_cpu_audit.py`): MAIN 10.4 ms and VRCAM 9.1 ms of CPU a
  frame across the workers; `RenderElements` 4.3 + 3.7 ms (~30 jobs per view), `SceneDrv` 1.4 + 1.2 ms.
  The second view's ~9 ms is the price of a separate view; only single-pass removes it (ceiling measured
  in single-pass-ceiling-20261008.md).
- The port on the workers: ~30 functions, none above ~0.5 ms of CPU a frame summed over all workers;
  `ObserveBindings` is a cached environment check called per draw, `ObserveCreatedCbv` is the stereo
  constant machinery, not diagnostics. Lock waits in the port's hooks (cascades, pose ledger) ~0.4% of
  worker time. No single target.

## Tools (tools/hitch_tests, kept on disk)

- `hitch_profiler.py` + `hitch_analyze.py` -- exact frame intervals from `CyberpunkVR_PresentTiming`,
  GameThread stacks with the decoded Lua call chain, redDispatcher stacks, per-thread cycles; long frames
  compared with normal ones, by mod and Lua function. `lua_mod_breakdown.py` drills into one mod.
- `worker_breakdown.py`, `worker_blocked.py`, `concurrency_profile.py` -- where worker time goes, what
  they block on, busy workers by frame phase.
- `luajit_reader.py` -- read-only LuaJIT 2.1 GC64 frame decoding in another process (CET's sandbox has
  no `debug`/`jit`).
- `gc_state_probe.py` -- LuaJIT `GCState` sampled at ~1 kHz with the present counter; `gc_tune.py` sweeps
  `Stereo.gc.restGrowth` live with it.
- `cet_gc_trace.py` -- per-frame Lua heap and frame time recorded inside CET through CETBridge;
  `cet_burn_ab.py` -- adds a fixed Lua busy-wait to measure how much Lua time reaches the frame.
- `luacheck.py` -- compiles Lua files with LuaJIT 2.1 (lupa). `reload.lua` reports more than 60
  upvalues in `M.frame` there; CET's build accepts it, so compare against that baseline.

Every test launch went through `launch_to_save.py`; CETBridge (the MCP bridge used for these tests) is a
development tool and was left unchanged.
