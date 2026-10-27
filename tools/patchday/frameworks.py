"""The frameworks the port stands on, moved to their next releases on evidence instead of hope.

    py -3.13 -B tools/patchday/frameworks.py fetch [--only codeware,cet] [--tag redscript=v1.0.0-preview.22]
    py -3.13 -B tools/patchday/frameworks.py check [--game <dir>] [--only ...] [--report <file.md>]
    py -3.13 -B tools/patchday/frameworks.py snapshot [--game <dir>] [--out tools/patchday/data/frameworks-2.31.json]

RED4ext, Cyber Engine Tweaks, redscript, ArchiveXL, TweakXL, Codeware and Input Loader each get a release
for nearly every game patch, mostly the same day (2.3: CET 1.36.0 + RED4ext 1.28.0 on 2025-07-17; 2.31:
CET 1.37.0 + RED4ext 1.29.0 on 2025-09-11; psiberx's three within days).

fetch    the newest GitHub release of each (or --tag name=tag, also for a prerelease) into
         build/patchday/frameworks/<name>-<tag>/: the release zip exactly as published, its notes
         (release.json) and its files extracted under files/ in the game's own layout. The game is not
         touched. The installed frameworks are Vortex hardlinks into its staging folder (fsutil hardlink
         list shows both paths), so they change through Vortex only: drag the staged zip onto Vortex's
         Mods page. Copying over them would write through the link into Vortex's own copy.
check    every staged release whose files differ from the installed ones, offline:
           - files the release adds, changes, drops (inside its plugin folder);
           - script API (Codeware, ArchiveXL, TweakXL): declarations removed or re-signed, and where the
             port's redscript or Lua names them;
           - the port's redscript compiled with the staged compiler and the staged framework scripts
             against the game's bundle (check_reds.py's sandbox): the authoritative script check;
           - CET: the CET internals Render/NativeTargetLifetime.hpp authenticates (cet_remap.py, dry
             run), every CET API name the port's Lua uses that the installed .asi carries and the
             staged one does not, and the user32 imports Hooks/OsInput.cpp patches in CET's IAT;
           - RED4ext: the plugin API the port declares (Main.cpp Supports/Query) against the loader.
         The SDK the port compiles against is sdk_layouts.py's (and the game's RTTI checks it live).
snapshot versions and file hashes of the installed set: what a port build was tested with.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
STAGE = os.path.join(REPO, 'build', 'patchday', 'frameworks')
PY = [sys.executable, '-B']
sys.path.insert(0, HERE)
import pe  # noqa: E402

# name: repo, the file that identifies it in the game, the release asset, the plugin folder it owns.
FRAMEWORKS = {
    'red4ext': dict(repo='wopss/RED4ext', main='red4ext/RED4ext.dll', asset=r'^red4ext[-_][\d.]+\.zip$',
                    folder=None),
    'cet': dict(repo='maximegmd/CyberEngineTweaks', main='bin/x64/plugins/cyber_engine_tweaks.asi',
                asset=r'^cet_[\d._]+\.zip$', folder=None),
    'redscript': dict(repo='jac3km4/redscript', main='engine/tools/scc_lib.dll',
                      asset=r'^redscript-v[\w.\-]+-windows\.zip$', folder=None),
    'archivexl': dict(repo='psiberx/cp2077-archive-xl', main='red4ext/plugins/ArchiveXL/ArchiveXL.dll',
                      asset=r'^ArchiveXL-[\d.]+\.zip$', folder='red4ext/plugins/ArchiveXL'),
    'tweakxl': dict(repo='psiberx/cp2077-tweak-xl', main='red4ext/plugins/TweakXL/TweakXL.dll',
                    asset=r'^TweakXL-[\d.]+\.zip$', folder='red4ext/plugins/TweakXL'),
    'codeware': dict(repo='psiberx/cp2077-codeware', main='red4ext/plugins/Codeware/Codeware.dll',
                     asset=r'^Codeware-[\d.]+\.zip$', folder='red4ext/plugins/Codeware'),
    'inputloader': dict(repo='jackhumbert/cyberpunk2077-input-loader',
                        main='red4ext/plugins/input_loader/input_loader.dll',
                        asset=r'^input_loader_v[\d.]+\.zip$', folder='red4ext/plugins/input_loader'),
}
# What a running game writes into a framework's folder; never part of a release.
RUNTIME_FILES = re.compile(r'(\.log$|/__folder_managed_by_vortex$|/Persistent/|/logs?/)', re.I)
# What OsInput.cpp patches in cyber_engine_tweaks.asi's import table.
CET_IAT = ('GetClientRect', 'GetCursorPos', 'SetCursorPos')


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def product_version(path):
    """VS_VERSIONINFO's ProductVersion (or FileVersion), read from the file -- scc.exe has neither."""
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return None
    for key in ('ProductVersion', 'FileVersion'):
        k = data.rfind(key.encode('utf-16-le') + b'\0\0')
        if k < 0:
            continue
        v = k + len(key) * 2 + 2
        v += (-v) % 4
        end = v
        while end + 1 < len(data) and data[end:end + 2] != b'\0\0':
            end += 2
        s = data[v:end].decode('utf-16-le', errors='replace').strip()
        if s:
            return s
    return None


def github(path):
    req = urllib.request.Request(f'https://api.github.com/repos/{path}',
                                 headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'cpvr-patchday'})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def set_current(name, dest):
    """The release the last fetch of `name` asked for (the newest, or its --tag) is the one check uses."""
    path = os.path.join(STAGE, 'current.json')
    cur = {}
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            cur = json.load(f)
    cur[name] = os.path.basename(dest)
    os.makedirs(STAGE, exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(cur, f, indent=1)


def staged_dirs():
    """{name: (dir, release.json)} of the release each name's last fetch asked for."""
    out = {}
    path = os.path.join(STAGE, 'current.json')
    if not os.path.exists(path):
        return out
    with open(path, encoding='utf-8') as f:
        cur = json.load(f)
    for name, d in cur.items():
        meta = os.path.join(STAGE, d, 'release.json')
        if os.path.exists(meta):
            with open(meta, encoding='utf-8') as f:
                out[name] = (os.path.join(STAGE, d), json.load(f))
    return out


def safe_extract(zpath, dest):
    """A release zip is data from the internet: no absolute paths, no way out of dest."""
    root = os.path.realpath(dest)
    with zipfile.ZipFile(zpath) as z:
        for info in z.infolist():
            name = info.filename.replace('\\', '/')
            if name.startswith('/') or re.match(r'^[A-Za-z]:', name) or '..' in name.split('/'):
                raise ValueError(f'{zpath}: refusing entry {info.filename!r}')
            target = os.path.realpath(os.path.join(dest, name))
            if not target.startswith(root + os.sep) and target != root:
                raise ValueError(f'{zpath}: entry {info.filename!r} escapes the folder')
        z.extractall(dest)


def fetch(args):
    only = set(args.only.split(',')) if args.only else set(FRAMEWORKS)
    tags = dict(t.split('=', 1) for t in args.tag)
    for name in sorted(only):
        fw = FRAMEWORKS[name]
        rel = github(f"{fw['repo']}/releases/tags/{tags[name]}" if name in tags else f"{fw['repo']}/releases/latest")
        assets = [a for a in rel['assets'] if re.match(fw['asset'], a['name'], re.I)]
        if len(assets) != 1:
            print(f"{name:12s} {rel['tag_name']}: asset not recognised among {[a['name'] for a in rel['assets']]}")
            continue
        asset = assets[0]
        dest = os.path.join(STAGE, f"{name}-{rel['tag_name']}")
        zpath = os.path.join(dest, asset['name'])
        set_current(name, dest)
        if os.path.exists(zpath) and os.path.getsize(zpath) == asset['size']:
            print(f"{name:12s} {rel['tag_name']}: already staged")
            continue
        shutil.rmtree(dest, ignore_errors=True)
        os.makedirs(dest)
        req = urllib.request.Request(asset['browser_download_url'], headers={'User-Agent': 'cpvr-patchday'})
        with urllib.request.urlopen(req, timeout=300) as r, open(zpath, 'wb') as f:
            shutil.copyfileobj(r, f)
        safe_extract(zpath, os.path.join(dest, 'files'))
        meta = dict(name=name, repo=fw['repo'], tag=rel['tag_name'], published=rel.get('published_at'),
                    prerelease=rel.get('prerelease'), body=rel.get('body') or '', asset=asset['name'],
                    sha256=sha(zpath), fetched=datetime.datetime.now().isoformat(timespec='seconds'))
        with open(os.path.join(dest, 'release.json'), 'w', encoding='utf-8') as f:
            json.dump(meta, f, indent=1)
        print(f"{name:12s} {rel['tag_name']} ({(rel.get('published_at') or '')[:10]}"
              f"{', prerelease' if rel.get('prerelease') else ''}) -> {dest}")


# ---- script API ------------------------------------------------------------------------------------

DECL = re.compile(r'^\s*(?:@\w+(?:\([^)]*\))?\s+)*'
                  r'(?:(?:public|private|protected|static|final|native|cb|persistent|const|importonly|abstract|'
                  r'exec|quest|unsafe|inline)\s+)*(func|let|class|struct|enum)\s+(\w+)(.*)$')
OWNER = re.compile(r'^\s*@(addMethod|addField|replaceMethod|wrapMethod|replaceGlobal|wrapGlobal|if)\(([\w.]*)')


def strip_code(line):
    line = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    return line.split('//', 1)[0]


def script_api(root):
    """{(owner, kind, name): set of signatures} for every .reds under root. Owner is the class a
    member belongs to (the scope it is declared in, or the class an @addMethod/@addField names)."""
    api = {}
    files = []
    for dp, _, fs in os.walk(root):
        files += [os.path.join(dp, f) for f in fs if f.endswith('.reds')]
    for path in sorted(files):
        with open(path, encoding='utf-8', errors='replace') as f:
            lines = f.read().split('\n')
        depth = 0
        scopes = []       # (name, kind, depth inside its braces)
        pending = None    # the class an annotation on the line before names
        in_comment = False
        i = 0
        while i < len(lines):
            raw = lines[i]
            i += 1
            if in_comment:
                if '*/' not in raw:
                    continue
                raw = raw.split('*/', 1)[1]
                in_comment = False
            if '/*' in raw and '*/' not in raw.split('/*', 1)[1]:
                raw = raw.split('/*', 1)[0]
                in_comment = True
            line = strip_code(raw)
            m = OWNER.match(line)
            if m and m.group(1) != 'if':
                pending = m.group(2)
            d = DECL.match(line)
            owner = scopes[-1][0] if scopes and scopes[-1][1] in ('class', 'struct') else ''
            if d:
                kind, name, rest = d.groups()
                sig = rest
                # A signature over several lines: up to the balancing parenthesis.
                while sig.count('(') > sig.count(')') and i < len(lines):
                    sig += ' ' + strip_code(lines[i]).strip()
                    i += 1
                sig = re.sub(r'\s+', ' ', sig.split('{', 1)[0]).strip().rstrip(';').strip()
                if kind == 'func':
                    sig = re.sub(r'\s*=.*$', '', sig)
                prefix = line[:d.start(1)].split()
                tags = ' '.join(t for t in prefix if t in ('static', 'native', 'cb', 'final', 'persistent'))
                key_owner = pending if pending else owner
                if kind in ('class', 'struct', 'enum'):
                    api.setdefault(('', kind, name), set()).add(re.sub(r'\s+', ' ', rest.split('{', 1)[0]).strip())
                    if '{' in line:
                        scopes.append((name, kind, depth + 1))
                    else:
                        scopes.append((name, kind, None))  # its brace comes on a later line
                else:
                    api.setdefault((key_owner, kind, name), set()).add(f'{tags} {sig}'.strip())
                pending = None
            elif scopes and scopes[-1][1] == 'enum' and depth == scopes[-1][2]:
                e = re.match(r'^\s*(\w+)\s*(?:=\s*([-\w]+))?\s*,?\s*$', line)
                if e:
                    api.setdefault((scopes[-1][0], 'value', e.group(1)), set()).add(e.group(2) or '')
            opened, closed = line.count('{'), line.count('}')
            if scopes and scopes[-1][2] is None and opened:
                scopes[-1] = (scopes[-1][0], scopes[-1][1], depth + 1)
            depth += opened - closed
            while scopes and scopes[-1][2] is not None and depth < scopes[-1][2]:
                scopes.pop()
    return api


def game_names():
    """Names the game's own scripts declare (the decompiled 2.31 bundle in the snapshot): a framework
    member the port seems to use under such a name may be the game's member of that name instead."""
    if not hasattr(game_names, 'cache'):
        base = os.path.join(REPO, 'build', 'patchday', 'base-2.31', 'scripts_decompiled')
        names = set()
        for dp, _, fs in os.walk(base):
            for f in fs:
                if f.lower().endswith('.script'):
                    with open(os.path.join(dp, f), encoding='utf-8', errors='replace') as fh:
                        names.update(re.findall(r'\b(?:func|let|class|struct|enum)\s+(\w+)', fh.read()))
        game_names.cache = names
    return game_names.cache


def api_diff(old, new):
    removed = sorted(k for k in old if k not in new)
    changed = sorted(k for k in old if k in new and old[k] != new[k])
    added = sorted(k for k in new if k not in old)
    return removed, changed, added


def port_sources():
    out = []
    for sub, ext in (('mods/redscript', '.reds'), ('mods/cet', '.lua')):
        for dp, _, fs in os.walk(os.path.join(REPO, sub)):
            out += [os.path.join(dp, f) for f in fs if f.endswith(ext)]
    return sorted(out)


def uses(names, files):
    """{name: [file:line, ...]} where the port's scripts name it (as a member, a call or a type)."""
    if not names:
        return {}
    rx = re.compile(r'(?<![\w$])(' + '|'.join(sorted(map(re.escape, names), key=len, reverse=True)) + r')(?!\w)')
    out = {}
    for path in files:
        with open(path, encoding='utf-8', errors='replace') as f:
            for n, line in enumerate(f, 1):
                code = line.split('--', 1)[0] if path.endswith('.lua') else line.split('//', 1)[0]
                for m in rx.finditer(code):
                    out.setdefault(m.group(1), []).append(f'{os.path.relpath(path, REPO)}:{n}')
    return out


# ---- CET ---------------------------------------------------------------------------------------------

def ascii_strings(path, minlen=4):
    with open(path, 'rb') as f:
        data = f.read()
    return set(m.group(1).decode() for m in re.finditer(rb'(?<![\x21-\x7e])([A-Za-z_][A-Za-z0-9_]{%d,})\x00' % (minlen - 1), data))


def lua_identifiers():
    """{name: [file:line]} of what the port's Lua calls or reads that it does not define itself (its
    locals, functions and parameters are its own names, whatever an .asi happens to contain)."""
    out = {}
    own, members = set(), set()
    for dp, _, fs in os.walk(os.path.join(REPO, 'mods', 'cet')):
        for f in fs:
            if f.endswith('.lua'):
                with open(os.path.join(dp, f), encoding='utf-8', errors='replace') as fh:
                    text = fh.read()
                for m in re.finditer(r'\blocal\s+(?:function\s+)?([\w\s,]+)', text):
                    own.update(re.findall(r'\w+', m.group(1).split('=')[0]))
                own.update(re.findall(r'\bfunction\s+(?:[\w.]+[.:])?(\w+)\s*\(', text))
                members.update(re.findall(r'\bfunction\s+[\w.]+[.:](\w+)\s*\(', text))
                members.update(re.findall(r'(\w+)\s*=\s*function\b', text))
                for m in re.finditer(r'\bfunction\b[^(]*\(([^)]*)\)', text):
                    own.update(re.findall(r'\w+', m.group(1)))
                for m in re.finditer(r'\bfor\s+([\w\s,]+?)\s+in\b', text):
                    own.update(re.findall(r'\w+', m.group(1)))
    for dp, _, fs in os.walk(os.path.join(REPO, 'mods', 'cet')):
        for f in fs:
            if not f.endswith('.lua'):
                continue
            path = os.path.join(dp, f)
            with open(path, encoding='utf-8', errors='replace') as fh:
                for n, line in enumerate(fh, 1):
                    code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '', line.split('--', 1)[0])
                    for m in re.finditer(r'(?<![\w.:])([A-Za-z_]\w{3,})|[.:]([A-Za-z_]\w{3,})', code):
                        if (m.group(1) and m.group(1) in own) or (m.group(2) and m.group(2) in members):
                            continue
                        out.setdefault(m.group(1) or m.group(2), []).append(f'{os.path.relpath(path, REPO)}:{n}')
    return out


def check_cet(old_asi, new_asi, report):
    report.append('### CET')
    old_s, new_s = ascii_strings(old_asi), ascii_strings(new_asi)
    ours = lua_identifiers()
    gone = sorted(n for n in ours if n in old_s and n not in new_s)
    report.append(f'- Lua identifiers of the port that the installed .asi carries as a name: '
                  f'{sum(1 for n in ours if n in old_s)}; missing from the staged .asi: {len(gone)}')
    for n in gone[:40]:
        report.append(f'  - `{n}`: {", ".join(ours[n][:4])}{" ..." if len(ours[n]) > 4 else ""}')
    imp = pe.Image(new_asi).imports().get('user32.dll', set())
    missing = [n for n in CET_IAT if n not in imp]
    report.append('- user32 imports OsInput.cpp patches in its IAT: ' +
                  ('all present' if not missing else f'MISSING {missing} -- the CET cursor hooks stay unpatched'))
    r = subprocess.run(PY + [os.path.join(HERE, 'cet_remap.py'), '--old', old_asi, '--new', new_asi],
                       cwd=REPO, capture_output=True, text=True)
    report.append('- CET internals (cet_remap.py, dry run):')
    report += ['      ' + ln for ln in (r.stdout + r.stderr).strip().splitlines()]
    return not gone and not missing and r.returncode == 0 and 'LOST' not in r.stdout and 'incomplete' not in r.stdout


def check_red4ext(new_dll, report):
    """The loader's plugin API: Main.cpp declares API v1 (COMPAT_0) and SDK 1.0.0. RED4ext logs an
    incompatible plugin and skips it, so the live launch is the final word; this reads what the
    release itself says it accepts."""
    report.append('### RED4ext')
    with open(os.path.join(REPO, 'src', 'Main.cpp'), encoding='utf-8') as f:
        main = f.read()
    sup = re.search(r'Supports\(\)\s*\{\s*return\s+(\w+)', main)
    sdk = re.search(r'info->sdk\s*=\s*(\w+)', main)
    report.append(f'- the port declares {sup and sup.group(1)}, {sdk and sdk.group(1)}, '
                  f'runtime {"INDEPENDENT" if "RUNTIME_VERSION_INDEPENDENT" in main else "pinned"}')
    s = ascii_strings(new_dll, 6)
    with open(new_dll, 'rb') as f:
        data = f.read()
    hints = sorted(set(m.group(0).decode(errors='replace') for m in
                       re.finditer(rb'[^\x00]{0,60}(?:API version|SDK version|incompatible)[^\x00]{0,80}', data, re.I)))
    for h in hints[:8]:
        report.append(f'  - loader string: `{h.strip()}`')
    report.append(f'- exports of the loader: {len(s)} names (no static verdict; the launch log is checked by check_logs.py)')
    return True


# ---- check ---------------------------------------------------------------------------------------------

def framework_files(root, fw):
    """Relative paths (game layout) of a framework under root: its plugin folder, or its main file's folder."""
    base = fw['folder'] or os.path.dirname(fw['main'])
    out = {}
    top = os.path.join(root, base)
    if fw['folder'] and os.path.isdir(top):
        for dp, _, fs in os.walk(top):
            for f in fs:
                rel = os.path.relpath(os.path.join(dp, f), root).replace('\\', '/')
                if not RUNTIME_FILES.search('/' + rel):
                    out[rel] = os.path.join(dp, f)
    return out


def release_files(files_dir):
    out = {}
    for dp, _, fs in os.walk(files_dir):
        for f in fs:
            rel = os.path.relpath(os.path.join(dp, f), files_dir).replace('\\', '/')
            out[rel] = os.path.join(dp, f)
    return out


def check(args):
    staged = staged_dirs()
    only = set(args.only.split(',')) if args.only else set(FRAMEWORKS)
    report = [f'# Framework releases against the installed ones ({datetime.datetime.now().isoformat(timespec="seconds")})', '']
    ok = True
    differing = {}
    for name in sorted(only):
        fw = FRAMEWORKS[name]
        if name not in staged:
            report.append(f'- **{name}**: nothing staged (frameworks.py fetch)')
            continue
        d, meta = staged[name]
        rel = release_files(os.path.join(d, 'files'))
        changed = [r for r, p in rel.items() if not os.path.exists(os.path.join(args.game, r))
                   or sha(p) != sha(os.path.join(args.game, r))]
        inst_v = product_version(os.path.join(args.game, fw['main'])) or '?'
        new_v = product_version(rel.get(fw['main'], '')) if fw['main'] in rel else None
        # The release is the one installed when its main binary is (r6/cache xmls, configs and the
        # like are rewritten by the game or by the player).
        main_inst = os.path.join(args.game, fw['main'])
        if fw['main'] in rel and os.path.exists(main_inst) and sha(rel[fw['main']]) == sha(main_inst):
            report.append(f'- **{name}**: staged {meta["tag"]} = installed ({inst_v})'
                          + (f'; files that differ (written at runtime): {", ".join(changed[:6])}' if changed else ''))
            continue
        differing[name] = (d, meta, rel)
        report.append(f'## {name}: installed {inst_v} -> staged {meta["tag"]} ({new_v or "?"}, '
                      f'{(meta.get("published") or "")[:10]}{", PRERELEASE" if meta.get("prerelease") else ""})')
        report.append('')
        notes = [ln.rstrip() for ln in meta['body'].replace('\r', '').split('\n') if ln.strip()][:14]
        report += ['> ' + ln for ln in notes] + ['']
        added = [r for r in changed if not os.path.exists(os.path.join(args.game, r))]
        report.append(f'- files: {len(rel)} in the release, {len(changed) - len(added)} changed, {len(added)} new'
                      + (': ' + ', '.join(added[:8]) if added else ''))
        if fw['folder']:
            dropped = sorted(set(framework_files(args.game, fw)) - set(rel))
            if dropped:
                report.append(f'- installed files the release no longer has: {", ".join(dropped[:12])}')
        if fw['folder'] and os.path.isdir(os.path.join(d, 'files', fw['folder'])):
            old_api = script_api(os.path.join(args.game, fw['folder']))
            new_api = script_api(os.path.join(d, 'files', fw['folder']))
            if old_api or new_api:
                removed, resig, add = api_diff(old_api, new_api)
                report.append(f'- script API: {len(old_api)} -> {len(new_api)} declarations; '
                              f'{len(removed)} removed, {len(resig)} re-signed, {len(add)} added'
                              + (': ' + ', '.join(f'{k[0] + "." if k[0] else ""}{k[2]}' for k in add[:6]) if add else ''))
                hit = uses({k[2] for k in removed + resig}, port_sources())
                game = game_names()
                for k in removed + resig:
                    where = hit.get(k[2])
                    if where:
                        what = 'REMOVED' if k in removed else 'RE-SIGNED'
                        report.append(f'  - {what} {k[0] + "." if k[0] else ""}{k[2]} ({k[1]}): '
                                      f'{sorted(old_api[k])[:2]} -> {sorted(new_api.get(k, []))[:2]}; '
                                      f'the port names it at {", ".join(where[:5])}'
                                      + (' (the game\'s scripts declare the name too: may be theirs)' if k[2] in game else ''))
                        ok = False
                if not any(hit.get(k[2]) for k in removed + resig):
                    report.append('  - none of the removed / re-signed names appears in the port\'s scripts')
        if name == 'cet' and fw['main'] in rel:
            ok &= check_cet(os.path.join(args.game, fw['main']), rel[fw['main']], report)
        if name == 'red4ext' and fw['main'] in rel:
            check_red4ext(rel[fw['main']], report)
        report.append('')
    # One compile with everything staged that carries scripts or the compiler.
    reds_stage = [n for n in ('codeware', 'archivexl', 'tweakxl') if n in differing]
    if reds_stage or 'redscript' in differing:
        cmd = PY + [os.path.join(HERE, 'check_reds.py'), '--game', args.game, '--label', 'frameworks']
        if 'redscript' in differing:
            cmd += ['--scc', os.path.join(differing['redscript'][0], 'files', 'engine', 'tools')]
        for n in reds_stage:
            cmd += ['--framework', os.path.join(differing[n][0], 'files', FRAMEWORKS[n]['folder'])]
        r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
        out = (r.stdout + r.stderr).strip().splitlines()
        report.append(f'## redscript: the port compiled with {", ".join(reds_stage + (["scc " + differing["redscript"][1]["tag"]] if "redscript" in differing else []))} staged')
        report.append('')
        report.append('```')
        report += out[-60:]
        report.append('```')
        ok &= r.returncode == 0
    text = '\n'.join(report) + '\n'
    print(text)
    if args.report:
        with open(args.report, 'w', encoding='utf-8') as f:
            f.write(text)
    print('FRAMEWORKS', 'OK' if ok else 'PROBLEMS')
    sys.exit(0 if ok else 1)


def snapshot(args):
    out = dict(game=args.game, taken=datetime.datetime.now().isoformat(timespec='seconds'), frameworks={})
    for name, fw in FRAMEWORKS.items():
        main = os.path.join(args.game, fw['main'])
        if not os.path.exists(main):
            out['frameworks'][name] = None
            continue
        files = framework_files(args.game, fw) if fw['folder'] else {}
        files[fw['main']] = main
        if name == 'redscript':
            files['engine/tools/scc.exe'] = os.path.join(args.game, 'engine', 'tools', 'scc.exe')
        if name == 'cet':
            for extra in ('bin/x64/version.dll', 'bin/x64/winmm.dll'):
                if os.path.exists(os.path.join(args.game, extra)):
                    files[extra] = os.path.join(args.game, extra)
        # The GitHub tag, when a staged release carries the same main binary (scc carries no version,
        # Input Loader's dll says the game's).
        release = None
        main_sha = sha(main)
        if os.path.isdir(STAGE):
            for d in sorted(os.listdir(STAGE)):
                staged_main = os.path.join(STAGE, d, 'files', fw['main'])
                if d.startswith(name + '-') and os.path.exists(staged_main) and sha(staged_main) == main_sha:
                    release = d[len(name) + 1:]
        out['frameworks'][name] = dict(
            repo=fw['repo'], version=product_version(main), release=release,
            files={r: dict(size=os.path.getsize(p), sha256=sha(p)) for r, p in sorted(files.items())})
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
    for name, v in out['frameworks'].items():
        print(f"{name:12s} {(v['release'] or v['version']) if v else 'NOT INSTALLED'}  ({len(v['files']) if v else 0} files)")
    print('->', args.out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    f = sub.add_parser('fetch')
    f.add_argument('--only', default=None, help='comma list of ' + ','.join(FRAMEWORKS))
    f.add_argument('--tag', action='append', default=[], help='name=tag: that release instead of the newest')
    c = sub.add_parser('check')
    c.add_argument('--game', default=GAME)
    c.add_argument('--only', default=None)
    c.add_argument('--report', default=None)
    s = sub.add_parser('snapshot')
    s.add_argument('--game', default=GAME)
    s.add_argument('--out', default=None)
    args = ap.parse_args()
    if args.cmd == 'snapshot' and not args.out:
        with open(os.path.join(REPO, 'include', 'Core', 'GameBuild.hpp'), encoding='utf-8') as fh:
            version = re.search(r'kVersion\s*=\s*"([^"]+)"', fh.read()).group(1)
        args.out = os.path.join(HERE, 'data', f'frameworks-{version}.json')
    {'fetch': fetch, 'check': check, 'snapshot': snapshot}[args.cmd](args)


if __name__ == '__main__':
    main()
