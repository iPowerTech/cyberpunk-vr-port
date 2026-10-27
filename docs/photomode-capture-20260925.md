# Photo Mode screenshots and RTV retention (Cyberpunk 2077 2.31)

## Reproduction and native path

Taking a Photo Mode screenshot with a 2560x2560 VR render target created an
empty PNG and stopped visible frame progress. CET and some engine jobs still
ran, so this was not evidence of a stopped game thread or a Framegen deadlock.

Live x64dbg inspection established this path (addresses below are EXE RVAs):

- `298F140`: opens `Pictures/Cyberpunk 2077/photomode_*.png` and submits capture.
- `1D87940` -> renderer virtual method `29A5220` -> `224E0A0`: queues the request.
- `20E9AE8`: converts pixel dimensions to a fixed resolution enum. Unsupported
  dimensions, including 2560x2560, fall back to enum 7 (1920x1080).
- `224DEF4`: returns that preset while capture manager `+1C0` is set.
- The render job at `293978`, cold block `1E4E66D`, compares the requested size
  with the active size. On mismatch it requests a resize and returns without
  rendering the screenshot. Live values were requested 1920x1080 and active
  2560x2560. SettingsRes and DXGI keep the configured VR size, preventing the
  requested size from being reached.

Returning the configured VR dimensions for an active Photo Mode request lets
the native capture reach readback and completion. Inactive queries stay native.

## Second mismatch: CPU image size

Fixing the resize loop alone produced valid PNG containers with corrupt rows:
the header was 2160x2160 while the actual pixel stream had a 2560-pixel stride.
Pixel correlation confirmed the 2560 stride. The encoder at `290E438` copies a
tightly packed image using the supplied dimensions.

`224DE90` independently fits screenshot dimensions into 3840x2160, turning the
square VR size into 2160x2160 even though the render target remains 2560x2560.
The second hook preserves dimensions when the input exactly matches the forced
VR target on the Photo Mode readback path; unrelated dimensions continue through the native clamp. This keeps
the native allocation/readback/PNG path consistent rather than repairing the
saved file afterward.

`src/Hooks/PhotoModeCapture.cpp` owns both hooks. The size query uses a byte
signature; the 2.31 clamp RVA additionally checks its 16-byte entry. Hook
installation rolls back on failure. There is no new configuration setting,
persistent resource, frame loop, or screenshot encoder. Diagnostic counters
only increment behind `CyberpunkVR_RuntimeDiagnostics`.

## Validation

- Release build succeeds. Installed DLL includes both capture-hook exports.
- Native size-query signature matches exactly once at `224DEF4`.
- Candidate 02: user confirmed three saves without the original freeze, but
  all three 2160x2160 PNGs had incorrect row interpretation.
- Candidate 03, PID 24284: native hook installation reported `ok`; both size
  corrections were observed once for the first shot. Capture `+1C0` and phase
  `+180` returned to zero afterward.
- `photomode_25092026_150931.png`: 2560x2560, 7,425,424 bytes; visually inspected
  and the row corruption is gone. The user later confirmed that the blur came
  from their Photo Mode settings and requested no change to it. The subsequent
  `photomode_25092026_151419.png` is sharp, also 2560x2560.
- Candidate 03 follow-up: user reported 21-30 FPS after leaving Photo Mode and
  another GPU hang on re-entering (`151015-24284-32372`), without x64dbg attached.
  The same failure reproduced with Framegen disabled (`151500-37804-36912`).

## Re-entering Photo Mode without taking a screenshot

The user clarified that this second failure needs no screenshot: enter Photo
Mode, exit, then enter again. Disabling the VRCAM component made the sequence
stable. Re-enabling it reproduced the FPS collapse and GPU hang. Live component
status confirmed `enabled=0` and `enabled=1` respectively; this was not merely a
change to the overlay's enable flag.

Report `152207-33328-15984` recorded 16902 MB GPU memory used against 15995 MB
total. The native render-frame clock fell to approximately 14 FPS and then
stopped. The device reported `0x887A0006`; DRED did not identify a faulting
allocation. Framegen was excluded by the separate disabled comparison.

The mirror RTV candidate table in `src/Stereo/Capture.cpp` retained every
candidate resource with AddRef, up to 512 descriptors. This filter accepts
intermediate MAIN and VRCAM textures of the selected resolution, not just final
eye outputs. Retired targets, and the heaps backing placed resources, therefore
remained referenced across Photo Mode graph rebuilds. This table is now
non-owning descriptor metadata. Actual published capture sources still retain
their separate references. Lookup and replacement are protected by the same
mutex; reusing an RTV slot for a non-candidate/null target invalidates the old
entry as well.

Candidate 04 validation, PID 5868:

- Release build and `git diff --check` passed; installed DLL SHA256 is
  `bb512f08cae8b44713353f6c8646e3f51eb8af7f1dff5ab8b8c890ffd0f606cf`.
- User tested three Photo Mode enter/exit cycles without screenshots and
  confirmed no crash and lower VRAM use.
- After those cycles, nvidia-smi reported 9296 MiB used out of 16303 MiB for the
  whole GPU. This is a different counter from the engine crash-report estimate.
- A subsequent six-second read-only sample measured 42.87 native render frames
  per second; screenshot-active and capture-phase fields were both zero. GPU
  memory at that point was 9536 MiB, with the game still responsive.
- Native capture hook installed successfully. VRCAM is restored (`enabled=1`)
  and `xr_framegen=1` is restored. x64dbg is detached. No crash report newer
  than the start of this process was present.

Investigation artifacts and exact DLL/PDB checkpoints are in the ignored
`build/photomode-20260925/` directory. The first build after CMake's source glob
changed did not compile the new translation unit; candidate 01 was therefore
not a valid test of the hook. Subsequent builds explicitly verified the export
and the live hook-installation log.

GPU reports from the debugger-assisted runs (`144403-13624-7544`,
`145017-27000-26976`, `145956-17580-35448`) reported DXGI device hung
`0x887A0006`, without a DRED page-fault address. These reports do not establish
that Framegen caused the original screenshot freeze. Candidate 03 testing uses
read-only sampling plus a bounded diagnostic gate, without attaching x64dbg.

## Save preview regression: limit both overrides to Photo Mode requests

The native `capture + 0x1C0` flag means an active capture of either kind. It is
not a Photo Mode flag. The first implementation also gave save previews the VR
dimensions: recent `screenshot.png` files became 2560x2560 instead of the
previous native 456x256, showing repeated tiny images along the first rows.

IDA confirms `298E268` opens the save's `screenshot.png` and invokes `1D87940`
with its second argument set to 1. Photo Mode's `298F140` invokes the same
function with 0. `1D87940` writes request type 3 for previews and type 1 for
Photo Mode. `224E0A0` copies the descriptor into manager `+0x190`, placing the
type at manager `+0x1C4`. The size override now requires an active type-1
request; type 3 and all other types keep the native result.

The clamp helper at `224DE90` is also called by world-widget mesh/plane picking.
Its bypass now requires the screenshot readback return address `1C6BF92`, a
current type-1 request and an exact VR-size match. The renderer global
`342AC00`, renderer `+0x320` and descriptor type are read afresh. The readback
may finish after the active flag clears, so this gate uses the request's type,
not a lasting global Photo Mode toggle. Its native call instruction is verified
at hook installation. No extra hook or queued-request state was added.

Tests cover photo/save/inactive requests, invalid dimensions, equal-sized world
UI, unrelated resolutions and alternating photo/save requests. Release build
and `git diff --check` pass. Candidate DLL SHA256:
`684e69c823539bb29d1dcc6bea695b7849df2f9725ea16ab199f291aa8ef6b53`.
Existing save files and their already-written previews were not modified.
After restarting with the installed DLL and archive, the user confirmed that
both requested fixes work on 2026-09-25.

## Follow-up: world-UI leaf ABI regression

The later elevator crash showed that gating the C++ clamp detour by return
address still changed the native leaf's register contract for other callers.
The mesh-picker kept a live window pointer in R8, which the C++ wrapper clobbered.
This shared-entry detour is now removed. Only the screenshot readback CALL is
redirected, through a register-preserving assembly thunk. See
`elevator-cursor-crash-20260925.md` for the captured corrupt pointer and the
machine-code regression test. Request-type gating of save previews is retained.
