# Overlay organization, controls and world marker size

The requested reorganization is implemented in the existing VR shell:

- GENERAL keeps menu/camera options; projection FOV, prediction, separation,
  world scale, IPD and frame reuse now live in STEREO.
- FRAMEGEN keeps generation/backend settings and runtime status. NVIDIA quality
  labels are Fast (performance), Medium (balanced) and Slow (best).
- OVERLAY contains the VR panel settings and all FPS overlay controls/statistics.
- CONTROLS contains GENERAL, DRIVING and BINDINGS. Driving includes the wheel,
  armed-driving throttle options and seated camera offsets formerly in GENERAL.
- VRIK is renamed AVATAR. BINDINGS is a grouped input/action table with readable
  wrapping, including on-foot, scanner, D-pad, driving and overlay controls.

`VrWidgets.cpp` provides shared sliders and lists using the supplied game-style
reference: red labels/angled borders, cyan values and arrows, and a value badge
that moves along the slider track. Native ImGui owns slider drag, numeric input,
popup selection, keyboard navigation and disabled state. Arrow buttons repeat
while held; increments follow the displayed precision. Long selector values fit
within their control and options retain full labels in the dropdown.

## World markers

The existing world-mappin collector now applies half the authored root scale
through the game's `SetScale(Vector2)` setter. This scales the icon and attached
label together around the existing pivot. Current live quest roots used1x1 scale
and a0.5x0.5 pivot; a preview in PID15864 successfully applied0.5x0.5 to all three.
This is a preview, not verification of the newly built native path.

Scale ownership is held by weak native widget identity alongside projection
metadata. Repeated position updates do not repeatedly halve the root. A new
authored scale is halved once, and a replacement widget starts fresh. Live hidden
roots are retained when pruning, preventing another halving on reappearance.
Minimap/world-map controllers remain excluded; direct screen-projected chatter
text keeps its existing size. Stereo depth and the prior batched-text fix remain.

## Optional analog movement

Reference: the user's local
`Downloads/cyberpunk-vr-port-0.1.6-TE6-testing-highlight/cyberpunk-vr-port-0.1.6-TE6-testing-highlight`,
especially `src/Hooks/XInput.cpp::ApplyStickRange` and the stick-tuning UI.
No other input remaps from that checkout were imported.

CONTROLS > GENERAL has **Analog movement**, off by default. Fixed mode retains
the current0.12/0.18 deadzones, movement quantization and gesture behavior.
Analog mode on foot maps each signed axis from its configurable centre deadzone
to the chosen full-travel point. Defaults are15% for each stick and90% full input;
ranges are0-30% and80-100%. Vehicles retain their existing response.

Settings persist as `xr_movement_speed_mode` (0 fixed,1 analog),
`xr_left_stick_deadzone`, `xr_right_stick_deadzone`, `xr_max_input_threshold`.
Sprint/dash/crouch use raw travel in analog mode so range remapping does not
activate them early. Scanner paging/zoom and dash rearming retain their previous
physical thresholds. Invalid/nonfinite analog values are neutralized.

## Validation

Five analog-input groups, ten world-marker groups, fifteen overlay groups and
thirty-three HUD groups passed (63 checks). The widget test covers arrows, dragging/clamping, popup keyboard choice,
Ctrl-click numeric input and disabled controls. WARP previews of the production
overlay controls and bindings guide were visually inspected. The GPU shell test
stubs backend-dependent settings pages, so it does not independently render every
reorganized page. Evidence/logs are in `build/overlay-refresh-20260924/`.

The final Release build passed and was installed with settings/calibration
preserved. Deployment report:
`build/roomscale-plugin-deploy-20260924-175111/deployment.json`.
DLL SHA256: `1f6c1f4e89372d1d71aa94e76bdaa838f687d9c14567ce9b4a1e08eb221a3377`.
The user restarted into PID20940 and confirmed the changes work. The loaded
DLL matches; both ink shader variants and the native projection hooks installed
without a marker scale/signature warning. No measured in-game analog speed curve
is claimed; the automated input tests validate the mapping and thresholds.
