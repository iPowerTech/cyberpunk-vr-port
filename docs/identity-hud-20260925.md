# q304 identity imprint and dossier in the texture HUD

In PID31716 the player is the Bella replacer. The active entity now has all
62 VRCAM variants and the selector enables exactly one (2560x2560), without an
error. The user confirmed that the replacer stereo works.

Read-only HUD inspection identified two separate `CustomAnimationsHudGameController`
instances rather than a new global HUD or replacement text:

- Entry `q304_dossier` (CName hash17866681522088458331), resource
  `ep1/gameplay/gui/quests/q304/q304_dossier.inkwidget`, root920x1000 in BottomLeft.
  It contains both male/female dossiers, portrait, notes and the native input icon.
- Entry `q304_imprint_active_indicator` (hash10696329027580923616), resource
  `ep1/gameplay/gui/quests/q304/q304_imprint_active_indicator.inkwidget`, root700x150
  in TopLeft. It contains the localized imprint label and active/error/loading states.

Both live entries had `useSeparateWindow=false` and no `inkHudEntryInfo` bounds.
The ordinary HUD controller was hidden in pause; that is why zero XR sprites in
that snapshot is not itself a capture failure. Native entry names/resources
were resolved from the live layer entries and the engine's CName/resource APIs.
WolvenKit's MCP transport disconnected, so subsequent Lua inspection used its
existing CETBridge file protocol directly; no mouse or keyboard control was used.

The existing guarded pre-spawn hook now creates separate windows for these two
exact names. Both route to Main and inherit the normal HUD transform, follow and
visibility policy. The original game controllers still own all text, states,
animations and input. Unrelated custom-animation entries remain unchanged.

Lua supplies capture bounds only for the two matching widget trees once they
have an `inkVirtualWindow` parent. A32px border preserves the dossier icon at
X=-10 and the SC_01 panel's small overflow. It expands the capture rectangle, not
the native window/layout. The normal native-slot mask/fallback remains intact.

Appended `Identity dossier` / `hud_element_identity_dossier` and
`Identity imprint` / `hud_element_identity_imprint` to the layout catalogue,
preserving existing indices and persisted keys.

All34 HUD tests passed, including channel routing, layout persistence, GPU
composition, texture ownership, tracking, style and previous HUD features.
The real Lua module passed the extended lexical fixture through CET: both
identity crops, native layout preservation, unrelated custom-root exclusion,
cold load, replacement, phone/subtitle/loot regressions. Artifacts and rollback
files are under `build/identity-hud-20260925/`. Live window and display validation
follow after restart.

## Additional imprint panels inspected before the combined deployment

The user then showed two more imprint panels and explicitly requested that the
game remain open while they identified further elements. That request was
respected throughout inspection; the user subsequently explicitly requested
closing the game and deploying the combined changes. PID23780 ran the first
identity-HUD candidate DLL `2c9fe7b6121e021702d531a9ef72db068cfb87f594e4671328bbcc692a504ade`.

In this scene the player is the normal `Character.Player_Puppet_Base` and the
two panels belong to `gameuiBriefingGameController`, HUD entry
`briefing_sequence_player` (live index42), resource
`base/gameplay/gui/widgets/hud_briefings/hud_briefings.inkwidget`.
It is distinct from entry `briefing` / `BriefingScreen` (live index18).

Its dynamically loaded3840x2160 root contains `Left` and `Right`, both visible,
and the central `Centre_intro`/`Centre_outro` animation variants. Live text
includes "Оттиск личности", imprint activation and malfunction variants.
Snapshots are `popup-entry-names.json`, `briefing-before.json`, and
`imprint-matches.json` in the same artifact directory. No game progression,
pause state, UI visibility or popup content was modified during inspection.

Prepared the existing separate-window/Main path for `briefing_sequence_player`
and its full-root bounds, retaining both panels and native sequence animations.
Appended `Story briefings / imprint` with key `hud_element_briefing_sequence`.
The original fullscreen briefing menu remains outside this conversion.
Three targeted HUD tests and the updated real-module Lua bounds fixture passed,
including empty content transitions. This addition was held in source until
the user's remaining HUD requests were collected for the combined deployment.

The user clarified that the two right-hand panels show "Аврора Кассель" and
loading/preparation. A second live read (`right-panels.json`) confirmed the
exact descendants under `/Root/Root/Right`: `R_Person_Female_flex/R_Person_Female`
contains "Аврора Кассель", while `R_Booting_systems_flex` and
`Booting_systems_group` contain "Запуск систем", the eight loading/done rows,
percentages and preparation status. The right group occupies
(2990,1267,720,698), entirely within the3840x2160 source already prepared for
capture. The full-root capture includes both panels and their changing states;
no second copy or additional HUD source is needed. The game remained running.

Further user-requested inspection of "Оттиск поведения" / "Оттиск активен"
confirmed two covered forms. The persistent700x150 indicator has
`text_up/imprint_text` = "Оттиск поведения" and `active_group/text_01` =
"Активирован" (with error/deactivated siblings). In PID23780 its parent is
already an `inkVirtualWindow` with bounds(-32,-32,764,214) from the installed
first candidate; the Lua module has no errors. The popup phrase at
`Centre_outro/MessageBox/imprint_active/messageOutro` is part of the previously
recorded briefing sequence and is covered by the pending full-root addition.
These read-only checks are saved as `imprint-behavior.json` and
`imprint-states.json`. Pause/animation visibility was left untouched; no deploy
or game restart was performed.

## Combined deployment

After the user explicitly requested closing the game and deploying, the Release
build completed and PID23780 was terminated. Installed DLL SHA256:
`d4d88b8f80a4081f9ce47685d9bcc504f23625909a3613727614a199c491540f`.
Matching PDB and the updated `hud_panel.lua` were installed; all destination
hashes match their source files. The Bella archive remains current and matches
the repository asset. INI, UserSettings and VRIK calibration hashes are unchanged.
Backup and deployment manifest: `build/identity-hud-20260925/combined-deploy-212623/`.
The game was not relaunched. The combined briefing capture's visual verification
remains for the next game session; earlier test results are recorded above.
