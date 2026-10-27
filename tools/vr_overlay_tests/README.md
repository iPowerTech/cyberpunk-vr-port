# VR overlay checks

Build and run:

```powershell
cmake -S tools/vr_overlay_tests -B build/vr-overlay-tests -A x64
cmake --build build/vr-overlay-tests --config Release --parallel 4
ctest --test-dir build/vr-overlay-tests -C Release --output-on-failure
```

The GPU check uses WARP, the production ImGui shell/renderer and a mock XR
swapchain. It rejects debug-layer errors and checks timeout ownership, both-eye
visibility, the ray layer and closure cleanup. PPM files show the real draw output.
The test substitutes backend-dependent settings sections, but renders the game
actions, overlay panel controls and bindings guide from production code. It does
not certify physical controller feel. A desktop pass also checks transformed draw
data restoration, and a pure test checks fitting/pointer coordinates.
`chord_reopen` covers reopening after an idle bridge heartbeat, one toggle per
hold and the next hold closing the panel. `bridge_timeout` retains the active
bridge failure timeout. The suite has15 cases.
`widgets` drives the production themed controls through real ImGui input: arrow
increments, clamped slider drag, dropdown selection, keyboard numeric entry and
disabled controls. GPU outputs include `vr-bindings.ppm` for the bindings table.
`imgui_input` runs the actual ImGui event processing with90Hz pointer/wheel input
and30Hz UI frames. It checks scroll backlog, neutral stop, short clicks, stalled
frames and off-panel cancellation without disabling ImGui's click trickling.
It also checks cursor ownership with noisy desktop motion/leave events, tracking
loss/recovery, real button activation for both sources, held-button handoff,
desktop idle/drag behavior and VR trigger/scroll/grip reclaim.

Replace the `-- OVERLAY_MODULE` line in `bridge.lua` with the production
`modules/vr_overlay.lua` source. Its lexical game mocks cover pause/open/resume,
native action dispatch and shutdown. PauseGame/UnpauseGame mocks throw: opening or
closing the overlay must not call them. Only selecting a native game action opens
the actual pause scenario.
No global game objects are modified by that fixture.
