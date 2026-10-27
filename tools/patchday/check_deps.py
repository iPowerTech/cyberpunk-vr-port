"""The frameworks the port runs on: installed version against the newest release.

    py -3.13 -B tools/patchday/check_deps.py [--game <dir>] [--offline]

After a game patch RED4ext, CET, redscript, ArchiveXL, TweakXL and Codeware each need a release for the
new build (usually within days), and the port's own build needs the RED4ext.SDK that matches the new
RED4ext. This prints, for each, what is installed (file version, or the date for scc which carries none),
the newest GitHub release and its date, so "is everything out yet" is one command. GitHub's REST API,
unauthenticated (60 requests an hour; this makes eight).

Also: the port reads CET's internals at fixed RVAs (Render/NativeTargetLifetime.hpp: CetResizeRva,
CetResetRva, CetShutdownRva + FNV guards, made against CET 1.37.1). A new CET leaves those guards
failing -- the swapchain-target lifetime tracking then stays off, by design -- until they are re-found
in the new cyber_engine_tweaks.asi (the old one is kept in build/patchday/base-2.31/frameworks).
"""
import argparse
import datetime
import json
import os
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
DEPS = [
    ('RED4ext', 'wopss/RED4ext', 'red4ext/RED4ext.dll'),
    ('Cyber Engine Tweaks', 'maximegmd/CyberEngineTweaks', 'bin/x64/plugins/cyber_engine_tweaks.asi'),
    ('redscript', 'jac3km4/redscript', 'engine/tools/scc_lib.dll'),
    ('ArchiveXL', 'psiberx/cp2077-archive-xl', 'red4ext/plugins/ArchiveXL/ArchiveXL.dll'),
    ('TweakXL', 'psiberx/cp2077-tweak-xl', 'red4ext/plugins/TweakXL/TweakXL.dll'),
    ('Codeware', 'psiberx/cp2077-codeware', 'red4ext/plugins/Codeware/Codeware.dll'),
    ('Input Loader', 'jackhumbert/cyberpunk2077-input-loader', 'red4ext/plugins/input_loader/input_loader.dll'),
]


def file_version(path):
    if not os.path.exists(path):
        return 'NOT INSTALLED'
    ps = ("(Get-Item -LiteralPath '{}').VersionInfo.ProductVersion".format(path.replace("'", "''")))
    r = subprocess.run(['powershell.exe', '-NoProfile', '-Command', ps], capture_output=True, text=True, timeout=60)
    v = r.stdout.strip()
    stamp = datetime.datetime.fromtimestamp(os.path.getmtime(path)).strftime('%Y-%m-%d')
    return f'{v} ({stamp})' if v else f'? ({stamp})'


def latest(repo):
    req = urllib.request.Request(f'https://api.github.com/repos/{repo}/releases/latest',
                                 headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'cpvr-patchday'})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            j = json.load(r)
        return j.get('tag_name', '?'), (j.get('published_at') or '')[:10]
    except Exception as e:  # noqa: BLE001 -- a report line, not a failure
        return f'(unavailable: {e.__class__.__name__})', ''


def sdk_state():
    sdk = os.path.join(REPO, 'externals', 'RED4ext.SDK')
    r = subprocess.run(['git', '-C', sdk, 'log', '-1', '--format=%h %cs %s'], capture_output=True, text=True)
    return r.stdout.strip() or '?'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=GAME)
    ap.add_argument('--offline', action='store_true')
    args = ap.parse_args()
    exe = os.path.join(args.game, 'bin', 'x64', 'Cyberpunk2077.exe')
    print(f"game: {file_version(exe)}")
    print(f"{'framework':22s} {'installed':28s} {'newest release':30s}")
    for name, repo, rel in DEPS:
        inst = file_version(os.path.join(args.game, rel))
        tag, date = ('-', '') if args.offline else latest(repo)
        print(f'{name:22s} {inst:28s} {tag} {date}')
    print(f"RED4ext.SDK (externals): {sdk_state()}")
    if not args.offline:
        tag, date = latest('wopss/RED4ext.SDK')
        print(f"RED4ext.SDK newest release: {tag} {date} (the SDK mostly moves on master: "
              f"git -C externals/RED4ext.SDK fetch && git log --oneline HEAD..origin/master)")


if __name__ == '__main__':
    main()
