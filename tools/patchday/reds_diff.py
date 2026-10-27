"""Which game script methods the port wraps or replaces did a patch change?

    py -3.13 -B tools/patchday/reds_diff.py [--old <final.redscripts>] [--new <final.redscripts>]

check_reds.py says whether the port still COMPILES; this says what it now wraps. A wrapper of a method
whose body changed still compiles and may no longer do what it was written for (the method now does
the thing itself, reads another field, is called from elsewhere). Both bundles are decompiled with
redscript-cli (build/patchday/tools/redscript-cli-0.5.31, cached per bundle), every
@wrapMethod / @replaceMethod / @addMethod / @addField target in mods/redscript is looked up in both,
and each is reported unchanged / BODY CHANGED / SIGNATURE CHANGED / GONE (class or method).
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
CLI = os.path.join(REPO, 'build', 'patchday', 'tools', 'redscript-cli-0.5.31', 'redscript-cli.exe')
OLD = os.path.join(REPO, 'build', 'patchday', 'base-2.31', 'r6', 'cache', 'final.redscripts')
ANNOT = re.compile(r'@(wrapMethod|replaceMethod|addMethod|addField|replaceGlobal|wrapConstructor)\s*\(\s*(\w*)\s*\)')
FUNC = re.compile(r'\bfunc\s+(\w+)\s*\(([^)]*)\)\s*(?:->\s*([\w<>\[\]:, ]+))?')
CLASS = re.compile(r'^\s*(?:(?:public|private|protected|abstract|final|native|importonly)\s+)*(?:class|struct)\s+(\w+)'
                   r'(?:\s+extends\s+(\w+))?', re.M)


def decompile(bundle):
    with open(bundle, 'rb') as source:
        h = hashlib.sha256(source.read()).hexdigest()[:16]
    out = os.path.join(REPO, 'build', 'patchday', 'decompiled', h + '-single')
    target = os.path.join(out, 'all.reds')
    if not os.path.isfile(target):
        os.makedirs(out, exist_ok=True)
        # Never use --dump-files/-f: compiled mods embed absolute source paths.
        # The CLI follows those paths even outside -o and can overwrite the
        # installed game scripts. Single-file output ignores the file index.
        with tempfile.TemporaryDirectory(prefix='redscript-decompile-') as tmp:
            staged = os.path.join(tmp, 'all.reds')
            subprocess.run([CLI, 'decompile', '-i', os.path.abspath(bundle), '-o', staged],
                           check=True, capture_output=True)
            with open(staged, 'rb') as src, open(target + '.tmp', 'wb') as dst:
                dst.write(src.read())
            os.replace(target + '.tmp', target)
    return out


def block_end(text, start):
    """Index after the brace block opening at or after start (strings are rare enough in signatures)."""
    i = text.index('{', start)
    depth = 0
    in_str = False
    while i < len(text):
        c = text[i]
        if in_str:
            if c == '\\':
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return len(text)


def index_scripts(root):
    """{class: {'extends': base, 'methods': {name: [(signature, body hash)]}, 'fields': set}}"""
    classes = {}
    for dp, _, files in os.walk(root):
        for f in files:
            with open(os.path.join(dp, f), encoding='utf-8', errors='replace') as fh:
                text = fh.read()
            for m in CLASS.finditer(text):
                try:
                    end = block_end(text, m.end())
                except ValueError:
                    continue
                body = text[m.end():end]
                c = classes.setdefault(m.group(1), dict(extends=m.group(2), methods={}, fields=set()))
                pos = 0
                while True:
                    fm = FUNC.search(body, pos)
                    if not fm:
                        break
                    sig = ' '.join((fm.group(2) + ' -> ' + (fm.group(3) or 'Void').strip()).split())
                    line_end = body.find('\n', fm.end())
                    rest = body[fm.end():line_end if line_end >= 0 else len(body)]
                    if '{' in rest:
                        fend = block_end(body, fm.end())
                        fbody = body[fm.end():fend]
                        pos = fend
                    else:
                        fbody = ''
                        pos = fm.end()
                    digest = hashlib.sha1(' '.join(fbody.split()).encode()).hexdigest()[:12]
                    c['methods'].setdefault(fm.group(1), []).append((sig, digest))
                for fd in re.finditer(r'^\s*(?:(?:public|private|protected|native|persistent|const|inline|edit|let|editable)\s+)*let\s+(\w+)\s*:', body, re.M):
                    c['fields'].add(fd.group(1))
    return classes


def port_targets():
    out = []
    root = os.path.join(REPO, 'mods', 'redscript')
    for dp, _, files in os.walk(root):
        for f in files:
            if not f.endswith('.reds'):
                continue
            path = os.path.join(dp, f)
            rel = os.path.relpath(path, REPO).replace('\\', '/')
            lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
            for i, line in enumerate(lines):
                m = ANNOT.search(line)
                if not m or '//' in line[:m.start()] or line.lstrip().startswith(('*', '/*')):
                    continue
                kind, cls = m.group(1), m.group(2)
                # A signature may run over several lines (one parameter per line).
                tail = ' '.join(lines[i:i + 12])
                fm = FUNC.search(tail)
                fd = re.search(r'\b(?:let|var)\s+(\w+)\s*:', tail)
                name = fm.group(1) if fm and kind != 'addField' else (fd.group(1) if fd else '?')
                sig = ' '.join((fm.group(2) + ' -> ' + (fm.group(3) or 'Void').strip()).split()) if fm else ''
                out.append(dict(kind=kind, cls=cls, name=name, sig=sig, where=f'{rel}:{i + 1}'))
    # CET mods hook script methods by name (Observe / ObserveAfter / ObserveBefore / Override): a
    # method a patch removed is simply never called again, without an error anywhere.
    cet = re.compile(r'\b(Observe|ObserveAfter|ObserveBefore|Override)\(\s*["\'](\w+)["\']\s*,\s*["\'](\w+)')
    for dp, _, files in os.walk(os.path.join(REPO, 'mods', 'cet')):
        for f in files:
            if not f.endswith('.lua'):
                continue
            path = os.path.join(dp, f)
            rel = os.path.relpath(path, REPO).replace('\\', '/')
            for i, line in enumerate(open(path, encoding='utf-8', errors='replace')):
                if line.lstrip().startswith('--'):
                    continue
                for m in cet.finditer(line):
                    out.append(dict(kind='cet' + m.group(1), cls=m.group(2), name=m.group(3), sig='',
                                    where=f'{rel}:{i + 1}'))
    return out


def lookup(classes, cls, name):
    """The method as the class or one of its bases declares it."""
    seen = 0
    while cls and cls in classes and seen < 32:
        if name in classes[cls]['methods']:
            return classes[cls]['methods'][name]
        cls = classes[cls]['extends']
        seen += 1
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--old', default=OLD)
    ap.add_argument('--new', default=os.path.join(GAME, 'r6', 'cache', 'final.redscripts'))
    args = ap.parse_args()
    old = index_scripts(decompile(args.old))
    new = index_scripts(decompile(args.new))
    targets = port_targets()
    counts = {}
    for t in targets:
        if t['kind'] in ('replaceGlobal',):
            state = 'global (not checked)'
        elif t['kind'].startswith('cet') and not lookup(old, t['cls'], t['name']):
            # A native class or method the scripts never declare: nothing to compare.
            state = 'ok'
        elif t['cls'] not in new:
            state = 'GONE: class' if t['cls'] in old else 'class not in either bundle'
        elif t['kind'] == 'addField':
            state = 'CLASH: the game now has this field' if t['name'] in new[t['cls']]['fields'] and \
                t['name'] not in old.get(t['cls'], {}).get('fields', set()) else 'ok'
        elif t['kind'] == 'addMethod':
            state = 'CLASH: the game now has this method' if lookup(new, t['cls'], t['name']) and \
                not lookup(old, t['cls'], t['name']) else 'ok'
        else:
            o, n = lookup(old, t['cls'], t['name']), lookup(new, t['cls'], t['name'])
            if not n:
                state = 'GONE: method'
            elif not o:
                state = 'new in this bundle (was not in the old one)'
            elif sorted(s for s, _ in o) != sorted(s for s, _ in n):
                state = 'SIGNATURE CHANGED: ' + '; '.join(s for s, _ in n)
            elif sorted(d for _, d in o) != sorted(d for _, d in n):
                state = 'BODY CHANGED'
            else:
                state = 'unchanged'
        key = state.split(':')[0]
        counts[key] = counts.get(key, 0) + 1
        if state not in ('unchanged', 'ok'):
            print(f"  {state:40s} {t['kind']:13s} {t['cls']}.{t['name']}  <- {t['where']}")
    print(f'{len(targets)} port annotations: ' + ', '.join(f'{k} {v}' for k, v in sorted(counts.items())))
    bad = sum(v for k, v in counts.items() if k.startswith(('GONE', 'SIGNATURE', 'CLASH')))
    print('SCRIPT DIFF', 'OK' if not bad else f'{bad} broken', '' if not counts.get('BODY CHANGED') else
          f"({counts['BODY CHANGED']} wrapped bodies changed: re-read them)")
    sys.exit(0 if not bad else 1)


if __name__ == '__main__':
    main()
