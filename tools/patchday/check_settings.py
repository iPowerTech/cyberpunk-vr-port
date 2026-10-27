"""The game settings the port names must still exist, with the same type, in the game's settings schema.

    py -3.13 -B tools/patchday/check_settings.py [--game <dir>]

Two ways a renamed or removed setting hurts after a patch:
  * SettingsSystem:GetVar with a name the game does not have is an engine assert -- the game dies,
    pcall does not help (memory cet-getvar-crash). The CET mods call GetVar with literal names.
  * mods/config/UserSettings.json is copied over the player's own on the first VR launch
    (Core/FirstLaunch.cpp). Its "version" must be the schema's, and every value in it must name a
    setting of that type, or the game resets or misreads the player's settings.

The schema is the game's r6/config/settings/options.json + platform/pc/options.json.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
GETVAR = re.compile(r'GetVar\(\s*(?:n)?["\']([^"\']+)["\']\s*,\s*(?:n)?["\']([^"\']+)["\']')


def schema(game):
    out, version = {}, None
    for rel in ('r6/config/settings/options.json', 'r6/config/settings/platform/pc/options.json'):
        p = os.path.join(game, rel)
        if not os.path.exists(p):
            continue
        with open(p, encoding='utf-8') as f:
            j = json.load(f)
        version = j.get('version', version)
        for g in j.get('options', []):
            for o in g.get('options', []):
                out[(g['group_name'], o['name'])] = o.get('type')
    return out, version


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=GAME)
    args = ap.parse_args()
    vars_, version = schema(args.game)
    bad = 0
    print(f'schema version {version}, {len(vars_)} settings')
    for dp, _, files in os.walk(os.path.join(REPO, 'mods')):
        for f in files:
            if not f.endswith(('.lua', '.reds')):
                continue
            path = os.path.join(dp, f)
            with open(path, encoding='utf-8', errors='replace') as fh:
                for ln, line in enumerate(fh, 1):
                    for m in GETVAR.finditer(line):
                        key = (m.group(1), m.group(2))
                        where = f"{os.path.relpath(path, REPO).replace(os.sep, '/')}:{ln}"
                        if key not in vars_:
                            print(f'  GetVar MISSING {key[0]} {key[1]}  <- {where}  (an engine assert: crash)')
                            bad += 1
                        else:
                            print(f'  GetVar ok      {key[0]} {key[1]}  <- {where}')
    shipped = os.path.join(REPO, 'mods', 'config', 'UserSettings.json')
    with open(shipped, encoding='utf-8') as f:
        ours = json.load(f)
    if ours.get('version') != version:
        print(f"  UserSettings.json version {ours.get('version')} != schema {version}: refresh it from a "
              f"flat launch of the new build")
        bad += 1
    n = 0
    for g in ours.get('data', []):
        for o in g.get('options', []):
            n += 1
            key = (g['group_name'], o['name'])
            if key not in vars_:
                print(f'  UserSettings.json: {key[0]} {key[1]} no longer exists')
                bad += 1
            elif vars_[key] and o.get('type') and vars_[key] != o['type']:
                print(f"  UserSettings.json: {key[0]} {key[1]} is {vars_[key]} now (ours: {o['type']})")
                bad += 1
    print(f'UserSettings.json: {n} values checked')
    print('SETTINGS', 'OK' if not bad else f'{bad} PROBLEMS')
    sys.exit(0 if not bad else 1)


if __name__ == '__main__':
    main()
