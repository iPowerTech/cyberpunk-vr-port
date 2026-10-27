"""Write <diff>/review_unattributed.md: how the functions the first classification left without a subsystem were
resolved, what they turned out to be, and the hand-read verdicts (review_unattributed.tsv).

    py -3.13 -B tools/patchday/diaphora_review.py [--diff build/patchday/diaphora-2.5] [--v1 functions.v1.tsv]
"""
import argparse
import collections
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PD = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'build', 'patchday')

FINDINGS = """
## What they turned out to be

- **Wwise rebuilt at Demute.** Every Wwise plugin factory and parameter-block constructor changed only in its
  allocation tag: the source path went from `E:\\R6.Main\\dev\\external\\wwise\\...` to
  `C:\\Users\\DEMUTE-PROG2\\Perforce\\florian.heynen_DEMUTE-PROG2_7150\\dev\\external\\wwise\\...` (Demute is a sound
  studio). The DSP code around it (AkFDNReverb channel variants, compressor, flanger, harmonizer, pitch shifter,
  room verb, Vorbis codebook decoders with SSE) is a recompile: instruction or control-flow deltas, same
  algorithms. The keyword pass missed them because func_strings keeps 80 characters, which cuts the new path at
  `external\\wwi`.
- **MSVC CRT / STL update.** printf's `output_processor` state machine, `__crt_strtox`, `wctype` / `_wcsnicmp`,
  `_wfsopen` mode handling, `_invalid_parameter_internal` replacing `_invalid_parameter_noinfo` and new exception copy
  constructors (`fmt::v12::format_error`, `utf8::exception` / `invalid_utf8` / `not_enough_room`,
  `std::ios_base::failure`, `std::bad_weak_ptr`). That is a newer toolset and two new header libraries (fmt 12,
  utf8cpp).
- **C++ exception funclets.** Hundreds of the added functions at 0x23xxxxx are `dtor$N` unwind handlers: test a
  state bit, clear it, free or destroy one local. They have no callers; their owner is reached through the FH4 tables
  (unwind map <- FuncInfo4 <- UNWIND_INFO handler data <- .pdata), and they take its subsystem.
- **Dynamic initializers of global constants.** Thousands of small added functions in the CRT initializer tables
  (`__xc_a..__xc_z`, 23,673 + 1,348 + 1,126 slots) build one global each and register its destructor with `atexit`:
  - mod.io SDK REST endpoint descriptors, one per endpoint: `/games/{game-id}/mods/{mod-id}/...`, `/me/...`,
    `/users/{user-id}`, `/oauth`, `/external`, `/metrics`, `/agreements`, `/authenticate`, `/ping`, `/report`;
  - config variables: `Editor/Denoising/ReBLUR/*` (NRD denoiser radii, history), `Odyssey3D/AO`, dynamic resolution
    (`FixedTargetFrameTime`, `GpuTargetTime_VarBufferScale`, `AdaptToCpu_GpuClamp`, `TargetTimeScale`),
    `AntilagIntensity`;
  - per-translation-unit copies of header constants: CNames `CPO`, `DisabledContent`, `notQueryable`, `abstract`,
    `None`, the quest-graph debugger command strings (`Toggle breakpoint`, `Execute single node`, `Kill signal`,
    `Awake signal`), interaction prefixes (`Interactions.`, `InteractionChoice`);
  - a Winsock init guard in 33 translation units (refcounted `WSAStartup(2.0)` + cleanup at exit, the shape of
    asio's `winsock_init` header object, new with the networking code).
  In the table they sit in translation-unit order, so a slot without evidence of its own takes the subsystem its
  tagged neighbours agree on.
- **Native JSON tweaks with TweakXL's operators.** The TweakDB loader (`sub_1420F43D0`, tweakDB.cpp) now goes
  `sub_14210A360` (binary blob) -> `sub_142120C50`, which walks the game services' list of directories, takes their
  `tweaks` subfolder (`unk_143F70A88` = "tweaks") and every `*.json` in it -> `sub_14211F210` (`$type` / `$value`
  values) -> `sub_14211E670` (`.iconPath`, `relpaceInlineName` -- sic) -> `sub_14211E200`, which applies `!append`,
  `!append-once`, `!append-from`, `!prepend`, `!prepend-once`, `!prepend-from`, `!merge`, `!remove`, `!remove-all`.
  2.5 reads TweakXL-style patches itself at load time; worth a live check against TweakXL (same records patched twice,
  load order).
- **Smaller new constants:** CName `GyroAimingSensitivityVerticalStyleDualGrip` (gyro aiming setting), garment
  CNames `partial_hide`, `BodySuit`, `LongSleeve`, appearance CNames `empty_appearance_default`,
  `appearance_expansion_source`, `boundingBox`, `gender`.
"""

METHOD = """
## How

The first pass (functions.v1.tsv) used string keywords, imported names, tiny bodies and call-graph votes. It left
3,663 changed functions without a subsystem, and its votes had a flaw: a utility called from thousands of places
(`sub_14019DCD4`, 2,702 callers) got its own subsystem from a 9-to-7 vote of its neighbours, and every caller with no
other evidence inherited that. Now a function with more than 50 callers passes on no subsystem it only inherited,
which changed the subsystem of {moved} of the {changed} changed functions against v1 and made the indirect evidence
below necessary for many more of them.

IDA (ida/scripts/refs_of.py) records for every function still without a subsystem how the binary reaches it, and the
classifier uses, in this order:

1. its own IDA name (FLIRT / RTTI names such as `AK::SoundEngine::...`);
2. the owner of a C++ exception funclet (FH4 chain to .pdata);
3. the class of a vtable holding it: MSVC RTTI, or for REDengine classes the class registered behind the table's
   native-type slot, by module prefix (`vehicle*`, `ink*`, `quest*`, `anim*`, `Px*`, ...);
4. the constructor that installs that vtable;
5. the function taking its address (callbacks, jobs);
6. call-graph neighbours again, hubs excluded;
7. CRT initializer table neighbours (same translation unit);
8. address neighbours on both sides that agree (same object file);
9. what is still left was read here function by function (Hex-Rays) -- the table at the end.

Keywords were added for what the reading showed: the truncated Wwise path, mod.io endpoints, NRD denoiser / dynamic
resolution / AntiLag config names, the YAML tweak operators, and a `runtime / third-party libraries` class (CRT, STL,
ConcRT, fmt, utf8cpp).
"""


def load(path):
    with open(path, encoding='utf-8', newline='\n') as f:
        lines = f.read().split('\n')
    cols = lines[0].split('\t')
    return [dict(zip(cols, l.split('\t'))) for l in lines[1:] if l]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--diff', default=os.path.join(PD, 'diaphora-2.5'))
    ap.add_argument('--v1', default='functions.v1.tsv')
    a = ap.parse_args()
    rows = load(os.path.join(a.diff, 'functions.tsv'))
    v1 = {(r['status'], r['old'], r['new']): r for r in load(os.path.join(a.diff, a.v1))}
    key = lambda r: (r['status'], r['old'], r['new'])
    changed = [r for r in rows if r['kind'] not in ('identical', 'addresses-only')]
    was = [r for r in changed if v1[key(r)]['subsystem'] == 'unattributed']
    moved = sum(1 for r in changed if v1[key(r)]['subsystem'] not in ('unattributed', r['subsystem']))
    left = [r for r in changed if r['subsystem'] == 'unattributed']
    md = ['# Functions the first classification left without a subsystem', '',
          'v1 (functions.v1.tsv): %d changed functions without a subsystem. Now: %d. Functions without one at all: %d, '
          'all unchanged (%s).' % (len(was), len(left), sum(r['subsystem'] == 'unattributed' for r in rows),
                                   dict(collections.Counter(r['kind'] for r in rows if r['subsystem'] == 'unattributed')))]
    md.append(METHOD.format(moved=moved, changed=len(changed)))
    md += ['## The %d from v1, now' % len(was), '', '| subsystem | functions |', '|---|---|']
    md += ['| %s | %d |' % kv for kv in collections.Counter(r['subsystem'] for r in was).most_common()]
    md += ['', '| evidence | functions |', '|---|---|']
    md += ['| %s | %d |' % kv for kv in collections.Counter(r['evidence'] for r in was).most_common()]
    md.append(FINDINGS)
    verdicts = load(os.path.join(a.diff, 'review_unattributed.tsv'))
    md += ['## Read by hand (%d)' % len(verdicts), '',
           'No rule above reached these; each was read from its Hex-Rays code (review_pseudocode_left_2.5.json).', '',
           '| rva | build | kind | subsystem | what it is |', '|---|---|---|---|---|']
    md += ['| %s | %s | %s | %s | %s |' % (v['rva'], v['build'], v['kind'], v['subsystem'], v['verdict'].replace('|', '/'))
           for v in sorted(verdicts, key=lambda v: (v['subsystem'], v['verdict'], int(v['rva'], 16)))]
    md += ['', '## Limits', '',
           '- A subsystem is evidence, not a proof: the `evidence` and `context` columns of functions.tsv say which rule '
           'decided it. Call-graph and table-neighbour votes are the weakest; per-TU copies of header constants take the '
           'subsystem of the file they are compiled into.',
           '- Change kinds come straight from the Diaphora exports and do not depend on any of this.']
    open(os.path.join(a.diff, 'review_unattributed.md'), 'w', encoding='utf-8').write('\n'.join(md) + '\n')
    print('v1 %d -> %d left; moved %d of %d; verdicts %d' % (len(was), len(left), moved, len(changed), len(verdicts)))


if __name__ == '__main__':
    main()
