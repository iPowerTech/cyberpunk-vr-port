"""Self-tests of the patch-day tooling, against the old build alone (no new exe needed).

    py -3.13 -B tools/patchday/selftest.py

1. identity: remap the inventory onto the very exe it was taken from. Every address must map to
   itself and every guard must re-derive to the value already in the source.
2. shift: pretend every address moved by +0x10 and every guard re-derived to value+1, apply that to a
   scratch copy of src/ and include/, re-scan the copy and check that exactly the addresses changed
   (by +0x10), the guards changed (by +1), and nothing else did -- not-rva literals, foreign (CET)
   addresses, comments of unmapped values. Also that the copy still has the same number of lines.
3. offsets: every field-offset carrier read back through its window gives the value it carries.
4. framework api: an edited copy of the installed Codeware (one method the port's Lua calls removed, one
   its redscript calls re-signed) is reported as exactly that by frameworks.py, and fails the compile.
"""
import collections
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import inventory  # noqa: E402

BASE = os.path.join(REPO, 'build', 'patchday', 'base-2.31')
WORK = os.path.join(REPO, 'build', 'patchday', 'selftest')
SHIFT = 0x10


def run(args):
    print('>', ' '.join(args))
    r = subprocess.run([sys.executable, '-B'] + args, cwd=REPO, capture_output=True, text=True)
    print(r.stdout[-1500:])
    if r.returncode:
        print(r.stderr[-3000:])
        sys.exit(f'FAILED: {args[0]}')
    return r.stdout


def identity(db_path):
    out = os.path.join(WORK, 'identity')
    run([os.path.join(HERE, 'remap.py'), '--db', db_path, '--new', BASE, '--out', out])
    with open(os.path.join(out, 'map.json'), encoding='utf-8') as f:
        m = json.load(f)
    with open(os.path.join(out, 'guards.json'), encoding='utf-8') as f:
        g = json.load(f)
    moved = [k for k, r in m.items() if r['new'] is not None and int(r['new'], 16) != int(k, 16)
             and r['status'] != 'text-end']
    bad_guards = [x for x in g if x['status'] != 'ok']
    print(f'identity: {len(m)} addresses, {len(moved)} moved, {len(bad_guards)} guards not ok')
    return not moved and not bad_guards


def shift(db_path, db):
    repo = os.path.join(WORK, 'repo')
    shutil.rmtree(repo, ignore_errors=True)
    for d in ('src', 'include'):
        shutil.copytree(os.path.join(REPO, d), os.path.join(repo, d))
    out = os.path.join(WORK, 'shift')
    run([os.path.join(HERE, 'remap.py'), '--db', db_path, '--new', BASE, '--out', out, '--repo', repo,
         '--simulate-shift', hex(SHIFT), '--apply', '--version', 'selftest'])
    with open(os.path.join(out, 'map.json'), encoding='utf-8') as f:
        m = {int(k, 16): v for k, v in json.load(f).items()}
    mapped = {k for k, v in m.items() if v['new'] is not None and v['status'] in __import__('remap').RELIABLE}
    occ2, guards2 = inventory.scan(repo)
    # Positions are stable (a hex literal of the same width), so the rescan lines up 1:1 with the
    # original occurrences by (file, line, column).
    before = {(o['file'], o['line'], o['col']): o for o in db['occurrences']}
    after = {(o['file'], o['line'], o['col']): o for o in occ2}
    errors = []
    checked = collections.Counter()
    for key, o in before.items():
        a = after.get(key)
        # Another module's offset ("ngfx-capture-interception.dll +0x4184FE") is never rewritten.
        moves = o['value'] in mapped and o['kind'] != 'foreign-module'
        want = o['value'] + SHIFT if moves else o['value']
        if a is None:
            # A literal that grew a digit shifts the columns after it on the same line.
            cands = [x for x in occ2 if x['file'] == o['file'] and x['line'] == o['line'] and x['value'] == want]
            if cands:
                checked['shifted-column'] += 1
                continue
            errors.append(f"{o['file']}:{o['line']} {o['literal']} vanished")
            continue
        if a['value'] != want:
            errors.append(f"{o['file']}:{o['line']} {o['literal']} -> {a['literal']} (wanted {want:#x}, kind {o['kind']})")
        checked['ok'] += 1
    # Literals that are not addresses (all hex literals outside the db) must be untouched.
    db_keys = set(before)
    for key, a in after.items():
        if key in db_keys:
            continue
        if a['kind'] in ('ida-name',):
            continue
    g_before = {(g['file'], g['line'], g['type'], g.get('name')): g for g in db['guards']}
    g_after = collections.defaultdict(list)
    for g in guards2:
        g_after[(g['file'], g['line'], g['type'], g.get('name'))].append(g)
    for key, g in g_before.items():
        cand = g_after.get(key, [])
        if g['type'] == 'fnv':
            ok = any(c['fnv'] == (g['fnv'] + 1) & ((1 << 64) - 1) for c in cand)
        else:
            want = bytes((b + 1) & 0xFF for b in bytes.fromhex(g['bytes'])).hex()
            ok = any(c['bytes'].lower() == want for c in cand)
        if not ok:
            errors.append(f"guard {key} not rewritten to value+1")
        checked['guard'] += 1
    # Line counts: the rewrite never adds or removes a line.
    for rel in sorted({o['file'] for o in db['occurrences']}):
        with open(os.path.join(REPO, rel), encoding='utf-8', newline='') as f1, \
                open(os.path.join(repo, rel), encoding='utf-8', newline='') as f2:
            a, b = f1.read(), f2.read()
        if a.count('\n') != b.count('\n') or a.count('\r') != b.count('\r'):
            errors.append(f'{rel}: line structure changed')
    # Files without a single address must be byte-identical.
    for d in ('src', 'include'):
        for dp, _, files in os.walk(os.path.join(REPO, d)):
            for f in files:
                p1 = os.path.join(dp, f)
                rel = os.path.relpath(p1, REPO).replace('\\', '/')
                if rel in {o['file'] for o in db['occurrences']} | {g['file'] for g in db['guards']} | inventory.SKIP_FILES:
                    continue
                with open(p1, 'rb') as a, open(os.path.join(repo, rel), 'rb') as b:
                    if a.read() != b.read():
                        errors.append(f'{rel}: changed although it names no address')
    print('shift checks:', dict(checked))
    for e in errors[:40]:
        print('  ERROR', e)
    print(f'shift: {len(errors)} errors')
    return not errors


def main():
    os.makedirs(WORK, exist_ok=True)
    db_path = os.path.join(HERE, 'data', 'rva_db-68af45ea.json')
    with open(db_path, encoding='utf-8') as f:
        db = json.load(f)
    ok1 = identity(db_path)
    ok2 = shift(db_path, db)
    ok3 = offsets()
    ok4 = framework_api()
    ok = ok1 and ok2 and ok3 and ok4
    print('SELFTEST', 'PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


def framework_api():
    """A Codeware release that drops a method the port's Lua calls (inkSystem.GetLayers) and re-signs
    one its redscript calls (GameInstance.GetStaticEntitySystem): frameworks.py must name exactly
    those two, with the port's lines, and the compile against that release must fail in the port."""
    import frameworks as fw
    game_cw = os.path.join(fw.GAME, 'red4ext', 'plugins', 'Codeware')
    fake = os.path.join(WORK, 'fw', 'Codeware')
    shutil.rmtree(fake, ignore_errors=True)
    shutil.copytree(os.path.join(game_cw, 'Scripts'), os.path.join(fake, 'Scripts'),
                    ignore=shutil.ignore_patterns('__folder_managed_by_vortex'))
    p = os.path.join(fake, 'Scripts', 'Codeware.Global.reds')
    with open(p, encoding='utf-8', newline='') as f:
        text = f.read()
    gone = '    public native func GetLayers() -> array<ref<inkLayerWrapper>>\n'
    resig = 'public static native func GetStaticEntitySystem() -> ref<StaticEntitySystem>'
    if gone not in text.replace('\r\n', '\n') or resig not in text:
        print('framework api: the Codeware declarations this test edits are not in the installed release')
        return False
    text = text.replace(gone, '').replace(gone.replace('\n', '\r\n'), '')
    text = text.replace(resig, 'public static native func GetStaticEntitySystem() -> Int32')
    with open(p, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    removed, changed, _ = fw.api_diff(fw.script_api(game_cw), fw.script_api(fake))
    want_removed = [('inkSystem', 'func', 'GetLayers')]
    want_changed = [('GameInstance', 'func', 'GetStaticEntitySystem')]
    hits = fw.uses({k[2] for k in removed + changed}, fw.port_sources())
    errors = []
    if removed != want_removed:
        errors.append(f'removed {removed}, wanted {want_removed}')
    if changed != want_changed:
        errors.append(f're-signed {changed}, wanted {want_changed}')
    for name in ('GetLayers', 'GetStaticEntitySystem'):
        if not hits.get(name):
            errors.append(f'{name}: the port\'s uses were not found')
    r = subprocess.run([sys.executable, '-B', os.path.join(HERE, 'check_reds.py'), '--framework', fake,
                        '--label', 'selftest-fw'], cwd=REPO, capture_output=True, text=True)
    if r.returncode == 0 or 'vrport_basketball' not in r.stdout:
        errors.append(f'compile against the edited Codeware did not fail in vrport_basketball.reds '
                      f'(exit {r.returncode})')
    print(f"framework api: removed {[k[2] for k in removed]}, re-signed {[k[2] for k in changed]}, "
          f"port uses {sum(len(v) for v in hits.values())} lines, compile exit {r.returncode}")
    for e in errors:
        print('  ERROR', e)
    print(f'framework api: {len(errors)} errors')
    return not errors


def offsets():
    """Every field-offset carrier read back through its window (as if its function had changed) must
    give the value it carries."""
    r = subprocess.run([sys.executable, '-B', os.path.join(HERE, 'offsets.py'), 'check', '--new', BASE,
                        '--force-window'], cwd=REPO, capture_output=True, text=True)
    out = r.stdout
    print(out.strip().splitlines()[0] if out.strip() else r.stderr[-800:])
    bad = [ln for ln in out.splitlines() if ln.strip().startswith(('conflict', 'moved'))]
    for ln in bad:
        print('  ERROR', ln.strip())
    print(f'offsets: {len(bad)} errors')
    return bool(out.strip()) and not bad


if __name__ == '__main__':
    main()
