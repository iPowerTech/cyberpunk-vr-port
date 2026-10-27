# Local-shadow reuse and the moving flashlight

## Prior project findings

`src/Stereo/ViewReuse.cpp` records why `LocalShadowReuse=1` was introduced:
VRCAM skips the native local-shadow node at RVA0xAD5770 and samples a shared
per-light shadow atlas. Both views had identical light-to-slice mappings. An
older audit measured VRCAM's local-shadow CPU work at 10.6x MAIN's (another note
reports 30x), with repeated updates disturbing the shared cache and causing
flicker. These are ratios for one CPU node, not frame-rate multipliers.

The historical comparisons were about other symptoms:

* 2026-07-29: disabling reuse made the node execute for VRCAM, but missing lamp
  illumination remained. Reuse was restored for performance.
* 2026-08-18: disabling reuse did not fix the interior specular mismatch
  (shiny sofa/ceiling in VRCAM, normal in MAIN). Reuse was restored again.

`docs/vrcam_node_audit_v2.md` explicitly captured `LocalShadowReuse=1`.
It measured 0.1275 ms MAIN versus 0.0011 ms VRCAM self CPU time per frame for
this node. It does not measure the on/off GPU or FPS cost. Historical comments
claiming that matching slices make reuse universally exact were too broad:
they establish index compatibility, not current contents for a moving light.

## Current controlled comparison

Game PID10760, started2026-09-23 15:43:05. Loaded marker DLL SHA256:
`2a516851e86db6f0522decf623406a0ef890a291525196c2f7bd7746a3d42c96`.
The user identified a flashlight-shadow trail during head movement only in the
left/VRCAM eye; MAIN had no trail.

Only `xr_local_shadow_reuse` in the installed `bin/x64/vrport.ini` changed from
1 to0 using the existing hot reload. No DLL deployment, game restart, pose
command or marker setting change was involved.

Read-only live evidence under `build/world-markers-re`:

* `shadow-10760-reuse-before.json`: mode1, skip hits138039 ->138195 over0.5s;
  MAIN/VRCAM camera uploads both advance, world-marker correction remains1.
* `shadow-10760-reuse-disabled.json`: mode0, skip hits150327 ->150327 over0.5s;
  camera uploads advance by26 in each eye, world-marker correction remains1.
* Original settings backup: `shadow-10760-vrport-before.ini`.

The user first reported improvement and then explicitly confirmed:
"Ну гостинг исчез полностью". Keep the installed key at0 and default native
local-shadow updates to0 in source. Retain1 only as an explicit experiment.

This identifies the reuse switch as the cause of the observed trail in this
scene. A stale atlas or mismatched update timing is a plausible mechanism, but
the exact age/order has not been measured in a new GPU capture. Do not claim a
proven one-frame delay, a measured FPS cost, or universal scene validation.
Read-only static assembly for the node and renderer is saved in
`build/world-markers-re/shadow-native-node.txt`; RenderDoc replay stayed closed.

The current game already uses the corrected setting. The source default is for
future builds/fresh configurations; it does not require interrupting this session.
The Release build with the new default passed (`shadow-default-build.log`). It
was not deployed over the running game. The matching DLL/PDB for PID10760 were
preserved under `build/world-markers-re/accepted-2a516851/`; pass that DLL with
`live_probe.py --dll` while inspecting this launch after rebuilding.
