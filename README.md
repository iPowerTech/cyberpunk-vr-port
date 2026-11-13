# CyberpunkVR Port

A 6-DoF **VR mod for Cyberpunk 2077**, built as one **RED4ext plugin** (`cyberpunkvrport.dll`): OpenXR head
tracking, real stereo from the engine, a full-body avatar with motion-controlled hands, VR weapon aim, the physical
reload, motion melee, holsters, driving with the wheel in your hands, and an in-headset settings overlay.

This is a fork of [dariulone/cyberpunk-vr-port](https://github.com/dariulone/cyberpunk-vr-port). On top of it, it adds
**DLSS 5 Neural Rendering** and **FSR3 frame generation** in VR, NVIDIA Reflex control, hand interaction with devices
and screens, and its own controls (X and A swapped on foot, sprint on the right stick).

> ⚠️ Experimental community mod. Not affiliated with CD PROJEKT RED. Use at your own risk and keep backups of your
> saves.

## Requirements

- Cyberpunk 2077 **2.5** (PC). The engine offsets are matched to 2.5; earlier versions do not work.
- [RED4ext](https://github.com/wopss/RED4ext) **1.31.0** or newer.
- An OpenXR runtime (SteamVR, Virtual Desktop's VDXR, Pimax, ...). On Quest through Virtual Desktop, use **VDXR**.
- For DLSS 5 Neural Rendering: an NVIDIA RTX GPU and `nvngx_dlssnr.dll` (see below).

Cyber Engine Tweaks, ArchiveXL, TweakXL, Codeware, Input Loader and redscript are **not** needed: the plugin runs the
port's Lua mods, mounts its archive, writes its TweakDB records and compiles its scripts itself. Keep them if other
mods of yours need them; the port works with or without them.

## Installation

**One-click installer:** run it and pick the game's main folder (the one with `bin`, `r6` and `archive`). It checks
the game version, removes earlier releases of the port and installs RED4ext and `nvngx_dlssnr.dll`.

**By hand:** extract the release archive into the game's main folder. Everything lands in
`red4ext\plugins\cyberpunkvrport\`. Delete `red4ext\plugins\CyberpunkVR_Stereo` and `CyberpunkVR_Hands` if an older
release left them; their settings and calibration move over at the first start.

Then **start your OpenXR runtime first** and launch the game. On the first launch the VR preset is merged into the
game's `UserSettings.json`, with a timestamped backup.

> Anything else that proxies `dxgi.dll` in `bin\x64` (R.E.A.L. VR, for one) must be removed: the two fight over the
> same engine hooks.

From a source tree:

```
cmake --build build-stereo --config Release --target cyberpunkvrport_stereo
pwsh scripts\build_dist.ps1 -Version 0.2.3 -BuildDir build-stereo
```

## DLSS 5 Neural Rendering

NVIDIA's DLSS 5 neural rendering model (`nvngx_dlssnr.dll`, tested with 310.9.16), run on **both eyes**.

- **Where it runs:** inside the game's own frame, per eye, right after DLSS, writing back onto the DLSS output. The
  headset capture and FSR3 come after it, so the path to the headset is unchanged.
- **The DLL:** `nvngx_dlssnr.dll` goes into `bin\x64` beside the game's exe. It is not part of this repository; the
  one-click installer includes it.
- **Settings:** F10 overlay or VR menu, **DLSS5-NR** page:
  - **DLSS 5 Neural Render** on/off. Off is the game's own DLSS picture, untouched.
  - **Alternate eyes:** one eye per frame, half the cost.
  - Preset, style, **intensity** (0-2; above 1 the model's change is pushed further), local tone and structure
    strength, automatic skin mask with its own structure strength, colour strength, scene paper white.
  - The live line shows passes per frame, the GPU time of each eye's pass and the VRAM it uses.
- **Cost:** about 4 ms of GPU per eye at 1864x2016 per eye on the development PC; it scales with the output
  resolution, not with the DLSS mode. Each eye keeps one context of about 0.55 GB of VRAM.
- **Lifetime:** NR's contexts go with the game's DLSS features. When the game releases DLSS (loads, menus, exit) NR
  releases its own first, so closing the game with NR on does not leave the GPU in a bad state.

## FSR3 frame generation

AMD FidelityFX FSR3 frame generation for the headset, from the game's motion vectors and depth, per eye.

- **Needs:** DLSS, DLAA or another upscaler that hands the game's motion vectors over.
- **Settings:** F10 overlay or VR menu, **FSR3** page:
  - **FSR3 frame generation** on/off.
  - **Mode:**
    - **Interpolation:** the frame halfway between two real frames, shown before the second one. The real frame waits
      for it: about half a game frame more latency.
    - **Extrapolation:** the real frame goes to the headset at once; half a game frame later the same frame is moved
      half a frame ahead along the motion vectors, with the head pose moved ahead too. No added wait, but it is a
      prediction: it misses when the motion changes, and what a moving object uncovers is stretched.
  - **Optical flow:** FidelityFX optical flow for what the game's vectors miss (particles, transparencies). Costs GPU
    time.
  - **Show the generated frames:** off sends only the real frames (a test of what showing them costs; not saved).
- **Snap turns** are treated as cuts, so no frame is generated across one.
- **Works with DLSS 5 NR:** NR runs inside the game frame, FSR3 after the capture.
- **Runtimes:** works with the port's threaded submit and with the inline loop Virtual Desktop's VDXR uses. On Quest,
  SteamVR through Virtual Desktop throttles the headset loop when NR is on; use VDXR there.

## Controls

VR controller input is merged into the game's own gamepad, so the game's *Controller* bindings apply. In menus and
braindances the pad is the game's own. On foot:

| Input | Action |
|---|---|
| Left stick | Move, one speed whatever the deflection |
| Left stick fully forward, held | Sprint (default; see *Sprint* below) |
| Right stick X | Turn (snap or smooth) |
| Right stick fully up / fully down | Dash / crouch |
| Right trigger / left trigger | Fire / aim (left trigger is also the melee block) |
| Right grip | Draw from or put back into a holster |
| Left grip | Grab the magazine during a reload; at your left ear, the scanner |
| A | Reload, interact (X and A are swapped on foot, `xr_swap_xa`, on by default) |
| X | Jump |
| B, weapon in hand | Drop the magazine (physical reload) |
| Y tap / Y held | Next weapon / VR weapon wheel |
| L3 + R3, held 0.5 s | Open or close the VR menu |
| L3 + left grip, held 0.5 s | Activate the cyberware operating system |
| Squat in the room | Crouch |
| A hand on a device / a fingertip on a screen | Use it (press its button, touch the screen) |

**Sprint** (*Controls → Sprint*): hold the left stick forward (default); click the left stick; or push the **right
stick forward** once to sprint until the left stick is let go, with a second quick push to dash.

**Driving:** grab the wheel with a grip where the animation holds it and steer with the tilt of your hands. Right and
left trigger are throttle and brake. A hand on the hub is the horn. With a weapon drawn the right trigger fires and the
throttle latches. Hold B to get out.

Hotkeys: `F7` recenter, `F10` / `Insert` the settings overlay.

## Settings

Everything is set live in the **F10 overlay** (in both eyes, operated with the controllers) or in the **VR menu**
(L3 + R3). Settings are saved to `red4ext\plugins\cyberpunkvrport\config\vrport.ini`. Before the game starts, the
launcher picks the headset and render resolution (19 headsets, with a resolution ladder each).

## Logs and bug reports

- `red4ext\plugins\cyberpunkvrport\log\cyberpunkvrport.log` is the plugin's log and the file for a bug report. It is
  quiet by default; *F10 → Diagnostics* has the switches for detailed reports (they cost frame time, leave them off
  for play).
- `red4ext\logs\` holds script and plugin load errors. If a script fails to compile, every redscript mod is off, the
  port's too, so look there first when everything stops working at once.

## Test hardware used during development

- Headsets: Pimax, PICO 4 (VDXR), Meta Quest 3 (Steam Link and VDXR)
- CPU: AMD Ryzen 7 5800X, RAM 32 GB DDR4
- GPU: NVIDIA RTX 5070 Ti
- OS: Windows 11 Pro 25H2

## Credits

- **[dariulone](https://github.com/dariulone/cyberpunk-vr-port)**: the original port this fork is built on.
- **[ClarkCheekyKent / CheekyFoveatedDLSS](https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS)**: foveated DLSS
  research and the initial comparison backend.
- **[dabinn](https://github.com/dabinn/cyberpunk-vr-port)**: weapon aiming, head aim and renderer stability; fixes
  adapted from his fork.
- **[Ajson44](https://github.com/Ajson44/cyberpunk-vr-port)**: story progression, scripted cameras and quest
  interaction research.
- **[tig3rmast3r / OFXR Bridge](https://github.com/tig3rmast3r/OFXR-Bridge)**: open VR frame-generation research.
- **[satyaloka93](https://github.com/satyaloka93)**: PlayStation VR2 support, frame generation and stereo DLSS work,
  the aiming dot in both eyes under engine stereo.
- **[Syncher-source](https://github.com/Syncher-source)**: finger tracking, full-body tracking with Vive trackers, the
  HUD kept in front of the world, automatic recentering.
- **[EffinexX](https://github.com/EffinexX)**: the arm launcher's aim dot, lock-on markers for smart weapons, the
  overlay's input capture with a resting grip.
