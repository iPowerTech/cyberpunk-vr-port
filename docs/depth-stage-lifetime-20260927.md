# Depth staging lifetime during startup and resizing

## Evidence

Reports234256/PID18372 and235730/PID14312 contain NVIDIA Aftermath page-fault
data: a compute write targets an unmapped range formerly owned by a destroyed
committed R32_TYPELESS1485x1485 texture. Shader hash is unavailable (compute_01,
768 bytes, PC+240). The engine's coarse outstanding list is Lighting.

The direct D3D12 lifetime observer on DLL`ece2ed08`, PID19240, names the relevant
allocation path without changing its reference count:

- `OpenXRManager::CaptureSceneDepthInline`, OpenXRCapture.cpp:378 creates the
  plain R32 texture (flags0); line371 immediately releases the previous one.
- It runs from HookedResourceBarrier at DepthCapture.cpp:313, inside engine
  command-list recording, not after GPU completion.
- At startup the stage switches2560→1485→2560→1485, while the consumer is in
  menu mode with stable=0 and captured=0. One1485 stage is created and reaches
  final Release in the same millisecond on different recording threads.
- Engine depth textures are created through the placed-resource path with
  flags2; the port's staging copy is committed with flags0, matching the
  destroyed allocation class reported by Aftermath.
- The stage is an operand of CopyResource/CopyTextureRegion already recorded
  on the native list. A CPU mutex does not retain it until GPU execution.

The matching symbols and creation/destruction stacks are preserved in
`build/performance-20260926/resource-life-07/menu-success-19240.log`.
This establishes a concrete use-after-free path in the port. Cold startup and
in-game validation of the fix are still required; successful diagnostic boots
alone are not proof of a fix.

## Change

`CommandResources` keeps immutable resource batches on a command list through
its private IUnknown data. The recorded ownership ends on successful Reset or
list destruction, so discarded recordings and cached/replayed lists are both
handled. A weak registry does not keep deleted command lists alive.

Submission snapshots the batch BEFORE ExecuteCommandLists, so another thread
resetting the list immediately after Execute returns cannot erase ownership
before the post-hook records it. Each queue signals a completion fence and owns
its submitted batches until that value completes. Collection polls completion;
there is no CPU wait in the ordinary frame path. A fence failure stops further
captures and conservatively retains already-submitted references.

The inline stage writer, stage→snapshot copy and snapshot→XR depth copy all
retain their exact operands. This covers both pending writes and later readers
when a size change replaces the current stage/snapshot. Stage readers take a
CPU reference while holding the stage mutex. Scene-depth accessors acquire a
reference under the selection mutex; snapshot replacement and submit-side
acquisition share the present mutex. Re-selecting the same pinned scene-depth
resource now balances its AddRef instead of leaking a reference.

Inline capture also respects menu, depth-enabled and session gates. It waits
for the same60-frame stable gameplay condition already used by the consumer,
so transient startup/menu textures are not copied at all. Its frame claim is
rechecked under the stage mutex and published before ResourceBarrier reentry.
Shutdown disables the producer and drops the active staging reference.

## Validation

- WARP test with a deliberately blocked GPU queue verifies resources survive
  resize/reset until the completion fence, then are retired.
- Reset between native Execute and the submission post-hook is covered.
- Unsubmitted reset, replay of the same closed list, destruction without
  submission, and independent writer/reader queues are covered.
- A separate real D3D12 fixture verifies the diagnostic observer itself:
  65 texture lifetimes, non-final references, address reuse, untracked resource
  forwarding, and the disabled gate. It retains no extra resource references.
- All these tests pass. Release build succeeded. Installed and hash-verified
  DLL`429ab2cde64e1eba38c9c251bd685dd8d94c9f05f9b6b28c0ff2814ee77fab7a`;
  matching DLL/PDB are in `depth-lifetime-fix-08/`.
- User confirmed three normal starts (PID10156,3272,9260) and gameplay loading
  in the third. Startup logs were archived. No newer crash report appeared.
  Aftermath marker/debug-layer marker are absent; task-owned monitor26184 was
  stopped. The diagnostic hook is skipped, so the result does not depend on
  its slower startup timing.
- PID9260 menu reports stage=0/snapshot=0/captured=0. After gameplay warmup,
  stage=1/snapshot=1/captured=1: depth remains functional.
- Live retirement check over7.9 seconds: fence97234→98757, pending batches
  stayed1–3, recorded-list count6–7, failed=false, no failed submissions.
  MSVC container offsets were checked with a local fixture before read-only
  inspection. Data: `fixed-retirement-9260.json` and `fixed-lifetime-raw-9260.json`.
- Post-fix foreground sample:53 FPS, GPU87–92%, VRAM~8135–8155 MiB in a short
  sample. No capture fence waits/skips. This does not show a material FPS gain
  over the earlier focused53.6 FPS sample; the original performance question
  is not resolved by claiming this lifetime fix increases GPU utilization.

The earlier empty-ImGui optimization remains withdrawn. Story/cyberware changes
predating this investigation are preserved separately. No commit yet.
