# 0.1.7 consolidation review

Base: `b4a74461` (0.1.6, local `origin/main`).
Previous tip: `9f02086f`. All 32 subsequent commits are included, in order.
The old history is retained at `codex/pre-0.1.7-squash-20260928`.

This is a release inventory review of every commit's message and changed-file
inventory, with targeted code diffs and supporting validation notes. It is not
a claim that every line of the accumulated engine research was independently
audited or that all hardware/gameplay paths were retested for this release.
The final tree retains all accumulated work; only release metadata,
documentation and packaging change during consolidation.

| Original commit | Reviewed contribution and final release treatment |
| --- | --- |
| dabbcbae | Water descriptor assignment, grading LUT source, takeover fixes, tactical belt and grenade assets/scripts. Old unreleased 0.1.7 title is folded into this release. |
| f290ece1 | Scene-only stick yaw and input routing; old unreleased 0.1.8 title is folded into 0.1.7. |
| 046457d6 | Native roomscale, camera/body pose identity, VRIK stabilization, hand collision fixes and regression fixtures. |
| b0a600cc | HMD-aligned bending, pelvis/spine fit, foot contacts and shoulder reach. Later threshold changes are retained. |
| 3064b7ba | Vehicle heading, Combat pose/reload, scene suspension and roomscale thresholds. |
| 75f334b9 | Gesture swimming, water posture, native action ABI protection and belt startup clothing restoration. |
| 8e186ba2 | Ladder grip climbing, finger/rung poses and female neutral camera offset. |
| df6a7fb2 | Scene pose transport, VRIK suspension and body yaw continuity. |
| 3dbc3155 | Native HUD extraction, panels, free-look and per-element settings; supersedes bundled HUDitor setup. |
| d03cf78f | World-marker per-eye projection and local-shadow reuse default 0. |
| f828b61f | Physical-headset evidence for shared HUD layer; documentation-only, not a new renderer feature. |
| 1fdd829e | Native motion/depth frame generation, pacing, telemetry and performance overlay. |
| 54854ffd | Controller-driven VR overlay and diagnostic gating. |
| 055d0379 | Subtitle capture and independent interaction/dialogue HUD channel. |
| 52d9f40e | Overlay input ownership, cyberware melee laser suppression and serialized rig rebinding. |
| 23eeb3dc | Hybrid HMD/controller body rotation and belt appearance ownership. |
| caea2af1 | HUD/overlay refinements, separate loot channel, optional analog input and retirement of LootUi. Marker scale is superseded by 0.7 below. |
| 019ed1ee | Swimming response and stroke-speed-dependent boost. |
| fccdcade | Tracked wheel response, full-lock recovery, optional prediction and seated VRIK rounding tolerance. |
| 65efef71 | External/third-person cameras, Basilisk controls and HUD, weak-handle scope and descriptor lifetime fixes. |
| ed8abd96 | Photo Mode dimensions, reduced resource retention, marker scale 0.7. The shared clamp detour is superseded below. |
| c0a08e3e | Keypad cursor response and input retention outside the look cone. |
| 6860f525 | Save-thumbnail request separation and native subtitle scale. |
| 99ed712c | Moving story-camera heading, story HUD layouts and readback-only register-preserving Photo Mode clamp hook. |
| 143a1402 | Head-driven surveillance motors, takeover pose identity and scanner/CCTV HUD extraction. |
| 0cc06d12 | Loading-screen camera suspension, reused-EntityID rebinding and terrain resource readiness guards. |
| 0d6a0219 | Early sky radiance, per-eye graph cache and AA synchronization. Initial fog handle-only borrowing is superseded by the final fog fix. |
| 1b4c27f7 | MAIN-only shared sky-cache updates to prevent distant background flicker. |
| 482549c0 | All active cascade matrices; removal of SettingsGuard, MAIN cascade draw suppression and repeat settings replacement. |
| d3a9aea6 | Story attention, cyberware chord, depth lifetime, Reflex controls, overlay/Lua performance and bounded GPU timing. |
| d8c7fe9e | Reed camera objective, sniper head/HUD following, FG+RT formats, RT-toggle flags, settings merge, launcher fixes, AMD VRS guard, Steam Frame/G2, tutorial cameras and accumulated rendering research. QuickBoot remains source-only and excluded from dist. |
| 9f02086f | Reprojected MAIN fog history paired with its source matrix; native current-eye integration remains intact. User confirmed stable lamp beams after deployment. |

## Release decisions

- CMake project, RED4ext plugin metadata and package default use 0.1.7.
- Changelog describes the final combined behavior, not intermediate approaches
  later removed. README no longer claims that the release installs HUDitor.
- QuickBoot, ReloadRecorder, WorldMapDiag and single-pass probe shader assets
  are not packaged. Development source and historical notes remain available.
- No personal vrport.ini, launcher INI, calibration or save is copied into the
  dist. UserSettings.json in the package is the repo preset.
- More Occluders (Scripted v0.3) is listed as a separate requirement. The
  author's Nexus permissions do not allow re-uploading it in this release.
- Single-pass rendering is research code, not advertised as the release path.
- AMD divide-by-zero handling is included without claiming physical AMD testing.
- No push or remote tag/publication is part of this operation.

Build/test/package manifests and the original per-commit inventories are saved
under `build/release-0.1.7/`; the release ZIP is under `dist/`.

## Pre-release verification

- Release DLL built successfully; static inspection of the exported Query
  function confirms RED4ext semantic version 0.1.7.
- Rebuilt 32 current test suites and passed all 341 CTest tests. Stale build
  directories whose source projects no longer exist were not counted.
- QuickBoot source tests remain covered even though its CET module is excluded
  from the distribution.
- The old dist folder and ZIP named 0.1.7 were preserved under
  `build/release-0.1.7/previous-dist/` before packaging the new release.
- Package inspection caught a shared `r6/scripts/cache` directory entering the
  generated uninstaller. Packaging now admits only nonempty CyberpunkVRPort
  redscript modules, excluding shared caches and disabled authoring scripts.
- `CHANGELOG.md` remains local and ignored, as before this release task. The
  release ZIP includes it as `CHANGELOG.txt` from the local file.
- The final 0.1.7 archive excludes `MoreOccluders.reds`; users install the
  scripted v0.3 variant from its author's Nexus page.
