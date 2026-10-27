# VRCAM for the VR tutorial replacer

Live inspection in PID 18052 identified the current player as:

- PlayerPuppet EntityID 9000752, IsReplacer() true.
- Record `Character.q000_vr_replacer`.
- Template `base/characters/entities/player/replacer/tutorial_replacer.ent`,
  confirmed by both the live ResRef and the record's entityTemplatePath.
- 139 live components, zero VRCAM components. The selector requested
  vrcam_2560x2560 and reported that the player had no vrcam_* components.

The original template is in basegame_4_appearance.archive, with no EP1 or
installed-mod override. Its 111 authored components include the standard
slots/camera hard binding used by the other player replacers.

Added the template to REPLACER_FILES in tools/gen_vrcam_assets.py and generated
62 cameras with the existing vrcam_braindance_ prefix. Their virtual camera names
remain vrcam_feed_<W>x<H>, all ship disabled, and every dynamic texture already
exists. No native hook, selector branch, or extra texture was required.

Validation passed for unique CRUIDs, handle references, all original component
and template fields, and semantic CR2W -> JSON roundtrip equality. The generator
adds nothing on a second pass over the tutorial template. The resulting template
has 173 authored components.

The full project generator run also reported an unrelated existing Johnny raw
source CruidDict mismatch (167 entries for 169 chunks). That raw file was not
changed. Tutorial generation/roundtrip was verified independently; packing starts
from the already installed archive rather than recompiling unrelated raw sources.
All 316 previous archive payloads remain byte-identical. The repacked archive was
extracted again and all 317 entries checked against their source bytes.

The user authorized closing the game and installing the archive. Updated the
WolvenKit raw/cooked tutorial source, repository raw source, and the project,
repository, and game copies of cyberpunkvrport.archive. User settings and
calibration hashes were unchanged. The game was left closed; on-device VRCAM
validation requires returning to the tutorial after the next launch.

Archive SHA-256:
`EDED1057AA7DF6E8DA31CB77F9BF06ABC7B06B675F852C8FF3E2AEFB7D31445C`.
Evidence, original archive, roundtrip, and deployment receipt:
`build/tutorial-replacer-20260928/`, with deployment under `deploy-213941/`.
