"""Has the game updated, and are the frameworks for it out yet?

    py -3.13 -B tools/patchday/watch.py                 # one look
    py -3.13 -B tools/patchday/watch.py --loop 30       # every 30 minutes, a message box on any change

The game: Steam's appmanifest_1091500.acf (buildid, and whether an update is queued or downloading)
against the snapshot's manifest.json, and the installed exe's PE timestamp against include/Core/
GameBuild.hpp. The frameworks: the newest GitHub release of each against the one seen last time
(build/patchday/watch_state.json), so a release for the new build is noticed the hour it appears.
Read-only: it changes nothing in the game or Steam.
"""
import argparse
import ctypes
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import check_deps  # noqa: E402
import patchday  # noqa: E402

STEAMAPPS = r'C:\Program Files (x86)\Steam\steamapps'
STATE = os.path.join(REPO, 'build', 'patchday', 'watch_state.json')


def steam_state():
    p = os.path.join(STEAMAPPS, 'appmanifest_1091500.acf')
    with open(p, encoding='utf-8', errors='replace') as f:
        text = f.read()
    get = lambda k: (re.search(r'"' + k + r'"\s+"([^"]*)"', text) or [None, None])[1]  # noqa: E731
    return dict(buildid=get('buildid'), target=get('TargetBuildID'), flags=get('StateFlags'),
                to_download=get('BytesToDownload'), downloaded=get('BytesDownloaded'))


def look():
    version, ts = patchday.current_build()
    events = []
    st = steam_state()
    snap = os.path.join(patchday.base_dir(version), 'manifest.json')
    snap_build = None
    if os.path.exists(snap):
        with open(snap, encoding='utf-8') as f:
            snap_build = json.load(f).get('steam_buildid')
    exe_ts, _ = patchday.exe_identity(patchday.GAME)
    print(f'game: Steam build {st["buildid"]} (snapshot {snap_build}), exe {exe_ts:08X} (port made for {ts:08X}), '
          f'StateFlags {st["flags"]}, target {st["target"]}')
    if exe_ts != ts:
        events.append(f'The game exe changed ({ts:08X} -> {exe_ts:08X}): run patchday.py check')
    elif snap_build and st['buildid'] != snap_build:
        events.append(f'Steam build {snap_build} -> {st["buildid"]} with the same exe (data-only update?): '
                      f'run patchday.py check --force')
    if st['target'] not in (None, '0') and st['target'] != st['buildid']:
        events.append(f'Steam has an update queued/downloading: build {st["buildid"]} -> {st["target"]}')
    seen = {}
    if os.path.exists(STATE):
        with open(STATE, encoding='utf-8') as f:
            seen = json.load(f)
    now = {}
    for name, repo, _ in check_deps.DEPS:
        tag, date = check_deps.latest(repo)
        now[name] = tag
        print(f'  {name:22s} newest {tag} {date}')
        if tag.startswith('(') or name not in seen:
            continue
        if seen[name] != tag:
            events.append(f'{name} released {tag} ({date}), was {seen[name]}')
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    merged = dict(seen)
    merged.update({k: v for k, v in now.items() if not v.startswith('(')})
    with open(STATE, 'w', encoding='utf-8') as f:
        json.dump(merged, f, indent=1)
    for e in events:
        print('EVENT', e)
    return events


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--loop', type=float, default=0, help='minutes between looks; 0 = once')
    args = ap.parse_args()
    while True:
        events = look()
        if events and args.loop:
            ctypes.windll.user32.MessageBoxW(None, '\n'.join(events), 'CyberpunkVRPort patch watch', 0x40040)
        if not args.loop:
            sys.exit(1 if events else 0)
        time.sleep(args.loop * 60)


if __name__ == '__main__':
    main()
