# Intermittent pre-menu GPU crash, September24

User reports that two launches can freeze/crash before the menu while another
launch succeeds. Active report:
`Cyberpunk2077-20260924-233101-35480-24880`.
Reports232454/PID7980 and232421/PID12248 have the same engine assert.

## Confirmed dump evidence

- Engine assert at EXE+2A43F4B is GPU error reporting, not the instruction causing
  the GPU fault. Device removed reason887A0006 (`DXGI_ERROR_DEVICE_HUNG`).
- All three engine breadcrumb logs show HologramDepth_and_Distortion on the
  graphics queue and DecoupledParticleLighting on compute in progress; following
  command lists never started. DRED reports PageFaultVA0 and unsupported auto
  breadcrumbs. That alone does not establish a fence deadlock.
- NVIDIA Aftermath decoder shows an actual `Error_DMA_PageFault`, Graphics
  engine write to unmapped GPU VA115440F000 (PID35480), engine reset=true.
  PID7980 has VA118FC73000 and the identical fingerprint040FBDC8FAB2740B.
- Faulted shader is identified only as compute_01,512B, PC+110, MMU fault.
  No shader hash/source mapping is present. The crashing resource and exact
  originating API command are not identified by this dump.
- Driver596.49, GPU GB203-A. This is context, not evidence of faulty hardware or
  justification to change driver/system TDR settings.

Decoded evidence and native node disassembly are in
`build/startup-crash-20260924/`. Nsight Aftermath was used only to decode existing
files; no GPU monitor/capture injection or driver configuration was enabled.

## Concrete installer race found and fixed

`patch_command_list_vtable` previously changed15 shared command-list vtable
entries one at a time, then populated/published the originals registry at the
end. A render worker entering one of those detours during installation found no
registry entry and returned without forwarding the native command. Barriers,
root descriptor tables, dispatches and draws could be lost.

Now prepare the complete immutable originals record, obtain writable protection,
release-publish it, then atomically exchange the detour pointers. A concurrent
reader always has the originals. Failed protection leaves table/registry native.
No new per-command lock, extra GPU wait or runtime pacing change.

The test fixture imports production lookup and installer bodies verbatim, mocks
Win32 protection/pointer exchange, and schedules a simulated render caller at
every mutation boundary. Current ordering forwards270 calls /150 intercepted /
0 dropped. Moving registry publication back after patching (old ordering) loses
120 calls. Tests also cover failed protection and duplicate registration.
All3 tests pass, including the expected failing old-order regression. This proves
the CPU publication bug; it does not prove it is the sole cause of the reported
GPU MMU fault. Repeated launch validation is still required.

## Related pending work

The TPP handoff now stays inactive while menu mode is active. Its MAIN value
snapshot already fixes the VRCAM fallback architecture requested by the user;
the Basilisk HUD was visually accepted in PID33396.

The TPP steering hook261D5EC runs in PID21148 (179 calls/3sec), but its driving
gate remained false. Live Lua reports Driving/Tier2 while native cache still
reports no vehicle/Tier1. The cache refresh lived only in LocateCamera, whose
FPP serializer can be inactive when loading directly in TPP. Extracted the same
refresh and call it from both serializers, preserving the entity-tick throttle.
Steering motion still needs live validation after deployment.

Release build succeeded. Installed SHA256
bc46979c5bd1f682eee3970a1a9a2336f5d4278c1163734c3cb2127a39537a40;
checkpoint:build/startup-crash-20260924/candidate-01;
rollback/deploy report:build/roomscale-plugin-deploy-20260924-234916.
INI and calibration unchanged. No driver change or settings reset. User was asked
for three manual launches to the menu, then Basilisk TPP. The user confirmed all
three launches succeeded and loaded the Basilisk. PID29800 is running this exact
DLL and both command-list vtables were registered. This is the initial live
validation; three starts cannot exclude every intermittent GPU fault.
