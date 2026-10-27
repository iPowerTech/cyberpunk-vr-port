# TE6 render parity port — 26 September 2026

Selective port of the mechanisms documented in
`te6-testing2-render-review-20260926.md`, on top of `0cc06d12` and the pending
story-attention/cyberware changes. No third-party DLL was installed.

## Changes

- `GraphCacheLookup` gets the recovered FNV salt for the prepared VRCAM view.
  The native five-argument contract, including the cache-entry count and created
  output, is preserved. Apply only at the verified caller in the same native
  frame as the prepared single view. Other views keep their original key.
- MAIN's AA mode is temporarily installed before native FlagCompute and restored
  afterwards. The observed MAIN camera **context** is the identity, not the
  unrelated viewData argument. The native value of feature bit 33 survives
  forcing the other MAIN feature flags onto VRCAM.
- The old `DealiasPostColor` physical-index override is removed, as in highlight.
  Compatibility exports remain at zero. Stable color capture is unchanged.
- Fog history input is synchronized only at native return RVA `61D0C0` inside
  the `61C3BC` VolumetricFog node, as in testing2. Owner, render frame, registry,
  tracked-pose validity, origin, resolution and live allocation checks are added.
  The input may be from MAIN's immediately preceding frame, because VRCAM runs
  first. No handle is accepted across a skipped frame or a >100 ms gap.
- Existing `LocalShadowReuse=0` remains in effect. sRGB conversion and unrelated
  TE6 changes were not part of this port.

## First live test and corrections

Candidate `93db45cb...` was installed and sampled in PID19044. All three enable
flags were 1; the graph-cache trampoline was installed. In 6.4 seconds the
cache salt ran 266 times. AA recorded 266 misses and fog recorded no candidates.
This was not reported as a successful functional test.

Two adaptation errors were identified and corrected:

1. `g_main_view_obj` is not the `viewData` object passed to FlagCompute. The
   live MAIN context equals `g_main_view_ctx`; that is now used instead.
2. The fog declaration token `0x49471DDA` is remapped through native `1F02E4` /
   `7674AC`. Its low 24 bits are not preserved. Exact node+caller filtering is
   retained; the incorrect extra key filter is removed.

## x64dbg evidence

The user opened x64dbg and authorized its use. The available MCP tools were
accessed directly through its configured loopback endpoint
`http://127.0.0.1:3000/mcp`. A bounded hardware breakpoint trace at `61D0C0`
captured both eyes for six consecutive native frames. It took less than a
second; the breakpoint was deleted, the game resumed and the debugger detached.

| Native frame | First call: VRCAM | Second call: MAIN |
|---:|---:|---:|
| 42067 | 20141 | 31851 |
| 42068 | 31384 | 20141 |
| 42069 | 31851 | 31384 |
| 42070 | 20141 | 31851 |
| 42071 | 31384 | 20141 |
| 42072 | 31851 | 31384 |

These are three persistent GPU buffers rotating through the input. Each handle
retained the same D3D resource, engine wrapper and SRV identity across the trace.
MAIN and VRCAM used the same registry; their actual logical keys were
`B0322214` and `B0325214`. Thus the recovered previous-MAIN history behavior is
intentional for this ordering; a same-frame-only gate would never activate.

Native SRV binding (`1FABD0`) and barrier tracking (`1F40DC`) identify the buffer
pool at EXE global `3438A28`, stride `B0`. With `index=handle-1`, compare:

- actual D3D resource: `pool + 192984 + index*176`;
- engine wrapper: `pool + 193048 + index*176`;
- SRV: `pool + 193032 + index*176`.

The port remembers identities, never calls COM on a remembered pointer. The
allocation is read again before substituting the handle. Camera/player/renderer,
origin or resolution changes reject the history; frame-clock reversal, a missed
frame and long gaps also reject it. Same-frame pairs additionally require the
same pose ID; different pose IDs are normal for a previous-frame history source.

## Controls and verification

Default-on exports: `CyberpunkVR_RenderCachePerEye`, `CyberpunkVR_EarlyAaMode`,
`CyberpunkVR_FogHistorySync`. They allow separate A/B tests; individual visual
causality has not been claimed merely from the DLL diff.

All new counters and snapshots are behind `CyberpunkVR_RuntimeDiagnostics`.
The bounded probe restored its original value (0). Counter layout:

1. Cache salts.
2. MAIN AA samples.
3. Temporary AA swaps.
4. AA sample misses.
5. MAIN fog samples.
6. Fog substitutions.
7. Fog frame/owner/pose/registry mismatch.
8. Invalid fog context or pose.
9. Invalid/recycled fog allocation.
10. Already matching fog handle.

Release build and `tools/render_parity_tests` passed, including cache-key
partitioning, preservation of all other feature bits, MAIN-context selection,
AA observation lifetime, fog frame/owner/resize/recenter guards and allocation
identity changes. The corrected candidate was installed after the first trace:

`A4E83CDBFAFA9372CD658B590DE8790952DFE0FCF4D92F6ECEE7DC6DF1759DBA`.

Backup: `build/render-parity-20260926/deploy-candidate2-115518/`.
Live verification of this corrected candidate is pending the user's next launch.
Artifacts and diagnostic helpers: `build/render-parity-20260926/`.

## GPU hang on the corrected candidate

The next launch (PID15720) produced report
`Cyberpunk2077-20260926-115722-15720-20476`. This is a GPU hang:
`DXGI_ERROR_DEVICE_HUNG` / `0x887A0006`, followed by the engine's assert on
thread20476. The last unfinished breadcrumb is `[3518:33] Lighting`, with
`COMMANDLIST_SCOPE` in progress and `FinalFlushBarriers` not reached. DRED has
no page-fault address; its breadcrumb interfaces returned `0x887A0004`.
The CPU stack records the GPU-error handler and cannot identify a shader fault.
The dump does not contain the plugin's data pages, so it cannot establish which
of the two newly working paths had run. A stale/freed pointer is NOT established.

The three-buffer trace establishes persistent allocation identities, **not**
safe GPU contents/access dependencies across frames. Identity checks alone do
not make cross-view borrowing safe. The fog binding substitution is isolated
off until its source/output dependencies have been checked in a stable run.

Isolation DLL: `F3C11A86F85A08E9283ACB53AA16B0CB7BC12E5F4DD5AE98213753AD3856E6B4`.
It keeps the cache/AA changes enabled and defaults `FogHistorySync=0`.
Backup of the faulting DLL/PDB:
`build/render-parity-20260926/deploy-fog-isolation-120738/`.

A separate default-off `CyberpunkVR_RenderParityDebugCapture` enables only these
diagnostics during a bounded probe, without enabling the whole project's heavy
runtime diagnostics. With fog disabled the probe only observes; no binding is
modified. The native fog output handle is read from
`*(workContext+0x18) -> *(view+0x1D48) -> state+0x18` and recorded alongside both
inputs, to detect read/write aliasing. The three debug rows are AA, MAIN fog,
VRCAM fog; the last fog field packs validity in the high32 bits and output handle
in the low32 bits. Waiting for the user's isolation launch.

## Isolation result and initialization guard

The fog-disabled isolation DLL also failed on its first launch:
`Cyberpunk2077-20260926-120813-21032-21196`. It is the same GPU hang pattern,
`[3408:33] Lighting`, no DRED fault VA. That rules out the fog substitution as
the necessary cause of this startup failure. The user immediately launched the
same binary again; PID25596 successfully loaded gameplay.

On that successful run, a 6.4-second bounded probe recorded 282 cache salts,
281 MAIN AA observations, 282 temporary AA swaps and **zero AA misses**. VRCAM
had mode1, MAIN mode0. The context-identity correction works. Fog was off, but
the observation path captured 281 MAIN samples with no invalid allocation.

A separate 20-second in-game trial enabled fog, then restored it to0. It made
876 substitutions over878 MAIN samples, with two pair/age rejects and no invalid
allocation or GPU crash. Both diagnostic gates were restored to0. This confirms
execution of the recovered mechanism in this scene; it is not a universal
visual-quality or stability proof. Evidence: `live-probe-fog-trial.json`.

Next candidate limits cache partitioning and bit33 preservation to initialized
gameplay with a fresh, valid MAIN AA observation. Provisional startup/menu
graphs keep their previous behavior. Missing AA metadata no longer preserves a
bit computed under the wrong AA mode. If the temporary AA write fails, the
feature-bit change is rejected too. Fog additionally rejects a borrowed input
whose actual D3D resource aliases the current native fog UAV output.

The new cache hook also uses a register-preserving MASM pre-call shim. Only
the hash in R8 changes; all other volatile GPRs, XMM0–5, flags, original return
address and fifth native argument are preserved. It tail-jumps into the native
trampoline, so all native outputs reach the caller unchanged. The ABI test
passes both unchanged-hash and modified-hash modes with an intentionally
clobbering callback. This guards the engine's internal calling convention;
the GPU log does not prove that register clobbering caused the earlier hang.

Installed startup-guard candidate:
`99C6B5D62BE8B514CB6EB187182E0BC5476EAF3103AA325A322908C42CF644B4`.
Backup: `build/render-parity-20260926/deploy-startup-guard-123030/`.
All three features default on again. A bounded startup recorder is waiting for
the next user launch, enabling only the parity-specific debug gate and restoring
it afterwards. Validation of this final candidate is still pending.

## Successful run and comparison snapshot

The user confirmed a successful launch and save load on candidate99C6, PID8428.
The 55-second startup recording (`startup-8428.jsonl`) shows no cache salts or AA
swaps during provisional startup; they start after the gameplay state becomes
usable. A subsequent 6.4-second gameplay probe recorded275 cache salts,
275 AA swaps and275 fog substitutions, with zero misses, invalid allocations
or context rejects during that interval. Both debug gates returned to0.
No new crash report was present after this run. One successful startup does not
prove an intermittent startup failure permanently fixed.

The user reported **no noticeable visual improvement**. Visual efficacy remains
unconfirmed. A deeper literal-level comparison was started:323 equal-structure
TE5/highlight function pairs have differing normalized instructions, with IR
saved in `build/te6-binary-review-20260926/*-literal-ir.json`. These still need
classification into TLS/relocation differences versus semantic constants.

The user then requested a dist so they could cleanly uninstall our mod and
compare the author's TE6 build. Produced and checksum-verified:

- `dist/CyberpunkVRPort-dev-20260926-current.zip`:265 entries, DLL99C6, current
  repo assets/scripts, native FG, story fixes, cyberware chord and render port.
- `dist/CyberpunkVRPort-settings-20260926.zip`:61 saved configuration files plus
  manifest, instructions and a restore helper. Includes the user's game settings
  and VRIK calibration, kept separate from the distributable.

Archives matched the installed assets; every payload entry in both ZIPs was
verified against its manifest. The game was not uninstalled or replaced with
TE6 by the agent. No commit was made. Await the user's comparison result before
claiming that the port reproduces the author's visual fix.

## Return from the user's TE6 comparison

The user reported that the same unresolved rendering issue also exists in the
author's build and requested its removal and restoration of ours, retaining fog.
The installed DLL matched TE6-testing2 (`bc3a561f...`); the game was closed.

Restored the checksum-verified99C6 distribution and all61 personal configuration
files. Dedicated VR mod folders were moved out of the game before deployment,
so old scripts could not survive an overlay copy. Removed the TE6-only LootUi,
NPC-ray native declaration and HUDitor overrides; the HUDitor folder contained
only the exact TE6 persistency file, no third-party code. All265 final payload /
personal game files were verified, including the original game settings and
VRIK calibration. No TE6-only payload files remain. Fog remains enabled in99C6.

Recoverable removed build and deployment report:
`build/restore-ours-20260926-130445/result.json`.
No game launch or source-code rollback was performed during restoration.
Remaining highlight/render discrepancy must be investigated independently;
the user's comparison does not establish that the recovered TE6 changes solve it.

## Subsequent highlight fix

The independent RenderDoc/x64dbg investigation identified sky-modulated local
lights receiving radiance 1000 times lower in VRCAM. The early sky-radiance
synchronization, user confirmation and post-install measurements are documented
in [sky-modulated-lights-20260926.md](sky-modulated-lights-20260926.md).
