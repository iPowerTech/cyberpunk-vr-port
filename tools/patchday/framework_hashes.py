"""Every game address a framework resolves through the game's address library, checked on two builds.

    py -3.13 -B tools/patchday/framework_hashes.py [--old build/patchday/base-2.31] [--new <game>]
                                                  [--src build/patchday/framework-src] [--out <report.md>]

RED4ext, ArchiveXL, TweakXL, Codeware and CET do not carry addresses: they carry the hash of a symbol
(`constexpr uint32_t X = 1234;`, `UniversalRelocFunc<...>(1234)`) and look it up in
bin/x64/cyberpunk2077_addresses.json at start. A patch breaks a framework where a hash it names is gone from
the new library, names several entries, or names a function whose code changed (the same call with the old
arguments then does something else). Per hash: the entries on both builds and, for code, whether the
function's masked bytes (sigs.py: addresses wildcarded) are the same. The RED4ext.SDK copy each framework
builds with is scanned too (its Detail/AddressHashes.hpp is what the SDK's own wrappers resolve).
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import AddressLibrary, Image, find_game_files  # noqa: E402
import sigs  # noqa: E402

GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
CONST = re.compile(r'constexpr\s+(?:std::)?uint32_t\s+(\w+)\s*=\s*(0[xX][0-9A-Fa-f]{1,8}|\d{5,10})\s*[uUlL]*\s*;')
INLINE = re.compile(r'Reloc\w*\s*<[^;]*?>\s*\w*\s*[({]\s*(0[xX][0-9A-Fa-f]{1,8}|\d{5,10})\s*[uUlL]*\s*[)}]')
SKIP_DIRS = {'.git', 'build', 'out', 'target', '.xmake', 'vsxmake2022', 'node_modules'}
FRAMEWORKS = {
    'RED4ext': 'RED4ext',
    'ArchiveXL': 'cp2077-archive-xl',
    'TweakXL': 'cp2077-tweak-xl',
    'Codeware': 'cp2077-codeware',
    'CET': 'CyberEngineTweaks',
}


def scan(root):
    """{hash: [(name, file:line)]} over C/C++ sources under root."""
    out = defaultdict(list)
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
        for f in files:
            if not f.endswith(('.hpp', '.h', '.cpp', '.inl')):
                continue
            p = os.path.join(d, f)
            try:
                text = open(p, encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            if 'uint32_t' not in text and 'Reloc' not in text:
                continue
            for n, line in enumerate(text.splitlines(), 1):
                for m in CONST.finditer(line):
                    out[int(m.group(2), 0)].append((m.group(1), f'{os.path.relpath(p, root)}:{n}'))
                for m in INLINE.finditer(line):
                    out[int(m.group(1), 0)].append(('(inline)', f'{os.path.relpath(p, root)}:{n}'))
    return out


class Build:
    def __init__(self, game):
        exe, lib = find_game_files(game)
        self.img = Image(exe)
        self.lib = AddressLibrary(lib, self.img)
        self._cache = {}

    def masked(self, rva):
        if rva in self._cache:
            return self._cache[rva]
        f = self.img.function_at(rva)
        val = None
        if f is not None and f[0] == rva:
            insns = sigs.decode(self.img, f[0], f[1])
            val = (sigs.masked_hash(self.img, insns), f[1] - f[0])
        self._cache[rva] = val
        return val


def check(h, old, new):
    o, n = old.lib.by_hash.get(h, []), new.lib.by_hash.get(h, [])
    if not n:
        return 'GONE' if o else 'absent-both', o, n, ''
    if len(n) > 1:
        return 'ambiguous', o, n, f'{len(n)} entries'
    if not o:
        return 'new-only', o, n, ''
    if len(o) > 1:
        return 'was-ambiguous', o, n, ''
    ro, rn = o[0], n[0]
    if not (ro in old.img.text and rn in new.img.text):
        return 'data', o, n, ''
    mo, mn = old.masked(ro), new.masked(rn)
    if mo is None or mn is None:
        return 'code-not-func-start', o, n, ''
    if mo[0] == mn[0]:
        return 'same-code', o, n, f'{mo[1]} B'
    return 'CHANGED', o, n, f'{mo[1]} -> {mn[1]} B'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=GAME)
    ap.add_argument('--src', default=os.path.join(REPO, 'build', 'patchday', 'framework-src'))
    ap.add_argument('--sdk', default=os.path.join(REPO, 'externals', 'RED4ext.SDK'))
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'framework_hashes.md'))
    a = ap.parse_args()
    old, new = Build(a.old), Build(a.new)
    roots = {k: os.path.join(a.src, v) for k, v in FRAMEWORKS.items() if os.path.isdir(os.path.join(a.src, v))}
    roots['RED4ext.SDK (port)'] = a.sdk
    lines = [f'# Framework address hashes: {os.path.basename(a.old)} -> {a.new}', '']
    totals = {}
    dump = []
    for fw, root in roots.items():
        found = scan(root)
        rows = defaultdict(list)
        for h, uses in sorted(found.items(), key=lambda kv: kv[1][0][0]):
            status, o, n, note = check(h, old, new)
            rows[status].append((h, uses, o, n, note))
            dump.append(dict(framework=fw, hash=h, names=sorted({u[0] for u in uses}), uses=[u[1] for u in uses],
                             status=status, old=o, new=n, note=note))
        totals[fw] = {k: len(v) for k, v in rows.items()}
        lines.append(f'## {fw} ({len(found)} hashes): ' + ', '.join(f'{k} {len(v)}' for k, v in sorted(rows.items())))
        lines.append('')
        for status in ('GONE', 'ambiguous', 'CHANGED', 'code-not-func-start', 'was-ambiguous', 'data', 'same-code',
                       'new-only', 'absent-both'):
            if status not in rows:
                continue
            brief = status in ('same-code', 'data')
            lines.append(f'### {status} ({len(rows[status])})')
            for h, uses, o, n, note in rows[status]:
                names = sorted({u[0] for u in uses})
                where = uses[0][1]
                ro = ','.join(f'{x:#x}' for x in o[:3]) or '-'
                rn = ','.join(f'{x:#x}' for x in n[:3]) or '-'
                if brief:
                    lines.append(f'- `{h}` {"/".join(names)} {ro} -> {rn} {note}')
                else:
                    lines.append(f'- `{h}` **{"/".join(names)}** {ro} -> {rn} {note} ({where})')
            lines.append('')
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.splitext(a.out)[0] + '.json', 'w', encoding='utf-8') as f:
        json.dump(dump, f, indent=1)
    for fw, t in totals.items():
        print(f'{fw:20s}', ', '.join(f'{k} {v}' for k, v in sorted(t.items())))
    print('report:', a.out)


if __name__ == '__main__':
    main()
