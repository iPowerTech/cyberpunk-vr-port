"""Single-pass ceiling in an arbitrary scene of ONE owned launch, in three explicit steps.

  launch  - own the simulator preview size, launch to the QuickBoot save, verify gameplay/HUD,
            leave the game running and record the owned state.
  measure - run the geometry_cost_report intervals against that PID (scene chosen in between,
            e.g. by a CET teleport), with a baseline screenshot per measurement.
  close   - close the owned game, observe crash reporters, restore the preview settings.

Steps refuse to act on a process they did not launch. All probe controls are restored by
geometry_cost_report.run even when an interval fails.
"""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools/quickboot_tests'), str(ROOT / 'tools/roomscale_tests'),
                str(ROOT / 'tools/swimming_tests'), str(Path(__file__).resolve().parent)]
import launch_to_save as boot  # noqa: E402
from live_read import Reader  # noqa: E402
from live_symbols import resolve  # noqa: E402
from preview_settings import PreviewSettings  # noqa: E402
import geometry_cost_report as cost  # noqa: E402

SIMULATOR = Path.home() / 'AppData/Local/OpenXR-Simulator'
DLL = 'red4ext/plugins/CyberpunkVR_Stereo/CyberpunkVR_Stereo.dll'


def load_state(path):
    state = json.loads(path.read_text(encoding='utf-8'))
    if type(state.get('pid')) is not int or state.get('closed'):
        raise RuntimeError('No owned running launch in this state file')
    return state


def checker(pid, started, baseline):
    def check():
        crash = boot.crash_report(pid, started)
        reporters = boot.new_reporters(set(baseline))
        if crash or reporters:
            boot.close_reporters(reporters)
            raise RuntimeError(crash or 'New CrashReporter during the measurement')
        if pid not in boot.process_ids('Cyberpunk2077.exe'):
            raise RuntimeError('Owned game exited')
    return check


def screenshot(path, check):
    request = SIMULATOR / 'screenshot_request.json'
    if request.exists():
        raise RuntimeError('Another screenshot request is pending')
    requested = time.time()
    request.write_text(json.dumps(dict(eye='both', layer='all')))
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        check()
        status = SIMULATOR / 'screenshot_status.json'
        if status.exists() and status.stat().st_mtime >= requested:
            path.write_bytes((SIMULATOR / 'screenshot.bmp').read_bytes())
            return str(path)
        time.sleep(.1)
    raise TimeoutError('No fresh simulator screenshot')


def launch(args):
    if boot.process_ids('Cyberpunk2077.exe'):
        raise RuntimeError('Close the game first; this step owns a fresh launch')
    args.state.parent.mkdir(parents=True, exist_ok=True)
    preview = PreviewSettings(SIMULATOR / 'settings.json')
    started = time.time()
    baseline = sorted(boot.reporter_pids() | boot.reporter_window_pids())
    state = dict(started=started, baselineReporters=baseline, preview=preview.start(),
                 previewOriginal=preview.original_bytes.decode('utf-8'))
    args.state.write_text(json.dumps(state, indent=2), encoding='utf-8')
    import subprocess
    process = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/quickboot_tests/launch_to_save.py'), '--timeout', str(args.timeout)],
                             capture_output=True, text=True, errors='replace', timeout=args.timeout + 30)
    ready = None
    for line in process.stderr.splitlines()[-20:]:
        print('[launch stderr]', line, flush=True)
    for line in process.stdout.splitlines():
        print(line, flush=True)
        if line.startswith('Launched PID '):
            state['pid'] = int(line.split()[-1])
        if line.startswith('{'):
            value = json.loads(line)
            if value.get('ready'):
                ready = value
    args.state.write_text(json.dumps(state, indent=2), encoding='utf-8')
    if 'pid' not in state:
        raise RuntimeError('Game was not launched')
    hud = (ready or {}).get('stableHud', {})
    if (not ready or ready.get('pid') != state['pid'] or not ready.get('focusSequence', {}).get('verified') or
            hud.get('stableMilliseconds', 0) < 1000 or hud.get('last', {}).get('menu') != 0):
        raise RuntimeError('Startup did not verify gameplay and a stable HUD; close this launch')
    state['startup'] = dict(stableHud=hud)
    args.state.write_text(json.dumps(state, indent=2), encoding='utf-8')
    print(json.dumps(dict(ready=True, pid=state['pid'])), flush=True)


def measure(args):
    state = load_state(args.state)
    pid = state['pid']
    game = boot.DEFAULT_GAME
    check = checker(pid, state['started'], state['baselineReporters'])
    check()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    symbols = resolve(pid, game / DLL, list(cost.NAMES))
    reader = Reader(pid, game / 'bin/x64/Cyberpunk2077.exe', 0, 0)
    try:
        probe = cost.LiveProbe(reader, symbols, pid, check)
        before = screenshot(args.out.with_suffix('.scene.bmp'), check)
        images = {}

        def image(row):
            if row['control']:
                images[row['label']] = screenshot(args.out.with_name(f"{args.out.stem}-{row['label']}.bmp"), check)
        results, cleanup = cost.run(probe, game / 'bin/x64/vrport.ini', on_interval=image if args.images else None)
        value = dict(pid=pid, dll=symbols['sha256'], label=args.label, scene=before, images=images, cleanup=cleanup, results=results)
        args.out.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
        cost.validate(results)
        if any(cleanup.values()):
            raise RuntimeError('Geometry cost controls were not restored')
        value['ceiling'] = cost.ceiling(results)
        args.out.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(dict(label=args.label, ceiling=value['ceiling'],
                              fps={row['label']: round(row['frameRate']['fps'], 2) for row in results}), indent=2), flush=True)
    finally:
        reader.close()


def close(args):
    state = json.loads(args.state.read_text(encoding='utf-8'))
    result = {}
    pid = state.get('pid')
    if pid and not state.get('closed'):
        from run_launch_tests import close_game, observe_shutdown
        result['shutdown'] = close_game(pid, boot.DEFAULT_GAME / 'bin/x64/Cyberpunk2077.exe')
        result['shutdownObservation'] = observe_shutdown(pid, state['started'], set(state['baselineReporters']))
    boot.close_reporters(boot.new_reporters(set(state['baselineReporters'])))
    if boot.process_ids('Cyberpunk2077.exe'):
        raise RuntimeError('A game process remains; preview restoration deferred')
    preview = PreviewSettings(SIMULATOR / 'settings.json')
    preview.original_bytes = state['previewOriginal'].encode('utf-8')
    preview.original = json.loads(preview.original_bytes)
    result['previewRestored'] = preview.restore()
    state.update(closed=True, close=result)
    args.state.write_text(json.dumps(state, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='step', required=True)
    for name in ('launch', 'measure', 'close'):
        step = sub.add_parser(name)
        step.add_argument('--state', type=Path, required=True)
        if name == 'launch':
            step.add_argument('--timeout', type=float, default=180)
        if name == 'measure':
            step.add_argument('--out', type=Path, required=True)
            step.add_argument('--label', required=True)
            step.add_argument('--images', action='store_true')
    args = parser.parse_args()
    dict(launch=launch, measure=measure, close=close)[args.step](args)


if __name__ == '__main__':
    main()
