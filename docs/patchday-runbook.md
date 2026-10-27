# Game update day: moving the port to a new Cyberpunk 2077 build (prepared 2026-10-21, on 2.31)

The port is made for exactly one `Cyberpunk2077.exe` (2.31, SHA-256 `a7de8294...`, linker timestamp
`68AF45EA`). This is what a patch breaks, what already protects the player, and the commands that carry the
port over. Tooling: `tools/patchday/` (Python 3.13, `py -3.13 -B ...`). Old-build snapshot:
`build/patchday/base-2.31/` (0.8 GB: exe, address library, shader caches, TweakDB, script bundle,
frameworks, the 844 base files our archive replaces, the archive TOC).

## What a patch breaks

| what | how many (2.31) | how it is carried over |
|---|---|---|
| game RVAs in src/ include/ (detours, patch sites, callers, vtables, globals) | 1090 distinct, 2863 occurrences | `remap.py`: CDPR's address library (`bin/x64/cyberpunk2077_addresses.json`, hash -> address, the same thing RED4ext resolves) + masked code compare |
| code guards: `{rva,len,FNV}`, `Site{rva,"hex"}`, `{rva,{bytes}}`, named byte arrays | 512 + 34 + 11 + 26 | re-derived by `remap.py` where the masked code is unchanged; left failing (feature off) where it changed |
| `IDA sub_14...` names in comments and log strings | 401 | rewritten with the address (cosmetic) |
| struct field offsets into engine objects (`registry+0x13BC4D0`, `+0x2F1D8`, collector `+0xC4DF0`, targeting `+0x31100` ...) | 66 field values of the 92 "not_rva" (the rest: masks, sizes, hashes, CET) | `offsets.py`: 324 carrier instructions in the game's own code (`mov rax,[rcx+0x13BC4D0]`) for 61 of them; in the new build each carrier's function is re-found by the library, and a changed one gives the new offset through the instruction window; bounds/neighbours without a carrier follow the field below (4); one value (`0x274248`) has no carrier at all |
| CET internals (`Render/NativeTargetLifetime.hpp`, CET 1.37.1) | 3 functions, 1 global, 2 fields, PE identity | `cet_remap.py` (masked-code search in the new `.asi`; a function matching several places is taken as the callee of a re-found one when its bytes match there) |
| RED4ext.SDK class layouts the port reads engine objects through | 117 classes, 516 fields (442 of them in RTTI) | `src/Core/SdkLayouts.cpp` checks them against the game's RTTI at every start (`[sdk-layout]` in the port log, before the build gate); `sdk_layouts.py diff` / `build` for a new SDK |
| redscript wrappers on game classes | `mods/redscript/*` | `check_reds.py`: the real compiler (scc) offline against the new bundle |
| TweakDB records the port names | 35 (+10 absent already on 2.31, baseline) | `check_tweaks.py` (record table of `tweakdb*.bin`) |
| game settings: CET `GetVar` names (an unknown name is an engine assert = crash) and the shipped `mods/config/UserSettings.json` (version 140, 319 values, copied over the player's on the first VR launch) | 2 + 319 | `check_settings.py` (the game's `r6/config/settings/*options.json`) |
| base files our archive REPLACES | 844 of 1203 (463 .effect, 153 .particle, 100 .app, 41 .animgraph, 28 .ent, 19 .rig, 16 .anims, 15 .inkwidget ...) | `archives.py diff` (RDAR SHA-1 per file); 2.31 originals kept for a three-way merge |
| shader twins (keyed by FNV of the original bytecode) | 161 in 10 folders | `check_shaders.py check` (DXBC digests of the new caches) -> regenerate a folder with its generator |
| game-shipped SDK DLLs (Streamline, DLSS, FSR, XeSS) | via exports; versioned structs | `patchday.py check` lists the replaced DLLs |
| frameworks (RED4ext, CET, redscript, ArchiveXL, TweakXL, Codeware, Input Loader) | tested set: `data/frameworks-2.31.json` | `frameworks.py` (stage the releases, script API diff, the port's redscript compiled with them, CET checks), `check_deps.py` (installed vs newest) -- see *Frameworks* below |

## What protects the player in the meantime

`include/Core/GameBuild.hpp` + `src/Core/GameBuild.cpp`: the DLL compares the running exe's PE timestamp and
SizeOfImage with the build it was made for. RED4ext loads the plugin on any build (it must declare itself
runtime-independent), so before this a patched game would have been detoured at 2.31's addresses -- a crash on
boot. Now, on another build, `Main` registers the natives (the scripts that declare them must still load) and
installs nothing: no device hook, no detour, no patch; the log says `[build] ... NOT the same build -- VR off`
and a message box explains it on the desktop. The detour and hook registries refuse on their own too, and
skip every address listed in `kUnmapped` (what `remap.py` could not carry over). `CVR_ALLOW_UNKNOWN_BUILD=1`
(environment, development only) overrides the gate.

So Steam may update the game whenever it wants: nothing crashes, VR is simply off until the port is moved.

Verified live on 2.31 (2026-10-21): the normal launch logs `[build] game 2.31 / 68AF45EA matches the port's
build.` with the same 36 hooks + 73 detours as before; with `CVR_PRETEND_UNKNOWN_BUILD=1` (environment: this
build is treated as a stranger -- the rehearsal of a patched game) the port's log is two lines, the message box
sits over the main menu, the save loads flat, 90 s in the world without a crash, redscript compiled, no new CET
error.

## Rehearsals (any time, on the working build)

    py -3.13 -B tools/patchday/selftest.py               # remap identity + simulated move on a scratch tree,
                                                         # every field-offset carrier read back through its window
    py -3.13 -B tools/patchday/patchday.py check --force # every patch-day check against the same build
    py -3.13 -B tools/patchday/patchday.py apply --version 2.31 --force --no-build   # must leave src/ include/ byte-identical
    py -3.13 -B tools/patchday/patchday.py verify        # deploy, simulator launch, logs vs baseline, close (VERIFY OK)
    py -3.13 -B tools/patchday/patchday.py frameworks    # newest framework releases against the installed ones
    set CVR_PRETEND_UNKNOWN_BUILD=1 (for the launch)     # what a player on a patched game gets

All of these passed on 2026-10-21.

On a REAL pair of builds (needs an older build's files; `build/patchday/tools/fetch-2.3.cmd` downloads the
2.3 exe, address library, scripts, TweakDB, shader caches and settings schema -- ~0.4 GB through
DepotDownloader with a Steam QR login -- into `build/patchday/base-2.3`):

    py -3.13 -B tools/patchday/realpair_test.py [--other build/patchday/base-2.3]

remap 2.31 -> 2.3 on a scratch tree, inventory of it against the 2.3 exe, remap back: the tree must come back
byte-identical except where the forward pass left an address unmapped; then the other checks on 2.3.
(Identity pair 2.31 <-> 2.31: 831 of 831 files identical.)

`remap --apply` and `offsets --apply` also carry `data/literal_overrides.json` (the hand decisions are keyed
by value), so the next inventory still recognises the forced addresses and the moved fields.

## Before the update (done 2026-10-21; repeat after any change that adds addresses)

    py -3.13 -B tools/patchday/patchday.py prepare [--save-log-baseline]

Snapshot (if missing), inventory of every address (`data/rva_db-68af45ea.json`), archive TOC + overrides +
originals, shader index, TweakDB baseline, redscript compile, self-tests (`selftest.py`: identity remap and a
simulated +0x10 move applied to a scratch copy of the tree -- every address and guard rewritten, nothing else).
`--save-log-baseline` only right after a good VR launch (it records which hooks/detours were ok).

The inventory needs only the OLD exe, which the snapshot keeps -- it can be re-run after the update too.

## Noticing it

    py -3.13 -B tools/patchday/watch.py [--loop 30]

Steam's `appmanifest_1091500.acf` (buildid, a queued/downloading update) against the snapshot's
`manifest.json` (2.31 = Steam build 20383525, depot manifests 1091501 `3389237510439361019`, 1091503
`2051822335481947109`, 2060310 `2810178366748735393` -- also what `download_depot` needs to fetch 2.31 again),
the exe against `GameBuild.hpp`, and the newest release of every framework against the last look; `--loop`
shows a message box on any change. Read-only.

## Patch day

1. **Let Steam finish, do not start the game yet** (it would start flat anyway).
2. **Check** (offline, ~5 min):

       py -3.13 -B tools/patchday/patchday.py check

   -> `build/patchday/check-<new timestamp>/REPORT.md`: address remap summary (+ `remap/report.md` with
   every address that needs a human; remap exits 2 when there is one), redscript errors (port only, and with
   all installed mods), wrapped/replaced script methods whose target changed (`reds_diff.py`), TweakDB misses,
   settings names, orphaned shader twins, replaced base files the patch changed, framework versions, CET,
   changed game DLLs.
3. **Frameworks** (see *Frameworks* below): when their releases for the new build are out,

       py -3.13 -B tools/patchday/patchday.py frameworks [--sdk-build]

   then install the staged zips through Vortex. Re-run `check` if any came after step 2. If the SDK changed
   what the port uses (`[sdk-layout]` mismatches, `sdk_layouts.py diff`): move `externals/RED4ext.SDK` to the
   new revision, `sdk_layouts.py gen`, rebuild.
4. **Apply** (rewrites the tree; commit or stash first so the diff is reviewable):

       py -3.13 -B tools/patchday/patchday.py apply --version 2.xx

   Snapshot of the new build -> `build/patchday/base-2.xx/`, `remap.py --apply` (addresses, guards,
   `GameBuild.hpp` incl. `kUnmapped`), `offsets.py check --apply` (field offsets whose carriers agree),
   `cet_remap.py --apply` (if CET changed), new inventory + carrier index for the patch after this one, DLL
   build. `git diff --stat` should show only hex digits changed in src/ include/.
5. **Review** `build/patchday/remap-<ts>/report.md`:
   - *Needs a human* (lost / ambiguous / changed): open the new exe in IDA (ida-headless `open_database`,
     `analyze`), find the function from the old one's decompile or callers, write the new value, and delete it
     from `kUnmapped` in `GameBuild.hpp`.
   - *Rewritten, review*: `changed-func` (the library names the function, its body changed -- read what the
     detour reads), `data-delta`, `sig-global` / `sig-near`.
   - *Guards over changed code*: the feature behind each stays off; re-verify, then refresh the guard from the
     new bytes (FNV of the range; for a Site/array the new bytes).
   - *Field offsets* (`offsets.py` in REPORT.md): `conflict` (carriers disagree) or a lost carrier -- read the
     carrier functions in the new exe; `moved-1vote` -- a single carrier said so, confirm it.
6. **CET**: done by `apply` when every part was found; if it printed LOST/changed, re-find those by hand (IDA on
   the new `.asi`) -- `cet_remap.py` refuses unless all three functions, the global and both fields matched.
7. **Scripts / tweaks**: fix what `check_reds.py` (does it compile) and `reds_diff.py` (which of the 437
   annotations' (412 redscript + 25 CET Observe/Override) targets are GONE, changed SIGNATURE, or changed BODY -- a wrapper of a changed body still
   compiles and may no longer do its job) and `check_tweaks.py` report. Both bundles are decompiled by
   `build/patchday/tools/redscript-cli-0.5.31/redscript-cli.exe` into `build/patchday/decompiled/<sha>/`
   (2.31 also in `base-2.31/scripts_decompiled/` and `Desktop/cyberpunk/cyberpunk`); diff a changed class there.
8. **Shaders**: for each folder `check_shaders.py check` calls orphaned: extract the new cache
   (`tools/asym_tests/extract_shader_cache.py --cache <game>/engine/shader_final.cache --out <dir> --kind ..`),
   run that folder's generator (named in the output), copy the twins to the plugin folder, then
   `check_shaders.py index --game build/patchday/base-2.xx --index tools/patchday/data/shader_twins-2.xx.json`.
9. **Archive**: for each `CHANGED` file of `archives.py diff`: old base = `base-2.31/archive_originals/<path>`,
   new base = `cp77tools unbundle <archive> --hash <hash>`, ours = `mods/archive/source/...` or the script that
   made it (make_case_fx.py, assemble.py, the per-gun pipelines). Re-make ours from the new base (the scripts
   start from base files), rebuild the archive, `deploy.py archive`.
10. **Deploy and launch** (game closed; the harness kills it first), one command:

        py -3.13 -B tools/patchday/patchday.py verify [--reds <changed mods>] [--archive] [--keep]

    = `deploy.py dll` (+ reds / archive), `launch.py` (simulator, QuickBoot save), `check_logs.py` against the
    baseline, `close.py` (player files byte-identical, ActiveRuntime back); `--keep` leaves the game running for
    manual tests. `check_logs.py` prints the `[build]` verdict, every hook and detour the 2.31 launch had ok and this one
    lost, guard refusals, loader/redscript/TweakXL/ArchiveXL/CET errors (CET's mod logs only for this launch;
    the 2.31 launch had 36 hooks + 73 detours ok, `data/log_baseline-2.31.json`). Then the usual simulator
    tests (`tools/single_pass_tests/run_launch_tests.py` = 9 checks; reload cycles; optics) and a headset round.
11. **Re-baseline** after a good launch: `check_logs.py --save-baseline --baseline
    tools/patchday/data/log_baseline-2.xx.json`, `patchday.py prepare` on the new build, commit, release
    (0.1.8 recipe in memory `cpvr-build-deploy`).

## Frameworks

Each gets a release for nearly every game patch, mostly the same day (2.3: CET 1.36.0 + RED4ext 1.28.0 on
2025-07-17; 2.31: CET 1.37.0 + RED4ext 1.29.0 on 2025-09-11; psiberx's three within days). Until they are
out the game may not even start with them; the port's own gate does not depend on them.

| framework (tested set, `data/frameworks-2.31.json`) | what the port takes from it | what checks a new release |
|---|---|---|
| RED4ext v1.30.0 | the plugin loader (`Main.cpp`: API v1 COMPAT_0, SDK 1.0.0, runtime-independent) and the SDK headers (117 classes) | the loader log at launch (`check_logs.py`: loaded / incompatible), `[sdk-layout]`, `sdk_layouts.py diff` / `build` |
| CET v1.37.1 | 22 Lua mods (180 CET API names the `.asi` carries), its import table (`OsInput.cpp` patches `GetClientRect`, `GetCursorPos`, `SetCursorPos`), its internals (`NativeTargetLifetime.hpp`) | `frameworks.py check`: names, imports, `cet_remap.py` dry run; `cet_remap.py --apply` |
| redscript v0.5.31 | the compiler of `mods/redscript` (34 files) | the port compiled with the staged `scc` |
| Codeware 1.20.3 | script API in redscript and Lua (`Entity.GetComponents`, `inkSystem.GetLayers`, `GameInstance.GetStaticEntitySystem` ...) | script API diff (removed / re-signed declarations the port names) + the compile |
| ArchiveXL 1.27.1 | the factories of `cyberpunkvrport.archive.xl` | API diff + compile; its log at launch |
| TweakXL 1.11.4 | 36 yaml records in `mods/tweaks` | compile; its log at launch (`check_tweaks.py` covers the game's records) |
| Input Loader v0.2.3 | `mods/config/input/CyberpunkVRPort_ScannerHud.xml` | the launch |

    py -3.13 -B tools/patchday/patchday.py frameworks [--sdk-build]

= `frameworks.py fetch` (the newest GitHub release of each, or `--tag name=tag`, into
`build/patchday/frameworks/<name>-<tag>/`: the zip as published, `release.json` with the notes, the files
under `files/` in the game's layout), `frameworks.py check` (report `build/patchday/frameworks-<time>/frameworks.md`;
for every release that differs from the installed one: files, script API, the compile, CET), `sdk_layouts.py
diff` (RED4ext.SDK master against the submodule's commit, for the 117 classes), and with `--sdk-build` a trial
build of the plugin against SDK master (its own build folder, CMake `CVR_RED4EXT_SDK_DIR`). `patchday.py check`
runs the same first three.

**Installing**: the frameworks are Vortex hardlinks (`fsutil hardlink list` shows Vortex's staging copy), so
drag the staged zip onto Vortex's Mods page (it upgrades the mod). Never copy over the files: a write through
the link changes Vortex's own copy. Rollback: Vortex keeps the previous archive; the tested files are also in
`base-2.31/frameworks/` with their hashes in `data/frameworks-2.31.json`. After a good launch:
`frameworks.py snapshot` records the new tested set.

**The live checks**: every start logs `[sdk-layout] N classes ..., M fields checked: X size and Y offset
mismatches` -- also on a build the gate turns VR off for, so the first launch of a patched game already names
each SDK class of ours that moved. `check_logs.py` counts every mismatch as a problem.

Rehearsed 2026-10-21:
- Codeware 1.20.3 -> 1.20.6, ArchiveXL 1.27.1 -> 1.27.5, TweakXL 1.11.4 -> 1.11.5 (all released 2026-10-19):
  0 declarations removed or re-signed (Codeware +4), the port's redscript compiles with all three.
- redscript 1.0.0-preview.22: 0 errors, 13 warnings (`UNUSED_ITEM_QUALIFIERS`: `const` on the wrapped
  state-machine functions), with the scripts in the struct-constructor syntax. 1.0 rewrites older syntax in
  place ("migration", logged) before it compiles; the port's 10 files with 63 `new Vector4/Vector2/HDRColor/
  inkMargin(...)` were moved to `Vector4(...)` on 2026-10-21 -- 0.5.31 accepts both (compiled offline and in
  the game). Of the other installed mods BlurBegone would still be migrated. A compile in the sandbox used to
  hang on 1.0: its console output filled an undrained pipe -- `check_reds.py` now writes it to a file and calls
  a run with neither log nor CPU for 2 minutes `STALLED`.
- CET 1.37.1 -> 1.36.0 (a real other build): Resize, Shutdown, the singleton and both trampoline fields re-found;
  ResetState's code differs (0x1CF -> 0x241 bytes; found as Shutdown's callee) -> by hand. Lua names 180 of 180,
  imports present.
- `selftest.py` framework_api: a Codeware with `GetLayers` removed and `GetStaticEntitySystem` re-signed -> both
  named with the port's lines, and the compile fails in `vrport_basketball.reds`.
- `[sdk-layout]` on 2.31: 117 classes, 442 fields, 0 mismatches. SDK master (f9ad5334, 12 commits past the
  submodule): only `entIPositionProvider` gained `worldOffset`, and the plugin builds against it
  (`--sdk-build`, a fresh build folder: ~20 min, most of it CMake fetching OpenXR and imgui).

## Statuses in remap/report.md

- `same` / `same-code`: the function's masked bytes are unchanged; same offset in it. Rewritten.
- `moved-in-func`: the function changed; the masked window of instructions around the site matched once in
  it. Rewritten.
- `fragment`: a cold fragment found by its window among the new root's chained `.pdata` fragments. Rewritten.
- `sig-global` / `sig-near`: no anchor; the window matched once in all of `.text` (or several times, one of
  them where the neighbourhood moved). Rewritten, review.
- `changed-func`: function start named by the library, body changed. Rewritten, review.
- `data-exact` / `data-delta` / `data-xref`: data by library hash, at the old distance past an entry, or by
  the RIP operand of the same instruction in an unchanged function. Rewritten.
- `ambiguous` / `changed` / `lost` / `data-slots-differ` / `data-xref-conflict`: NOT rewritten; in
  `kUnmapped`; a human.

## Known limits

- Field offsets are measured through the game's code only where the game itself uses the same constant
  (61 of 66); a field the game reaches by another computation (base + index * stride) has no carrier and
  surfaces as a code guard over changed code or a feature failing live. Offsets below 0x10000 (`+0x168` ...)
  are too common to measure this way -- the code guards over the functions that read them are what notices.
- RED4ext.SDK class layouts: the live check covers the fields RTTI lists (442 of 516); the 74 it does not
  (hand-reversed ones like `entEntity::transformComponent`) are covered only by the code guards over the
  functions that read them, and by a new SDK.
- `frameworks.py`'s Lua check is by name: a CET API name the port uses that the new `.asi` no longer carries
  is reported; a name that stayed but changed its arguments is not (the live launch's CET log is).
- The inventory recognises an address by its context (`*_RVA`, `base+0x..`, detour macros, guard tables) and
  the hand list `data/literal_overrides.json`; a NEW hex literal of an unusual form shows as `UNDECIDED` in
  `inventory.py`'s summary -- decide it there before patch day.
- `remap.py` is tested on 2.31 against itself and on a simulated move; the first real patch is its first real
  run -- read the report before building on it.
