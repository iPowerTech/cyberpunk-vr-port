# Elevator world-widget cursor crash

Report: `Cyberpunk2077-20260925-215151-43096-10184`, game PID43096, GameThread10184.
Installed candidate at the crash was the combined HUD DLL
`d4d88b8f80a4081f9ce47685d9bcc504f23625909a3613727614a199c491540f`.
The user approached an elevator panel and the game crashed while its cursor appeared.

## Evidence

- Exception `C0000005`, reported read at `FFFFFFFFFFFFFFFF` (non-canonical pointer).
- Fault EXE RVA`28A8BE`: `lock inc dword ptr [rax]` in `28A8A4`, obtaining the
  native window's widget picker. RCX was `EXE+2446768`, not a window object.
- The actual 56-byte hit record at stack`CC871FF090` contains
  `window.instance=7FF616546768` while its weak reference counter is a heap pointer.
  `7FF616546768` is exactly the return address of the mesh-picker's clamp call.
- Mesh hit mapper`2446050` loads the real window into R8, calls shared clamp
  `224DE90` at`2446763`, then stores R8 as the result window at`244677D`.
  The original leaf helper does not modify R8.
- Our ordinary C++ `ClampCaptureSize` detour checked `_ReturnAddress()` and
  called other C++ code before falling back to the original. It obeyed standard
  Win64 ABI, where R8 is volatile, but not this native caller's stricter contract.
  Selecting the original function in C++ was therefore insufficient isolation.

This is a regression in the shared Photo Mode clamp hook. The numeric-keypad
ownership/controller filter does not prevent it: native mesh picking reaches
the same helper before the keypad hook's post-processing. No expanded keypad
scope, elevator-specific input mode or corrupted save is required to explain it.

The small initial stack report is only a stack scan. An ordinary thread-context
unwind starts in the crash handler, so it is not used as evidence for the fault
chain. The exception registers, captured hit record and verified native
instructions establish the pointer corruption directly.

## Fix

Remove the detour on the shared clamp entry entirely. Patch only the verified
readback CALL at`1C6BF8D`, which normally targets`224DE90`. Both world-widget
callers (`2446763`, `2446B5B`) continue to call the untouched native leaf.

A nearby register-neutral absolute-jump relay reaches `PhotoCaptureClampThunk.asm`.
The thunk saves RAX/RCX/RDX/R8-R11, flags and XMM0-5 around the C++ photo-request
predicate. When no bypass is needed it restores everything and tail-calls the
original native helper. A genuine Photo Mode VR-size bypass restores the incoming
registers and returns. The thunk has standard Windows unwind metadata. Existing
request-type separation keeps savegame previews native.

Installation verifies the original clamp bytes, the exact CALL target and relay
range. No generic world-UI code or keypad behavior is disabled. The previous
DLL/PDB is saved under `build/elevator-cursor-crash-20260925/before/`.

## Validation

- Release build and `git diff --check` pass.
- Photo/save/inactive/request-transition tests pass.
- New machine-code regression deliberately clobbers every volatile register in
  the policy callback. Its negative control reproduces R8 corruption with an
  ordinary C++ wrapper. The production thunk matches the native leaf or bypass
  reference for all registers/flags across10 cases, including600x1066 UI bounds,
  456x256 thumbnails, square VR and oversized readback.
- Candidate DLL SHA256:
  `e100d38e71322b1d281bb7f4cf3e5c4200734b99df6424fcc78a27050c955e73`.
- The user restarted with this DLL, checked the same elevator panel and confirmed
  it works without the crash. Phone/Photo Mode were not separately replayed in
  this final check; native/bypass ABI and request-type regression tests passed.

Crash extraction, IDA output and test/build logs are retained under
`build/elevator-cursor-crash-20260925/`; original crash-report files were not changed.
