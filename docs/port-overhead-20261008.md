# Port overhead: GPU work the port adds to the frame (2026-10-08)

Rules for this work: no resolution change, no quality cut, no engine render-graph node disabled. Only
work the port itself adds is removed.

## Port GPU stages (city roof, -664.98 847.75 75, StageReplay on)

`tools/single_pass_tests/port_gpu_stages.py` samples the plugin's GPU stage profiler
(src/Render/GpuStageProfile.cpp). Its budget is 32 command-list batches shared by every stage, so a
single request is noisy; the A/B below repeats 6 requests per mode and normalizes by `CaptureMain`
(one per game frame).

| stage | per occurrence | note |
|---|---|---|
| CaptureMain | 37-46 us | eye 0 capture |
| CaptureSecond | 51-53 us | eye 1 capture |
| SecondBlit | 44-46 us | eye 1 extra copy |
| CaptureDepth | 41-48 us | depth capture |
| SubmitColor / SubmitDepth | 45-53 / 35-39 us | XR swapchain copies |
| HudSprites / HudBlur / HudStyle / HudSubmit | per channel canvas | see below |

## Empty HUD windows and channels are not drawn

A HUD window (`rootWidget` of a separate ink window) whose tree has no visible, non-transparent leaf
renders a fully transparent texture: in the city the Interaction channel had 1 sprite and 0 non-zero
pixels of 6.5 million, and several Main windows were empty too. `HasVisibleContent`
(src/Natives/HudPanel.cpp) walks the widget tree (visible, opacity > 0, tint alpha > 0; containers
are `inkCompoundWidget`, children at `inkMultiChildren+0x38`; anything else counts as content).

- An empty window keeps its slot (it stays masked natively) but adds no sprite.
- A channel whose windows are all empty sets `Snapshot::emptyContent`; `HudPanel::Capture` publishes
  a `Frame{empty=true}` without GPU work, and `HudQuad::Prepare` submits no layer but keeps the
  consumer ready, so the native slots stay masked and the next visible content appears in the quad,
  not natively first.
- Controls: `CyberpunkVR_HudSkipEmpty` (1 default, 0 draws everything), counter
  `CyberpunkVR_HudEmptySprites` (~1500 skipped windows/s in the city).

A/B, 6 rounds per mode, normalized per game frame:

| | canvases / frame | HudSprites / canvas | HUD GPU / frame |
|---|---|---|---|
| skip=0 | 1.76 | 34.8 us | ~388 us |
| skip=1 | 1.00 | 13.2 us | ~197 us |

Saving: ~0.19 ms GPU per frame. Visual check: Main HUD identical in both modes (FPS counter,
crosshair, bottom-left indicator, world labels); the interaction prompt ("[F] Сесть" at a spawned
car) appears with skip=1 (confirmed in the headset view). Simulator previews via
`tools/roomscale_tests/capture_preview.py` come back black on most PrintWindow calls in both modes;
retry until the file is large.

## Remaining port items

- Mono sun cascades: capture each slice to a committed image and restore into MAIN's aliased atlas,
  ~0.3 ms (docs/mono-sun-cascades-20260930.md). Removing the copies needs SRV redirection to the
  committed image and double buffering.
- Eye 1 extra copy (SecondBlit ~44 us).
