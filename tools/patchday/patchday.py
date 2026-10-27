"""Game update day, in three commands. See docs/patchday-runbook.md.

    py -3.13 -B tools/patchday/patchday.py prepare
        On the build the port works on, before Steam updates it: keep a copy of what the next step
        compares against (exe, address library, shader caches, TweakDB, script bundle, frameworks),
        index everything version-bound the port uses, record the good launch, run the self-tests.

    py -3.13 -B tools/patchday/patchday.py check [--game <dir>]
        After the update has downloaded, before anything is changed: every offline check against the
        new build, one report (build/patchday/check-<exe timestamp>/REPORT.md).

    py -3.13 -B tools/patchday/patchday.py frameworks [--sdk-build]
        Whenever a framework release comes out (RED4ext, CET, redscript, ArchiveXL, TweakXL, Codeware):
        stage the newest of each, check it against what is installed, diff RED4ext.SDK master.

    py -3.13 -B tools/patchday/patchday.py apply --version <x.yz> [--game <dir>] [--no-build]
        Snapshot the new build, rewrite every address and guard in src/ include/ for it, build the DLL,
        and index the new build for the patch after this one. Deploying and the launch are left to the
        runbook's last steps (they need the game closed and the simulator).
"""
import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
PY = [sys.executable, '-B']
sys.path.insert(0, HERE)
import pe  # noqa: E402

SNAP_FILES = ['r6/cache/final.redscripts', 'r6/cache/tweakdb.bin', 'r6/cache/tweakdb_ep1.bin']
SNAP_DIRS = ['engine', 'r6/config']
FRAMEWORK_FILES = ['bin/x64/plugins/cyber_engine_tweaks.asi', 'bin/x64/version.dll', 'bin/x64/winmm.dll',
                   'red4ext/RED4ext.dll', 'engine/tools/scc.exe', 'engine/tools/scc_lib.dll']
FRAMEWORK_DIRS = ['red4ext/plugins/ArchiveXL', 'red4ext/plugins/TweakXL', 'red4ext/plugins/Codeware',
                  'red4ext/plugins/input_loader']
SKIP_BIN = ('CyberpunkVR_', 'cpvr_', 'version.dll', 'winmm.dll', 'openvr_api.dll')


def current_build():
    """(version, timestamp) the source is for, from include/Core/GameBuild.hpp."""
    with open(os.path.join(REPO, 'include', 'Core', 'GameBuild.hpp'), encoding='utf-8') as f:
        text = f.read()
    v = re.search(r'kVersion\s*=\s*"([^"]+)"', text).group(1)
    ts = int(re.search(r'kTimestamp\s*=\s*0x([0-9A-Fa-f]+)', text).group(1), 16)
    return v, ts


def base_dir(version):
    return os.path.join(REPO, 'build', 'patchday', f'base-{version}')


def db_path(ts):
    return os.path.join(HERE, 'data', f'rva_db-{ts:08x}.json')


def exe_identity(game):
    img = pe.Image(pe.find_game_files(game)[0])
    return img.timestamp, img.size_of_image


def run(args, log=None, check=False):
    print('>', ' '.join(str(a) for a in args), flush=True)
    r = subprocess.run([str(a) for a in args], cwd=REPO, capture_output=True, text=True)
    out = (r.stdout or '') + (r.stderr or '')
    if log is not None:
        log.append((' '.join(str(a) for a in args), r.returncode, out))
    tail = out.strip().splitlines()[-6:]
    for ln in tail:
        print('   ', ln)
    if check and r.returncode:
        sys.exit(f'FAILED ({r.returncode}): {args[2] if len(args) > 2 else args}')
    return r.returncode, out


def snapshot(game, dest):
    """What the next patch is compared against: ~0.8 GB, no archives (their TOC is indexed instead)."""
    os.makedirs(os.path.join(dest, 'bin', 'x64'), exist_ok=True)
    src_bin = os.path.join(game, 'bin', 'x64')
    for f in os.listdir(src_bin):
        p = os.path.join(src_bin, f)
        if os.path.isfile(p) and f.endswith(('.exe', '.dll', '.json', '.bin')) and not f.startswith(SKIP_BIN):
            shutil.copy2(p, os.path.join(dest, 'bin', 'x64', f))
    for rel in SNAP_FILES:
        p = os.path.join(game, rel)
        if os.path.exists(p):
            os.makedirs(os.path.dirname(os.path.join(dest, rel)), exist_ok=True)
            shutil.copy2(p, os.path.join(dest, rel))
    for rel in SNAP_DIRS:
        shutil.copytree(os.path.join(game, rel), os.path.join(dest, rel), dirs_exist_ok=True)
    for rel in FRAMEWORK_FILES:
        p = os.path.join(game, rel)
        if os.path.exists(p):
            d = os.path.join(dest, 'frameworks', rel)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(p, d)
    for rel in FRAMEWORK_DIRS:
        p = os.path.join(game, rel)
        if os.path.isdir(p):
            shutil.copytree(p, os.path.join(dest, 'frameworks', rel), dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns('*.log'))
    manifest = dict(game=game, taken=datetime.datetime.now().isoformat(timespec='seconds'),
                    exe=dict(zip(('timestamp', 'size_of_image'), exe_identity(game))))
    acf = os.path.join(os.path.dirname(os.path.dirname(game)), 'appmanifest_1091500.acf')
    if os.path.exists(acf):
        with open(acf, encoding='utf-8', errors='replace') as f:
            m = re.search(r'"buildid"\s+"(\d+)"', f.read())
        manifest['steam_buildid'] = m and m.group(1)
    with open(os.path.join(dest, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=1)
    print(f'snapshot of {game} -> {dest}')


def shipped_dlls(base, game):
    """The SDK DLLs the game ships (Streamline, DLSS, FSR, XeSS, PhysX ...) that a patch replaced.
    The port reaches them through exports, but their structs are versioned (sl::ReflexOptions v1,
    sl::Constants, FFX/XeSS contexts): a changed one needs its hooks re-read."""
    import hashlib

    def sha(p):
        h = hashlib.sha256()
        with open(p, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 20), b''):
                h.update(chunk)
        return h.hexdigest()
    old_bin = os.path.join(base, 'bin', 'x64')
    new_bin = os.path.join(game, 'bin', 'x64')
    out = []
    for f in sorted(os.listdir(old_bin)):
        if not f.endswith('.dll'):
            continue
        n = os.path.join(new_bin, f)
        if not os.path.exists(n):
            out.append(f'GONE     {f}')
        elif sha(n) != sha(os.path.join(old_bin, f)):
            out.append(f'CHANGED  {f}')
    for f in sorted(os.listdir(new_bin)):
        if f.endswith('.dll') and not f.startswith(SKIP_BIN) and not os.path.exists(os.path.join(old_bin, f)):
            out.append(f'NEW      {f}')
    return '\n'.join(out) or 'every game-shipped DLL unchanged'


def prepare(args):
    version, ts = current_build()
    base = base_dir(version)
    game_ts, _ = exe_identity(args.game)
    if game_ts != ts:
        sys.exit(f'The installed game ({game_ts:08x}) is not the build the source is for ({ts:08x}); '
                 f'prepare runs on the working build. Already updated? Use check / apply.')
    if not os.path.exists(os.path.join(base, 'bin', 'x64', 'Cyberpunk2077.exe')):
        snapshot(args.game, base)
    log = []
    run(PY + [os.path.join(HERE, 'inventory.py'), '--game', base, '--out', db_path(ts)], log, check=True)
    run(PY + [os.path.join(HERE, 'archives.py'), 'toc', '--game', args.game, '--out',
              os.path.join(base, 'archive_toc.json.gz')], log, check=True)
    run(PY + [os.path.join(HERE, 'archives.py'), 'overrides', '--toc', os.path.join(base, 'archive_toc.json.gz'),
              '--out', os.path.join(base, 'archive_overrides.json')], log, check=True)
    if not os.path.isdir(os.path.join(base, 'archive_originals')):
        run(PY + [os.path.join(HERE, 'archives.py'), 'originals', '--game', args.game,
                  '--overrides', os.path.join(base, 'archive_overrides.json'),
                  '--out', os.path.join(base, 'archive_originals')], log)
    run(PY + [os.path.join(HERE, 'check_shaders.py'), 'index', '--game', base,
              '--index', os.path.join(HERE, 'data', f'shader_twins-{version}.json')], log, check=True)
    run(PY + [os.path.join(HERE, 'offsets.py'), 'index', '--game', base,
              '--out', os.path.join(HERE, 'data', f'offset_carriers-{version}.json')], log, check=True)
    run(PY + [os.path.join(HERE, 'check_tweaks.py'), '--game', args.game, '--write-baseline',
              '--baseline', os.path.join(HERE, 'data', f'tweak_baseline-{version}.json')], log)
    run(PY + [os.path.join(HERE, 'check_reds.py'), '--game', args.game], log)
    # The frameworks this build was tested with, and the SDK-layout table matching the SDK in use.
    run(PY + [os.path.join(HERE, 'frameworks.py'), 'snapshot', '--game', args.game,
              '--out', os.path.join(HERE, 'data', f'frameworks-{version}.json')], log)
    run(PY + [os.path.join(HERE, 'sdk_layouts.py'), 'gen', '--check'], log)
    if args.save_log_baseline:
        run(PY + [os.path.join(HERE, 'check_logs.py'), '--game', args.game, '--save-baseline',
                  '--baseline', os.path.join(HERE, 'data', f'log_baseline-{version}.json')], log)
    run(PY + [os.path.join(HERE, 'selftest.py')], log)
    bad = [c for c, code, _ in log if code]
    print('PREPARE', 'OK' if not bad else f'{len(bad)} step(s) reported problems: ' + '; '.join(bad))


def check(args):
    version, ts = current_build()
    base = base_dir(version)
    new_ts, new_size = exe_identity(args.game)
    out = os.path.join(REPO, 'build', 'patchday', f'check-{new_ts:08x}')
    os.makedirs(out, exist_ok=True)
    if new_ts == ts and not args.force:
        print(f'The installed game is still {version} ({ts:08x}): nothing to check yet.')
        return
    log = []
    run(PY + [os.path.join(HERE, 'remap.py'), '--db', db_path(ts), '--new', args.game, '--out',
              os.path.join(out, 'remap')], log)
    run(PY + [os.path.join(HERE, 'offsets.py'), 'check', '--new', args.game,
              '--index', os.path.join(HERE, 'data', f'offset_carriers-{version}.json')], log)
    run(PY + [os.path.join(HERE, 'check_reds.py'), '--game', args.game, '--label', f'{new_ts:08x}'], log)
    run(PY + [os.path.join(HERE, 'check_reds.py'), '--game', args.game, '--all', '--label', f'{new_ts:08x}-all'], log)
    run(PY + [os.path.join(HERE, 'reds_diff.py'), '--old', os.path.join(base, 'r6', 'cache', 'final.redscripts'),
              '--new', os.path.join(args.game, 'r6', 'cache', 'final.redscripts')], log)
    run(PY + [os.path.join(HERE, 'check_tweaks.py'), '--game', args.game,
              '--baseline', os.path.join(HERE, 'data', f'tweak_baseline-{version}.json')], log)
    run(PY + [os.path.join(HERE, 'check_settings.py'), '--game', args.game], log)
    run(PY + [os.path.join(HERE, 'check_shaders.py'), 'check', '--game', args.game,
              '--index', os.path.join(HERE, 'data', f'shader_twins-{version}.json')], log)
    run(PY + [os.path.join(HERE, 'archives.py'), 'toc', '--game', args.game, '--out',
              os.path.join(out, 'archive_toc.json.gz')], log)
    run(PY + [os.path.join(HERE, 'archives.py'), 'diff', '--overrides', os.path.join(base, 'archive_overrides.json'),
              '--new-toc', os.path.join(out, 'archive_toc.json.gz'), '--out', os.path.join(out, 'archive_diff.json')], log)
    run(PY + [os.path.join(HERE, 'check_deps.py'), '--game', args.game], log)
    framework_checks(args.game, out, log)
    run(PY + [os.path.join(HERE, 'cet_remap.py'), '--old',
              os.path.join(base, 'frameworks', 'bin', 'x64', 'plugins', 'cyber_engine_tweaks.asi'),
              '--new', os.path.join(args.game, 'bin', 'x64', 'plugins', 'cyber_engine_tweaks.asi')], log)
    log.append(('compare game-shipped DLLs with the snapshot', 0, shipped_dlls(base, args.game)))
    lines = [f'# Patch-day check: {version} ({ts:08x}) -> {new_ts:08x}', '',
             f'- generated {datetime.datetime.now().isoformat(timespec="seconds")}',
             f'- new exe: timestamp {new_ts:08x}, SizeOfImage {new_size:#x}',
             f'- address remap report: `{os.path.join(out, "remap", "report.md")}`', '']
    for cmd, code, text in log:
        name = os.path.basename(cmd.split()[2]) if len(cmd.split()) > 2 else cmd
        lines.append(f'## {name} -- {"OK" if code == 0 else f"exit {code}"}')
        lines.append('')
        lines.append('`' + cmd + '`')
        lines.append('')
        lines.append('```')
        lines += text.strip().splitlines()[-80:]
        lines.append('```')
        lines.append('')
    report = os.path.join(out, 'REPORT.md')
    with open(report, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('REPORT:', report)


def framework_checks(game, out, log):
    """The newest release of every framework staged, checked against what is installed (script API,
    the port's redscript with them, CET), and the RED4ext.SDK master against the submodule."""
    run(PY + [os.path.join(HERE, 'frameworks.py'), 'fetch'], log)
    run(PY + [os.path.join(HERE, 'frameworks.py'), 'check', '--game', game,
              '--report', os.path.join(out, 'frameworks.md')], log)
    run(PY + [os.path.join(HERE, 'sdk_layouts.py'), 'diff'], log)


def frameworks(args):
    """Runbook step 3, again whenever a framework release comes out: fetch, check, SDK diff."""
    out = os.path.join(REPO, 'build', 'patchday', 'frameworks-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    os.makedirs(out, exist_ok=True)
    log = []
    framework_checks(args.game, out, log)
    if args.sdk_build:
        run(PY + [os.path.join(HERE, 'sdk_layouts.py'), 'build'], log)
    bad = [c for c, code, _ in log if code]
    print('report:', os.path.join(out, 'frameworks.md'))
    print('FRAMEWORKS', 'OK' if not bad else f'{len(bad)} step(s) reported problems: ' + '; '.join(bad))


def apply(args):
    version, ts = current_build()
    new_ts, _ = exe_identity(args.game)
    if new_ts == ts and not args.force:
        sys.exit(f'The installed game is still {version}: nothing to apply.')
    new_base = base_dir(args.version)
    if not os.path.exists(os.path.join(new_base, 'bin', 'x64', 'Cyberpunk2077.exe')):
        snapshot(args.game, new_base)
    log = []
    # Exit 2 = some addresses need a human: they stay listed in kUnmapped (never hooked) and the rest is
    # still worth building and launching, so this does not stop here.
    run(PY + [os.path.join(HERE, 'remap.py'), '--db', db_path(ts), '--new', new_base, '--apply',
              '--version', args.version, '--out', os.path.join(REPO, 'build', 'patchday', f'remap-{new_ts:08x}')],
        log)
    run(PY + [os.path.join(HERE, 'offsets.py'), 'check', '--new', new_base, '--apply',
              '--index', os.path.join(HERE, 'data', f'offset_carriers-{version}.json')], log)
    run(PY + [os.path.join(HERE, 'cet_remap.py'), '--old',
              os.path.join(base_dir(version), 'frameworks', 'bin', 'x64', 'plugins', 'cyber_engine_tweaks.asi'),
              '--new', os.path.join(args.game, 'bin', 'x64', 'plugins', 'cyber_engine_tweaks.asi'), '--apply'], log)
    # The next patch compares against this one.
    run(PY + [os.path.join(HERE, 'inventory.py'), '--game', new_base, '--out', db_path(new_ts)], log)
    run(PY + [os.path.join(HERE, 'offsets.py'), 'index', '--game', new_base,
              '--out', os.path.join(HERE, 'data', f'offset_carriers-{args.version}.json')], log)
    run(PY + [os.path.join(HERE, 'archives.py'), 'toc', '--game', args.game, '--out',
              os.path.join(new_base, 'archive_toc.json.gz')], log)
    if not args.no_build:
        run(PY[:1] + [os.path.join(REPO, 'tools', 'build', 'build_stereo.py')], log)
    bad = [c for c, code, _ in log if code]
    print('APPLY', 'OK' if not bad else f'{len(bad)} step(s) reported problems: ' + '; '.join(bad))
    print('next: runbook steps 5-9 (report review, scripts, twins, archive), then patchday.py verify')


HARNESS = os.path.join(REPO, 'build', 'shotgun-reload-20261019', 'harness')


def verify(args):
    """Deploy the DLL (and the given redscript mods), launch to the QuickBoot save in the simulator,
    compare the launch logs with the good build's, close and restore everything."""
    version, _ = current_build()
    log = []
    run([sys.executable, os.path.join(HARNESS, 'deploy.py'), 'dll'], log)
    if args.reds:
        run([sys.executable, os.path.join(HARNESS, 'deploy.py'), 'reds'] + args.reds, log)
    if args.archive:
        run([sys.executable, os.path.join(HARNESS, 'deploy.py'), 'archive'], log)
    code, _ = run([sys.executable, os.path.join(HARNESS, 'launch.py')], log)
    baseline = os.path.join(HERE, 'data', f'log_baseline-{args.baseline or version}.json')
    if not os.path.exists(baseline):
        baseline = os.path.join(HERE, 'data', 'log_baseline-2.31.json')
    lcode, out = run(PY + [os.path.join(HERE, 'check_logs.py'), '--baseline', baseline], log)
    print(out)
    if not args.keep:
        run([sys.executable, os.path.join(HARNESS, 'close.py')], log)
    else:
        print('game left running (--keep); close it with', os.path.join(HARNESS, 'close.py'))
    print('VERIFY', 'OK' if code == 0 and lcode == 0 else 'PROBLEMS (launch exit %d, logs exit %d)' % (code, lcode))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('--game', default=GAME)
    p.add_argument('--save-log-baseline', action='store_true',
                   help='record the newest launch logs as the good baseline (only after a good launch)')
    c = sub.add_parser('check')
    c.add_argument('--game', default=GAME)
    c.add_argument('--force', action='store_true', help='TEST: run against the same build (rehearsal)')
    a = sub.add_parser('apply')
    a.add_argument('--game', default=GAME)
    a.add_argument('--version', required=True)
    a.add_argument('--no-build', action='store_true')
    a.add_argument('--force', action='store_true',
                   help='TEST: apply on the same build (an identity remap: the tree must not change)')
    f = sub.add_parser('frameworks')
    f.add_argument('--game', default=GAME)
    f.add_argument('--sdk-build', action='store_true', help='also a trial build against RED4ext.SDK master')
    v = sub.add_parser('verify')
    v.add_argument('--reds', nargs='*', default=[], help='redscript mods to deploy too (mods/redscript/<name>)')
    v.add_argument('--archive', action='store_true', help='deploy mods/archive/cyberpunkvrport.archive too')
    v.add_argument('--baseline', default=None, help='version of the log baseline (default: the current one)')
    v.add_argument('--keep', action='store_true', help='leave the game running for manual tests')
    args = ap.parse_args()
    {'prepare': prepare, 'check': check, 'apply': apply, 'verify': verify, 'frameworks': frameworks}[args.cmd](args)


if __name__ == '__main__':
    main()
