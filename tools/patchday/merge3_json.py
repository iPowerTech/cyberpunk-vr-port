"""Three-way merge of the base files our archive replaces: 2.31 original (old), the patch's version (new), ours.

    py -3.13 -B tools/patchday/merge3_json.py classify [--root build/patchday/overrides-2.5]
    py -3.13 -B tools/patchday/merge3_json.py merge    [--root ...] [--only <substring>]
    py -3.13 -B tools/patchday/merge3_json.py remap <json in> <json out>   # our own (non-override) assets

Works on WolvenKit JSON (cp77tools convert serialize; json_map.json maps a game path to the three files). The
Header block and the CR2W version fields are tooling noise and are ignored. A file is

  SAME     -- the patch changed nothing but the cooking (buffer version 62 -> 114, CRCs): keep ours as is;
  REMAP    -- the patch only moved physics filter bits (or ours carries masks the patch would have moved):
              ours with the same bit move;
  MERGE    -- the patch and we changed different leaves: a structural merge (below) of old, ours and new;
  CONFLICT -- both changed the same leaf to different values: needs a look (merge writes ours' value and lists it).

2.5 added the collision group PhotomodeCensorship as group 19 (engine\\physics\\collision_groups.json, 30 bytes
longer = one 19-character CName), so every query group bit moved up by one: Visibility 19 -> 20,
Audible 20 -> 21, Interaction 21 -> 22, ... CDPR recooked every resource whose filterData carries such a bit;
ours are moved the same way before merging (bits 0-18 and 63 stay).

The structural merge descends dicts by key and lists by element: a list neither side resized is merged by index; a
list whose elements all carry a unique identity (component/appearance name with its type, a material parameter
name, a depot path) is aligned by that identity, so an element the patch inserted does not shift our edits; any
other list resized by both sides is a CONFLICT. Leaves both sides set to the same value are not conflicts.
"""
import argparse
import collections
import copy
import json
import os
import sys

sys.setrecursionlimit(200000)

NOISE = {('Data', 'Version'), ('Data', 'BuildVersion')}
# a buffer's Flags carry its serialization version in the high word: 2.31 cooked 62, 2.5 cooks 114 (the engine
# still reads 62, see the `cmp [buffer+28h], 3Eh` converters)
BUFFER_VERSIONS = {62 << 16, 114 << 16}
NEW_GROUP = 19
MISSING = object()


def remap_mask(v):
    low = v & ((1 << NEW_GROUP) - 1)
    top = v & (1 << 63)
    mid = (v & ~((1 << NEW_GROUP) - 1) & ~(1 << 63)) << 1
    if mid >> 63:
        raise ValueError(f'mask {v:#x} has a bit at 62 that would collide with 63')
    return low | mid | top


def remap(j):
    """physics filter masks moved to the 2.5 group numbering, in place; returns the number of masks changed"""
    n = 0
    if isinstance(j, dict):
        for k, v in j.items():
            if k in ('mask1', 'mask2') and isinstance(v, (str, int)) and not isinstance(v, bool):
                w = remap_mask(int(v))
                if w != int(v):
                    j[k] = str(w) if isinstance(v, str) else w
                    n += 1
            else:
                n += remap(v)
    elif isinstance(j, list):
        for v in j:
            n += remap(v)
    return n


def load(p):
    with open(p, encoding='utf-8') as f:
        return json.load(f)


def body(j):
    return {k: v for k, v in j.items() if k != 'Header'}


def flatten(j, path=(), out=None):
    if out is None:
        out = {}
    if isinstance(j, dict):
        for k, v in j.items():
            flatten(v, path + (k,), out)
        if not j:
            out[path] = {}
    elif isinstance(j, list):
        for i, v in enumerate(j):
            flatten(v, path + (i,), out)
        if not j:
            out[path] = []
    else:
        out[path] = j
    return out


def noise(k, a, b):
    return k in NOISE or (bool(k) and k[-1] == 'Flags' and {a, b} <= BUFFER_VERSIONS)


def diff(a, b):
    return {k for k in set(a) | set(b)
            if a.get(k, MISSING) != b.get(k, MISSING) and not noise(k, a.get(k), b.get(k))}


def fmt(path):
    return '/'.join(str(x) for x in path)


# ---- structural merge ---------------------------------------------------------------------------------------

def ident(e):
    if not isinstance(e, dict):
        return None
    d = e.get('Data', e) if 'HandleId' in e else e
    if not isinstance(d, dict):
        return None
    name = d.get('name')
    if isinstance(name, dict) and '$value' in name:
        return ('name', d.get('$type'), name['$value'])
    if isinstance(d.get('DepotPath'), dict):
        return ('path', d['DepotPath'].get('$value'))
    keys = [k for k in d if k != '$type']
    if len(keys) == 1:
        return ('param', keys[0])
    anim = d.get('animSet')
    if isinstance(anim, dict) and isinstance(anim.get('DepotPath'), dict):
        return ('anim', anim['DepotPath'].get('$value'), d.get('priority'))
    # an element without a name of its own (the entity chunk of compiledData) is still unique by its type
    if '$type' in d:
        return ('type', d['$type'])
    return None


def keyed(*lists):
    out = []
    for lst in lists:
        ks = [ident(e) for e in lst]
        if any(k is None for k in ks) or len(set(ks)) != len(ks):
            return None
        out.append(ks)
    return out


class Merger:
    def __init__(self):
        self.conflicts = []

    def merge(self, o, a, b, path=()):
        if a == o:
            return b
        if b == o or a == b:
            return a
        if isinstance(a, dict) and isinstance(b, dict) and (isinstance(o, dict) or o is MISSING):
            oo = o if isinstance(o, dict) else {}
            out = {}
            for k in list(b) + [k for k in a if k not in b]:
                v = self.merge(oo.get(k, MISSING), a.get(k, MISSING), b.get(k, MISSING), path + (k,))
                if v is not MISSING:
                    out[k] = v
            return out
        if isinstance(o, list) and isinstance(a, list) and isinstance(b, list):
            return self.merge_list(o, a, b, path)
        self.conflicts.append((fmt(path), short(o), short(a), short(b)))
        return a

    def merge_list(self, o, a, b, path):
        if len(o) == len(a) == len(b):
            return [self.merge(o[i], a[i], b[i], path + (i,)) for i in range(len(o))]
        ks = keyed(o, a, b)
        if ks:
            return self.merge_keyed(o, a, b, ks, path)
        if len(o) == len(b):
            # only we resized: index merge with our tail (a deletion in the middle shows as shifted content, which
            # merges because the patch did not touch those elements; a conflict otherwise)
            n = min(len(a), len(o))
            return [self.merge(o[i], a[i], b[i], path + (i,)) for i in range(n)] + a[len(o):]
        if len(o) == len(a):
            n = min(len(b), len(o))
            return [self.merge(o[i], a[i], b[i], path + (i,)) for i in range(n)] + b[len(o):]
        self.conflicts.append((fmt(path), f'list {len(o)}', f'list {len(a)}', f'list {len(b)}'))
        return a

    def merge_keyed(self, o, a, b, ks, path):
        ko, ka, kb = ks
        mo, ma = dict(zip(ko, o)), dict(zip(ka, a))
        out, keys = [], []
        for k, e in zip(kb, b):
            if k in mo and k not in ma:
                if e != mo[k]:
                    self.conflicts.append((fmt(path + (str(k),)), 'kept', 'deleted by us', 'changed by patch'))
                continue
            v = self.merge(mo.get(k, MISSING), ma.get(k, MISSING), e, path + (str(k),))
            if v is not MISSING:
                out.append(v)
                keys.append(k)
        # elements only we added go after the element that precedes them in ours
        kbs = set(kb)
        for i, k in enumerate(ka):
            if k in mo or k in kbs:
                continue
            prev = next((ka[j] for j in range(i - 1, -1, -1) if ka[j] in keys), None)
            at = keys.index(prev) + 1 if prev is not None else 0
            out.insert(at, ma[k])
            keys.insert(at, k)
        return out


def short(v):
    if v is MISSING:
        return '<missing>'
    s = json.dumps(v)
    return s if len(s) < 120 else s[:117] + '...'


def kflatten(j, path=(), out=None):
    """flatten with list elements addressed by identity where the whole list has one (see ident), so a leaf keeps
    its address when an element is inserted before it; HandleIds are numbering, not content, and are left out"""
    if out is None:
        out = {}
    if isinstance(j, dict):
        for k, v in j.items():
            if k not in ('HandleId', 'HandleRefId'):
                kflatten(v, path + (k,), out)
    elif isinstance(j, list):
        ks = keyed(j)
        for i, v in enumerate(j):
            kflatten(v, path + (str(ks[0][i]) if ks else i,), out)
    else:
        out[path] = j
    return out


def verify(o, a, b, res):
    """leaves of the patch (o -> b) and of ours (o -> a) that the result does not carry; a leaf both changed is
    checked against ours"""
    fo, fa, fb, fr = (kflatten(x) for x in (o, a, b, res))
    db, da = diff(fo, fb), diff(fo, fa)
    lost_patch = sorted(fmt(k) for k in db - da if fr.get(k, MISSING) != fb.get(k, MISSING)
                        and not noise(k, fr.get(k), fb.get(k)))
    lost_ours = sorted(fmt(k) for k in da if fr.get(k, MISSING) != fa.get(k, MISSING)
                       and not noise(k, fr.get(k), fa.get(k)))
    return lost_patch, lost_ours


def handle_check(j):
    """HandleIds unique, every HandleRefId defined earlier in document order"""
    seen, errors = set(), []

    def walk(v, path):
        if isinstance(v, dict):
            if 'HandleId' in v:
                if v['HandleId'] in seen:
                    errors.append(f'duplicate HandleId {v["HandleId"]} at {fmt(path)}')
                seen.add(v['HandleId'])
            if 'HandleRefId' in v and v['HandleRefId'] not in seen and v['HandleRefId'] != '-1':
                errors.append(f'HandleRefId {v["HandleRefId"]} before its HandleId at {fmt(path)}')
            for k, x in v.items():
                walk(x, path + (k,))
        elif isinstance(v, list):
            for i, x in enumerate(v):
                walk(x, path + (i,))
    walk(j, ())
    return errors


# ---- commands -------------------------------------------------------------------------------------------------

def classify_one(root, files):
    o, n, u = (body(load(os.path.join(root, files[s]))) for s in ('old', 'new', 'ours'))
    fn = flatten(n)
    dn = diff(flatten(o), fn)
    ro, ru = copy.deepcopy(o), copy.deepcopy(u)
    remap(ro)
    moved = remap(ru)
    if not dn:
        return ('REMAP' if moved else 'SAME'), dn, set(), ro, n, ru, []
    if not diff(flatten(ro), fn):
        return 'REMAP', dn, set(), ro, n, ru, []
    du = diff(flatten(ro), flatten(ru))
    m = Merger()
    res = m.merge(ro, ru, n)
    return ('CONFLICT' if m.conflicts else 'MERGE'), dn, du, ro, n, ru, (res, m.conflicts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['classify', 'merge', 'remap'])
    ap.add_argument('args', nargs='*')
    ap.add_argument('--root', default=os.path.join('build', 'patchday', 'overrides-2.5'))
    ap.add_argument('--only')
    a = ap.parse_args()
    if a.cmd == 'remap':
        src, dst = a.args
        j = load(src)
        n = remap(j)
        with open(dst, 'w', encoding='utf-8') as f:
            json.dump(j, f, indent=2)
        print(f'{n} masks moved')
        return
    m = json.load(open(os.path.join(a.root, 'json_map.json'), encoding='utf-8'))
    report = []
    counts = collections.Counter()
    for p, files in sorted(m.items()):
        if a.only and a.only.lower() not in p.lower():
            continue
        kind, dn, du, ro, n, ru, merged = classify_one(a.root, files)
        counts[kind] += 1
        row = {'path': p, 'kind': kind, 'patch_leaves': sorted(map(fmt, dn)), 'our_leaves': sorted(map(fmt, du))}
        if merged:
            res, conflicts = merged
            row['conflicts'] = conflicts
        else:
            res, conflicts = (ru if kind == 'REMAP' else None), []
        if a.cmd == 'merge' and res is not None:
            errors = handle_check(res)
            # the result must keep every leaf the patch changed and every leaf we changed
            lost, lost_ours = verify(ro, ru, n, res) if kind != 'REMAP' else ([], [])
            out = os.path.join(a.root, 'merged', p + '.json')
            os.makedirs(os.path.dirname(out), exist_ok=True)
            header = load(os.path.join(a.root, files['new'])).get('Header')
            with open(out, 'w', encoding='utf-8') as f:
                json.dump({'Header': header, **res}, f, indent=2)
            row.update(handle_errors=errors[:20], n_handle_errors=len(errors), out=out,
                       lost_patch_leaves=lost[:20], n_lost=len(lost), lost_our_leaves=lost_ours[:20],
                       n_lost_ours=len(lost_ours))
        report.append(row)
        if kind != 'SAME':
            extra = ''
            if a.cmd == 'merge' and 'out' in row:
                extra = (f" handle_errors {row['n_handle_errors']} lost_patch {row['n_lost']}"
                         f" lost_ours {row['n_lost_ours']}")
            print(f"{kind:8} patch {len(dn):6} ours {len(du):6} conflicts {len(conflicts):4}{extra}  {p}")
            for c in conflicts[:3]:
                print('            ', ' | '.join(str(x)[:90] for x in c))
            for e in row.get('handle_errors', [])[:3]:
                print('            ', e[:160])
            for e in row.get('lost_patch_leaves', [])[:3]:
                print('             lost patch', e[:160])
            for e in row.get('lost_our_leaves', [])[:3]:
                print('             lost ours', e[:160])
    print(dict(counts))
    with open(os.path.join(a.root, f'{a.cmd}.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1, default=str)


if __name__ == '__main__':
    main()
