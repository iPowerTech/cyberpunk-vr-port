"""Compare two WolvenKit JSON files modulo handle numbering.

    py -3.13 -B tools/patchday/json_canon.py <a.json> <b.json> [--show 20]

WolvenKit numbers handles in the order it writes them, and a script that appends handles numbers them past the last
one; the same resource can therefore come back with different HandleIds. Both files are renumbered in document
order (a HandleRefId takes the number of the HandleId it names) and the Header block and the CR2W version fields
are dropped, then the leaves are compared.
"""
import argparse
import json
import sys

sys.setrecursionlimit(200000)

MISSING = object()


def canon(j):
    ids = {}

    def num(v):
        if v not in ids:
            ids[v] = str(len(ids))
        return ids[v]

    def walk(v):
        if isinstance(v, dict):
            out = {}
            for k, x in v.items():
                if k == 'HandleId' or k == 'HandleRefId':
                    out[k] = num(x)
                else:
                    out[k] = walk(x)
            return out
        if isinstance(v, list):
            return [walk(x) for x in v]
        return v
    body = {k: v for k, v in j.items() if k != 'Header'}
    data = body.get('Data', {})
    if isinstance(data, dict):
        data = {k: v for k, v in data.items() if k not in ('Version', 'BuildVersion')}
        body['Data'] = data
    return walk(body)


def flatten(j, path=(), out=None):
    if out is None:
        out = {}
    if isinstance(j, dict):
        for k, v in j.items():
            flatten(v, path + (k,), out)
    elif isinstance(j, list):
        for i, v in enumerate(j):
            flatten(v, path + (i,), out)
    else:
        out[path] = j
    return out


def compare(a, b):
    fa, fb = flatten(canon(a)), flatten(canon(b))
    return sorted((k for k in set(fa) | set(fb) if fa.get(k, MISSING) != fb.get(k, MISSING)
                   and not (k[-1] == 'Flags' and {fa.get(k), fb.get(k)} <= {62 << 16, 114 << 16})), key=str), fa, fb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--show', type=int, default=20)
    x = ap.parse_args()
    with open(x.a, encoding='utf-8-sig') as f:
        a = json.load(f)
    with open(x.b, encoding='utf-8-sig') as f:
        b = json.load(f)
    d, fa, fb = compare(a, b)
    print(f'{len(d)} leaves differ')
    for k in d[:x.show]:
        print('  ', '/'.join(map(str, k))[-150:], '|', fa.get(k, '<missing>'), '|', fb.get(k, '<missing>'))
    sys.exit(1 if d else 0)


if __name__ == '__main__':
    main()
