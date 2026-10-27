# Cascade shadow quality changes and SettingsGuard removal

## Confirmed cause

The previous sampling correction was tied to the Low cascade layout:

- `cascs_is_setup` accepted exactly two affine matrices and required matrices 2/3
  in the 928-byte CSConstants block to be entirely zero.
- The captured/copied matrix range contained only 32 floats (two matrices).
- Extent matching inspected only those two cascades.

In PID 16968 the user changed only CascadedShadowsRange from Low to Medium through
the native settings menu, with CascadedShadowsResolution kept at Low. A passive
collector observed the new third matrix at 70.54 s, then gameplay resumed at
71.44 s. The user confirmed the shadow jumps appeared.

| Gameplay interval | Active matrices | Sampling-pair counter | Render-camera-pair counter |
| --- | --- | --- | --- |
| Before settings change | 2 | 19,940 to 25,094 | 20,616 to 25,769 |
| After Medium applied | 3 | **25,104 to 25,104** | 25,779 to 32,265 |

The rasterization/camera correction continued, but sampling-matrix correction
stopped for the entire remaining 48 s. This was a rejected layout, not merely a
brief resource-recreation delay. Raw data: `build/cascade-settings-20260926/`.

An earlier PID 10092 session stalled after a programmatic settings confirmation
and a short debugger capture. It is not used as successful settings-application
evidence. The user restarted, and subsequent setting changes used the ordinary
menu with a collector that never paused the game or attached a debugger.

## Changes

`CascadeSampling.hpp` recognizes a contiguous active prefix of the shader's four
matrices. Active matrices must be finite, nondegenerate and affine; unused slots
must be zero. Layout matching requires equal active counts and compares extents
for **every** active cascade. The existing 2% extent tolerance is retained;
rotation and translation remain unrestricted, so normal head turns do not disable
the correction.

Both upload paths now capture/copy the full 64-float matrix array, including its
zero tail. The rest of CSConstants remains native in the default mode. Diagnostic
comparisons cover all four cascades. Render-camera sharing, fit-record sharing
and the existing upload-order policy are unchanged.

SettingsGuard only hid CascadedShadowsRange and CascadedShadowsResolution from the
menu. Its REDscript wrapper was removed from the repository and the installed game
(backed up outside the game). The README inventory and package upgrade notes were
updated. `deploy_full.ps1` treats the previous wrapper as a retired script so an
upgrade also removes it after backup. No other game settings are forced to Low.

The native first-launch code also contained a separate cascade guard: any
difference from the preset could replace the entire UserSettings.json on a later
launch. That comparison and replacement path were removed. `first_launch=0` now
returns before opening either settings file. The independent, explicitly gated
first-install VR preset remains; shadow-quality changes cannot trigger it.

## Resolution change and removal of MAIN draw suppression

PID 14920 confirmed that the new matrix handling follows Medium (3) and High (4)
range settings. Changing resolution to Medium then produced a severely dark MAIN
view while VRCAM remained correct. Disabling the legacy `CascadeSaveMain` export
restored both images immediately (before/after screenshots are saved with the
capture artifacts).

A candidate that restricted the optimization by current-frame depth resource and
subresource identity was installed as `24F191A7...`. The user reported that MAIN's
shadows were still incorrect and explicitly requested complete removal of
SaveMain. That candidate, its descriptor-copy hooks, metadata/cache classes and
tests were withdrawn. They are not part of the final change.

The final version removes command-level MAIN draw/clear suppression, its export,
saved-work counters and reports, and every parser/default/writer of
`xr_cascade_save_main`. The old key was removed from the installed vrport.ini with
a backup. Both views keep their native cascade clears and draws, while the
corrected matrix synchronization remains active. Per-side draw observation stays
available only behind the diagnostics gate.

## Validation

- Release build passed.
- All four render-parity CTest targets passed, including 3,090 cascade checks:
  counts 1–4, layout changes, per-cascade extent mismatch, 361 rotations and large
  translations per layout, malformed matrices, holes, NaN/Inf and small finite
  extents.
- The modified PowerShell files parse successfully; `git diff --check` passed.
- REDscript compilation succeeded after removing SettingsGuard; the user could
  select the restored Medium range setting in the native menu.
- Final installed DLL SHA256:
  `58FC7A6D6B691519B8DE9F54E8B8ABF92879B90D74C33623263FC29B09CCD560`.
- PID 14920 confirmed that the sampling-pair counter advances at Medium and High
  range. The user confirmed the range changes looked correct.
- The final build passed all four test targets again. Binary inspection verified
  that SaveMain exports, its INI key and the settings-replacement guard string are
  absent. The removed atlas-reuse candidate is not linked into this DLL.
- UserSettings.json was byte-identical before and after deployment. The user
  subsequently confirmed the final installed build works.
