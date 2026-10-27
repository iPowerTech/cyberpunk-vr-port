# HUD panel checks

```powershell
cmake -S tools/hud_tests -B build/hud-tests -A x64
cmake --build build/hud-tests --config Release
ctest --test-dir build/hud-tests -C Release --output-on-failure
```

The 33 C++ checks run production follow math, ColorBlit, panel capture and XR quad
code. D3D12 uses WARP; XR lifecycle is mocked, including a wait timeout and capture
serials with identical timestamps. Pixel readback checks all 64 sprite descriptors,
placement, transparent margins, tint and alpha, plus the native gain and shadow.
Held frames cannot be reused. Additional cases exercise stereo cancellation with
moving head poses, asymmetric and canted eye projections, body follow, precise display time, vertical look, layout latching,
per-element transforms and settings round trips.

Delayed follow is enabled for menu and HUD head-follow mode: a yaw offset strictly
above 10 degrees and below the configured cone follows after three seconds at
rest (0.5-degree jitter tolerance). Tests cover continued turning, strict angle
limits, wrapping, slow frames, tracking loss/recenter and actual HUD quad wiring.
The FPS overlay keeps the original cone behavior without delayed follow.
The diagnostic snapshot test verifies that DEBUG off stops snapshot writes while
the HUD still renders and follows. Live motion probes require launcher DEBUG or
`CyberpunkVR_RuntimeDiagnostics`; frozen counters are not tracking evidence.
The interaction channel checks texture/readiness isolation and its own90-degree
head cone, with no3-second catch-up and no dependency on main HUD body mode.
Its loot case publishes visible-content metadata with the captured frame, uses
the default10-degree loot cone, and restores the ordinary interaction cone when
loot closes. Reopening loot catches up smoothly without resetting the anchor.
Subtitles remain in Main; world interaction/dialog choices use Interaction, while
ordinary button hints remain in Main. Bounds fixtures initialize the converted
interaction window and extend the subtitle crop to include a negative mainPanel
position without changing native window layout. They cover larger text, transient
empty layout and rescans without cumulative crop growth.

`phone_bounds.lua` is a fixture for the production CET module. Replace its exact
`-- HUD_MODULE` line with `mods/cet/CyberpunkVRPort_Stereo/modules/hud_panel.lua` and
execute the resulting chunk in Lua/CET. All game APIs used by the module are lexical
mocks, so this does not alter game globals or widgets. It covers initial metadata,
full-screen bounds, holocalls, existing audio bounds, resize, stable layout, image
refresh, expired roots and layer replacement. Module loading is tested with all
game types absent, as on CET startup before `onInit`.
Loot detection also covers hidden/transparent ancestors, replaced or removed
widgets, respawn, and Codeware's fallback to the root for an invalid child path.

`cold_start.lua` tests the complete Stereo bootstrap. Replace its `-- HUD_MODULE`
and `-- STEREO_INIT` lines with the production HUD module and Stereo `init.lua`.
The mock environment exposes registration APIs at load time and game APIs only at
`onInit`, then exercises early update, initialization, HUD/VRCAM updates, the hotkey
and shutdown. It runs that lifecycle twice. The original eager CName construction
and late hotkey registration both fail this fixture.

Live evidence and verified IDA sites are in `docs/hud-panel-20260922.md`.

`live_motion.py` resolves matching DLL/PDB symbols and reads the HUD diagnostic
block while supplying a bounded simulator head trajectory (yaw +/-1.4 radians,
translation 2.5 cm and pitch 0.12 radians). It requires a live OpenXR Simulator
session with head-follow and angular stereo HUD enabled, checks the display clock,
follow speed, eye alignment and settled drift, and restores the starting simulator
pose in `finally`. It never writes game memory or changes focus, keys or settings.
Supply `--pid`, `--dll` and a fresh `--out` JSON path. Installed and local DLLs must
match; the matching PDB must be beside the chosen DLL.

`live_layout.lua` is a live regression probe for the alternate minimap slot. Its
returned function accepts `snapshot`, `show_alt` and `hide_alt`. Only run the
visibility cycle from a confirmed inactive alternate slot, allow UI updates
between calls, and always finish with `hide_alt`. Compare the settled container
width and minimap/quest coordinates before and after; transient layout values
in the same call that changes visibility are not the final result.
