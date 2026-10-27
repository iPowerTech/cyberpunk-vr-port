"""One flat launch of the game with a single RED4ext plugin (all other plugins ignored), until what it writes stops
growing; then the game is closed and red4ext\\config.ini and the plugin folder are put back exactly as they were.

    py -3.13 -B tools/patchday/plugin_run.py --dll <plugin.dll> --watch "<glob under the game folder>"
                                             [--out <dir>] [--stable 20] [--timeout 900]

Patch-day probes (tools/rtti_dump, RED4.RTTIDumper, SDK self-tests) run this way: nothing else of the mod setup
is loaded, so a probe sees the bare game. Before launching, CPU-heavy background jobs must not starve the game:
idat.exe processes are set to Idle priority (with 14 Diaphora exports at Normal the 2.5 init livelocked into the
120 s watchdog). --watch is matched relative to the game folder (e.g. "bin/x64/dumps/**/*"); once its total size
has not changed for --stable seconds the run ends and the matches are copied to --out.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'


def running(name):
    r = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {name}', '/FO', 'CSV', '/NH'], capture_output=True, text=True)
    return name.lower() in r.stdout.lower()


def idle_heavy_jobs():
    subprocess.run(['powershell', '-NoProfile', '-Command',
                    "Get-Process idat -ErrorAction SilentlyContinue | ForEach-Object { $_.PriorityClass = 'Idle' }"],
                   capture_output=True)


def watched(pattern):
    files = [f for f in glob.glob(os.path.join(GAME, pattern), recursive=True) if os.path.isfile(f)]
    return files, sum(os.path.getsize(f) for f in files)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dll', required=True)
    ap.add_argument('--name', help='plugin folder name (default: the DLL name)')
    ap.add_argument('--watch', required=True)
    ap.add_argument('--out')
    ap.add_argument('--stable', type=int, default=20)
    ap.add_argument('--timeout', type=int, default=900)
    a = ap.parse_args()
    if running('Cyberpunk2077.exe'):
        sys.exit('game already running')
    name = a.name or os.path.splitext(os.path.basename(a.dll))[0]
    plugins = os.path.join(GAME, 'red4ext', 'plugins')
    cfg = os.path.join(GAME, 'red4ext', 'config.ini')
    plug_dir = os.path.join(plugins, name)
    orig = open(cfg, encoding='utf-8').read()
    others = [d for d in os.listdir(plugins) if os.path.isdir(os.path.join(plugins, d)) and not d.startswith('__') and d != name]
    new = orig.replace('ignored = []', 'ignored = [' + ', '.join(f'"{d}"' for d in others) + ']')
    if new == orig:
        sys.exit('red4ext/config.ini has no "ignored = []" line; not touching it')
    before = set(watched(a.watch)[0])
    idle_heavy_jobs()
    try:
        with open(cfg, 'w', encoding='utf-8') as f:
            f.write(new)
        os.makedirs(plug_dir, exist_ok=True)
        shutil.copy2(a.dll, os.path.join(plug_dir, os.path.basename(a.dll)))
        exe = os.path.join(GAME, 'bin', 'x64', 'Cyberpunk2077.exe')
        p = subprocess.Popen([exe, '-skipStartScreen'], cwd=os.path.dirname(exe))
        print('launched', p.pid, 'with only', name, flush=True)
        t0 = time.monotonic()
        last = (None, t0)
        while time.monotonic() - t0 < a.timeout:
            if p.poll() is not None:
                print('game exited', p.returncode, flush=True)
                break
            files, size = watched(a.watch)
            fresh = [f for f in files if f not in before]
            if fresh:
                if size != last[0]:
                    last = (size, time.monotonic())
                elif time.monotonic() - last[1] >= a.stable:
                    print(f'output stable: {len(fresh)} files, {size} bytes', flush=True)
                    break
            time.sleep(2)
    finally:
        subprocess.run(['taskkill', '/F', '/IM', 'Cyberpunk2077.exe'], capture_output=True)
        subprocess.run(['taskkill', '/F', '/IM', 'CrashReporter.exe'], capture_output=True)
        time.sleep(3)
        with open(cfg, 'w', encoding='utf-8') as f:
            f.write(orig)
        shutil.rmtree(plug_dir, ignore_errors=True)
        print('restored config.ini:', open(cfg, encoding='utf-8').read() == orig)
    if a.out:
        files, _ = watched(a.watch)
        root = os.path.commonpath([GAME, *files]) if files else GAME
        for f in files:
            if f in before:
                continue
            dst = os.path.join(a.out, os.path.relpath(f, root))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.move(f, dst)
        print('moved to', a.out)


if __name__ == '__main__':
    main()
