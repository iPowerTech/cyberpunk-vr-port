# Restore native subtitle asset scale

WolvenKit semantic comparison of the installed `cyberpunkvrport.archive` and
the current game `basegame_1_engine.archive` found two authored changes in
`base/gameplay/gui/widgets/subtitles/subtitles.inkwidget`: `subtitlesPanel`
render scale 0.7 on both axes, and the parent's bottom margin 270 instead of 70.
WolvenKit JSON represents each change in both embedded-package views.

Restored only scale X/Y to 1.0 in both representations, then wrote the CR2W
through WolvenKit and compared the binary back to the base game. The only
remaining differences are the existing margin, as requested. The source CR2W
and raw JSON under `C:/Users/dariulone/Documents/CyberpunkVRPort` are updated.

The project's old packed archive had incorrect `source/archive/` prefixes and
included raw JSON. Its source tree also differs from the installed archive
(locomotion assets and a skeleton). To keep this change limited to subtitles,
the new archive was packed from the installed archive's extracted contents,
replacing only the edited subtitle resource. All 315 depot paths match the
installed manifest exactly; no raw JSON or project prefix is packed. Updated
the project's packed output, the repository's `mods/archive` release asset and
the installed archive. The prior repository asset and installed copy had the
same SHA256 before replacement. Extracting both finished archives and
comparing all payload SHA256 values confirmed that only `subtitles.inkwidget`
changed; the other 314 files are byte-identical.

Archive SHA256:
`b6876cb06623bc01b9b328b24cdc9040a8a40cb644e382dab5783ac784384fb6`.
Backups and conversion/build artifacts are under the repository's ignored
`build/preview-subtitles-20260925/`. After restarting, the user confirmed that
the subtitle change and save-preview fix work on 2026-09-25.
