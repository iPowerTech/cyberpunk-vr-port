"""Bundle a psiberx framework's scripts the way its release does (tools/dist/steps/compose-redscripts.ps1).

    py -3.13 -B tools/patchday/compose_reds.py <checkout> <out dir> [--name Codeware] [--version 1.20.6-2.5]

Every scripts/**/*.reds is grouped by its `module` line (no module: <name>.Global), the module/import lines and
comment / blank lines are dropped, and each group is written as <out dir>/<scope>.reds with a version header,
the module line and the union of its imports. <out dir> is the plugin's Scripts folder
(red4ext/plugins/<name>/Scripts), so a staged copy can stand in for the installed one in check_reds.py.
"""
import argparse
import os
import re


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('checkout')
    ap.add_argument('out')
    ap.add_argument('--name', default='Codeware')
    ap.add_argument('--version', default='dev')
    a = ap.parse_args()
    bundles = {}
    root = os.path.join(a.checkout, 'scripts')
    files = []
    for d, _, names in os.walk(root):
        files += [os.path.join(d, n) for n in names if n.endswith('.reds')]
    # PowerShell's Get-ChildItem -Recurse lists a folder's files before its subfolders, each sorted by name
    files.sort(key=lambda p: [s.lower() for s in os.path.relpath(p, root).split(os.sep)[:-1]] + ['\0' + os.path.basename(p).lower()])
    for path in files:
        lines = open(path, encoding='utf-8-sig').read().splitlines()
        module = next((m.group(1) for m in (re.match(r'^module\s+(.+)', l) for l in lines) if m), None)
        imports = [m.group(1) for m in (re.match(r'^import\s+(.+)', l) for l in lines) if m]
        body = [l for l in lines if not re.match(r'^\s*(//|module\s|import\s)', l) and l.strip()]
        scope = module or f'{a.name}.Global'
        b = bundles.setdefault(scope, {'module': module, 'imports': {}, 'sources': []})
        b['sources'].append('\n'.join(body).strip())
        for i in imports:
            b['imports'][i] = 1
    os.makedirs(a.out, exist_ok=True)
    for scope, b in bundles.items():
        out = [f'// {a.name} {a.version}']
        if b['module']:
            out.append(f'module {b["module"]}')
        out += [f'import {i}' for i in b['imports']]
        for src in b['sources']:
            if src:
                out.append('\n' + src)
        with open(os.path.join(a.out, f'{scope}.reds'), 'w', encoding='ascii', errors='replace', newline='\r\n') as f:
            f.write('\n'.join(out) + '\n')
        print(scope, len(b['sources']), 'files')


if __name__ == '__main__':
    main()
