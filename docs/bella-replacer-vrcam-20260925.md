# VRCAM on the q304 Bella player replacer

Live inspection with WolvenKit/CET (PID 17916) identified:

- PlayerPuppet EntityID `9016068`, `IsReplacer() == true`.
- Record `Character.q304_netrunner_replacer_bella`.
- Template `ep1/characters/entities/player/replacer/bella_replacer.ent`,
  confirmed by both the live entity and the TweakDB record's template path.
- Appearance `bella_replacer_fpp`.
- Existing selector wanted `vrcam_2560x2560`, but reported zero VRCAM components
  and `no vrcam_* components on the player entity`.

Read the original template from `ep1_2_gamedata.archive`. Its 118 native
components include the same `slots/camera` binding used by Johnny, with
`Torso_fppCamera_Aim_JNT` as the camera-slot bone. Added the 62 resolution
variants already authored for `johnny_silverhand_replacer.ent`, using the
existing generator and its `vrcam_braindance_` replacer prefix. The virtual view
names remain `vrcam_feed_<W>x<H>` and reuse the existing dynamic textures.
All variants are authored disabled; the existing entity-aware selector enables
only the chosen one and releases the previous player's cameras.

Added Bella to `REPLACER_FILES` so future asset regeneration retains the set.
No additional prefix, selector branch, native hook or resolution was needed.
The original template and Johnny reference JSON, generated JSON, roundtrip
JSON, manifest and packed archive are in `build/netrunner-replacer-20260925/`.

WolvenKit serialization/deserialization checks passed: 62 cameras, unique
component CRUIDs, valid handle references, exact field/binding parity with
Johnny, and all 118 original components and other template fields unchanged.
The other 315 archive payloads remain byte-identical; the new template is the
only added resource. Existing dynamic textures all resolve in the archive.
Running the generator again would add zero components.

Updated the WolvenKit project's source CR2W/raw JSON and packed archive,
repository release archive and game-installed archive. Archive SHA256:
`b763dd8c9b543a6862527fad19db1364b4cd7f1a0dd552cad5bdfb98fb6cb09a`.
After restarting in Bella, the user confirmed that VRCAM works.
