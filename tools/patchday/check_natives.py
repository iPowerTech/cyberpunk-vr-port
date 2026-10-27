"""Native declarations in redscript checked against the game's RTTI, offline -- what the game's script validator
would reject at startup (RED4ext then stops the game with "scripts contain invalid native definitions").

    py -3.13 -B tools/patchday/check_natives.py [--dump <rtti_dump.json>] [--plugins <src dir> ...] <dir or .reds> ...

The RTTI is RED4.RTTIDumper's JSON of the running game (every class: native name, parent, properties); the
script names of native classes (GameObject for gameObject) come from the ALIAS lines of the SDK headers
generated from the same build. Classes and fields a plugin registers at runtime are not in the dump: names
from RTTI_DEFINE_CLASS / RTTI_MAP_TYPE_NAME in --plugins sources count as existing classes (their fields are
not checked), and a missing field of a class a plugin RTTI_EXPAND_CLASSes is listed as unverified, not as an
error. For every `native class/struct X extends Y` and every `native let p` (also through @addField(X)):
X must exist, Y must be X's parent, p must be a property of X or of an ancestor.

That alone also lists what was never valid and the frameworks get away with (Codeware re-parents structs in its
validation hook; classes and properties the dumper's plugin-less run does not see). With --old-sdk (the generated
headers of the previous build) only what the new build broke is an error: a class or property the old RTTI had
and the new one lacks, a parent the new RTTI changed; the rest is listed as `pre-existing`.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FW = os.path.join(REPO, 'build', 'patchday', 'framework-src')

NAME_ALIAS = re.compile(r'static constexpr const char\* NAME = "(\w+)";\s*static constexpr const char\* ALIAS = "(\w+)";')
DEFINE = re.compile(r'RTTI_DEFINE_CLASS\(\s*([\w:]+)\s*(?:,\s*"(\w+)")?')
MAP_NAME = re.compile(r'RTTI_MAP_TYPE_NAME\(\s*([\w:]+)\s*,\s*"(\w+)"')
EXPAND = re.compile(r'RTTI_EXPAND_CLASS\(\s*Red::(\w+)')
PROJECT = re.compile(r'RTTI_DEFINE_CLASS\(\s*[\w:]+\s*,\s*App::Project::Name')

DECL = re.compile(r'\bnative\s+(?:class|struct)\s+(\w+)(?:\s+extends\s+(\w+))?\s*\{', re.M)
ADDFIELD = re.compile(r'@addField\((\w+)\)\s*(?:public\s+|protected\s+|private\s+)?(?:persistent\s+)?native\s+let\s+(\w+)\s*:\s*([^;]+);')


def load_aliases(sdk):
    to_native = {}
    root = os.path.join(sdk, 'include', 'RED4ext', 'Scripting', 'Natives', 'Generated')
    for d, _, ns in os.walk(root):
        for n in ns:
            if n.endswith('.hpp'):
                for native, alias in NAME_ALIAS.findall(open(os.path.join(d, n), encoding='utf-8', errors='replace').read()):
                    to_native[alias] = native
    return to_native


HEADER_STRUCT = re.compile(r'^struct\s+(?:__declspec\(\w+\(\w+\)\)\s+)?(\w+)\s*(?::\s*([\w:]+))?\s*\{(.*?)^\};',
                           re.S | re.M)
HEADER_NAME = re.compile(r'static constexpr const char\* NAME = "(\w+)";')
HEADER_FIELD = re.compile(r'^\s+([\w:<>, *]+?)\s+(\w+)((?:\[[^\]]*\])?);\s*//', re.M)


def load_headers(root):
    """{native name: dict(parent, props)} from generated SDK headers (props: the named fields only)."""
    raw, by_cpp = {}, {}
    for d, _, ns in os.walk(root):
        for n in ns:
            if not n.endswith('.hpp'):
                continue
            text = open(os.path.join(d, n), encoding='utf-8', errors='replace').read()
            space = '::'.join(re.findall(r'^namespace ([\w:]+)', text, re.M)[1:])   # below RED4ext
            for m in HEADER_STRUCT.finditer(text):
                nm = HEADER_NAME.search(m.group(3))
                if not nm:
                    continue
                cpp = (space + '::' + m.group(1)) if space else m.group(1)
                by_cpp[cpp] = nm.group(1)
                raw[nm.group(1)] = (space, m.group(2),
                                    [(f, t + n) for t, f, n in HEADER_FIELD.findall(m.group(3)) if not f.startswith('unk')])
    out = {}
    for name, (space, base, fields) in raw.items():
        parent = ''
        if base:
            # a base the generated set does not hold (hand-written in the SDK) is named like the
            # generated ones: namespaces run together, then the type (ent::ReplicatedItem = entReplicatedItem)
            parent = by_cpp.get(base) or by_cpp.get(space + '::' + base) or base.replace('::', '')
        out[name] = dict(parent=parent, props=[dict(name=f, type=t) for f, t in fields])
    return out


def load_plugins(dirs):
    names, expanded = set(), set()
    for root in dirs:
        for d, _, ns in os.walk(root):
            for n in ns:
                if not n.endswith(('.hpp', '.cpp', '.h')):
                    continue
                text = open(os.path.join(d, n), encoding='utf-8', errors='replace').read()
                mapped = dict(MAP_NAME.findall(text))
                for cpp, name in DEFINE.findall(text):
                    names.add(name or mapped.get(cpp) or cpp.split('::')[-1])
                names.update(mapped.values())
                expanded.update(EXPAND.findall(text))
                if PROJECT.search(text):
                    names.update(('ArchiveXL', 'TweakXL'))
    return names, expanded


def scan(paths):
    for p in paths:
        files = [p] if p.endswith('.reds') else [os.path.join(d, n) for d, _, ns in os.walk(p) for n in ns
                                                 if n.endswith('.reds')]
        for f in sorted(files):
            yield f, open(f, encoding='utf-8-sig', errors='replace').read()


def body_fields(text, start):
    depth, i = 0, start
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    return re.findall(r'\bnative\s+let\s+(\w+)\s*:\s*([^;]+);', text[start:i])


def dump_type(t):
    """The RTTI dump's type string as a (kind, inner) tree comparable with a script type; None = not compared."""
    for pre, kind in (('array:', 'array'), ('handle:', 'ref'), ('whandle:', 'wref')):
        if t.startswith(pre):
            inner = dump_type(t[len(pre):])
            return inner and (kind, inner)
    if t.startswith('static:'):
        inner = dump_type(t.split(',', 1)[1])
        return inner and ('array', inner)
    m = re.match(r'\[\d+\](.+)', t)
    if m:
        inner = dump_type(m.group(1))
        return inner and ('array', inner)
    if t.startswith('raRef:'):
        return 'ResourceAsyncRef'
    if t.startswith('rRef:'):
        return 'ResourceRef'
    if t.startswith('curveData:'):
        return 'CurveData' + t[len('curveData:'):]
    if ':' in t:
        return None
    return t


def script_type(t, native):
    t = t.strip()
    m = re.match(r'(array|ref|wref|script_ref)<(.+)>$', t)
    if m:
        inner = script_type(m.group(2), native)
        return inner and (m.group(1), inner)
    return native(t) if re.match(r'\w+$', t) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('paths', nargs='+')
    ap.add_argument('--dump', default=os.path.join(REPO, 'build', 'patchday', 'rtti', 'rtti_dump-6AD55DAF.json'))
    ap.add_argument('--sdk', default=os.path.join(FW, 'RED4ext.SDK'))
    ap.add_argument('--old-sdk', default=None, help='generated headers of the previous build (a .../Generated dir)')
    ap.add_argument('--plugins', nargs='*', default=[os.path.join(FW, n, 'src') for n in
                                                     ('cp2077-codeware', 'cp2077-archive-xl', 'cp2077-tweak-xl')]
                    + [os.path.join(REPO, 'src')])
    a = ap.parse_args()
    dump = {c['name']: c for c in json.load(open(a.dump, encoding='utf-8'))['classes']}
    to_native = load_aliases(a.sdk)
    plugin_names, expanded = load_plugins(a.plugins)
    expanded_native = {to_native.get(e, e) for e in expanded} | expanded

    old = load_headers(a.old_sdk) if a.old_sdk else None
    new = load_headers(os.path.join(a.sdk, 'include', 'RED4ext', 'Scripting', 'Natives', 'Generated')) if old else None

    def native(script_name):
        return to_native.get(script_name, script_name)

    def chain(cls, rtti=dump):
        seen = 0
        while cls in rtti and seen < 64:
            yield cls
            cls = rtti[cls]['parent']
            seen += 1

    def has_field(cls, f, rtti=dump):
        return any(p['name'] == f for c in chain(cls, rtti) for p in rtti[c]['props'])

    def prop(cls, f, rtti=dump):
        return next((p for c in chain(cls, rtti) for p in rtti[c]['props'] if p['name'] == f), None)

    errors, unverified, pre, types = [], [], [], []

    def report(msg, broke):
        # broke: None = no old RTTI to judge by, else whether the old build had it right
        (errors if broke in (None, True) else pre).append(msg)

    def field(rel, line, script_cls, cls, p, t, what):
        if has_field(cls, p):
            # the script's type was right for the old build (that is what the frameworks were generated from):
            # a type the new build changed is what the validator now rejects
            if old is not None:
                po, pn = prop(cls, p, old), prop(cls, p, new)
                if po and pn and po['type'] != pn['type']:
                    types.append(f'{rel}:{line}: {what} {script_cls}.{p}: {t.strip()}; type {po["type"]} -> '
                                 f'{pn["type"]} (RTTI {prop(cls, p)["type"]})')
            return
        msg = f'{rel}:{line}: {what} {script_cls}.{p}: no such property'
        if old is not None and has_field(cls, p, old):
            errors.append(msg)          # the old RTTI had it: removed, whatever a plugin adds
        elif any(c in expanded_native for c in chain(cls)):
            unverified.append(msg)
        else:
            report(msg, None if old is None else False)

    for f, text in scan(a.paths):
        rel = os.path.relpath(f, REPO)
        for m in DECL.finditer(text):
            script_cls, base = m.group(1), m.group(2)
            cls = native(script_cls)
            line = text.count('\n', 0, m.start()) + 1
            if cls not in dump:
                if script_cls not in plugin_names and cls not in plugin_names:
                    report(f'{rel}:{line}: native class {script_cls} does not exist',
                           None if old is None else cls in old)
                continue
            want = dump[cls]['parent'] or None
            got = native(base) if base else None
            if got != want and not (got is None and want in ('ISerializable', 'IScriptable')):
                report(f'{rel}:{line}: {script_cls} extends {base}, the RTTI parent is {want}',
                       None if old is None else (cls in old and (old[cls]['parent'] or None) != want))
            for p, t in body_fields(text, text.index('{', m.end() - 1)):
                field(rel, line, script_cls, cls, p, t, 'native let')
        for m in ADDFIELD.finditer(text):
            script_cls, p, t = m.group(1), m.group(2), m.group(3)
            cls = native(script_cls)
            line = text.count('\n', 0, m.start()) + 1
            if cls not in dump:
                if script_cls not in plugin_names:
                    report(f'{rel}:{line}: @addField({script_cls}) native {p}: class does not exist',
                           None if old is None else cls in old)
                continue
            field(rel, line, script_cls, cls, p, t, '@addField native')
    print('\n'.join(errors) or 'no errors')
    print(f'{len(errors)} errors; {len(unverified)} unverified (fields of plugin-expanded classes):')
    print('\n'.join(unverified))
    print(f'{len(types)} property types the new build changed:')
    print('\n'.join(types))
    if old is not None:
        print(f'{len(pre)} pre-existing (the old build had them the same way):')
        print('\n'.join(pre))
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
