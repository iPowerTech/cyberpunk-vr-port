"""Engine struct field offsets the port reads, re-measured in a new build through the game's own code.

    py -3.13 -B tools/patchday/offsets.py index                       # on the old build (prepare)
    py -3.13 -B tools/patchday/offsets.py check --new <game dir> [--apply]

The 92 values in data/literal_overrides.json "not_rva" are not addresses, so remap.py leaves them
alone -- but most are FIELD OFFSETS into engine objects (the render registry +0x13BC4D0, a registry
entry +0x2F1D8, the CPU collector +0xC4DF0, the targeting component +0x31100 ...) and a patch that
changes a class layout moves them. The game's own code carries every one of them as a displacement or
immediate (`mov rax, [rcx+0x13BC4D0]`). `index` finds those carrier instructions in the old exe and
records, per carrier, its function (address-library anchor), the masked bytes of that function and an
instruction window around the carrier with the carrier's own operand masked too. `check` finds each
carrier's function in the new exe: unchanged masked code means the offset did not move; otherwise the
window is searched in the new function and the operand is read back -- that is the new offset. Votes
of all carriers of a value must agree. A "bound" (field + size, an overflow check in our code) with no
carrier of its own follows the nearest measured field below it (within 0x100 bytes).

--apply rewrites the values whose carriers agree (at least two votes, or the only carrier there is,
flagged for review) in src/ include/. Masks, sizes, hashes and foreign (CET) values are not fields and
are skipped.
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import pe  # noqa: E402
import sigs  # noqa: E402

OVERRIDES = os.path.join(HERE, 'data', 'literal_overrides.json')
FIELD = re.compile(r'field|offset|lock|flag|ring|record|pool|struct|cache|table|bound|profile|size', re.I)
NOT_FIELD = re.compile(r'mask|hash|CET|\(ours|effect id|pointer floor|alignment|image-span|ring capacity|descriptor count|'
                       r'^bound$|^size$', re.I)
MAX_CARRIERS = 12


def field_values():
    with open(OVERRIDES, encoding='utf-8') as f:
        j = json.load(f)
    return {int(k, 16): why for k, why in j['not_rva'].items() if FIELD.search(why) and not NOT_FIELD.search(why)}


def find_carriers(img, lib, v, func_cache):
    text = img.text_bytes()
    pat = struct.pack('<I', v)
    out = []
    pos = 0
    seen_funcs = set()
    while len(out) < MAX_CARRIERS:
        i = text.find(pat, pos)
        if i < 0:
            break
        pos = i + 1
        h = img.text.va + i
        f = img.function_at(h)
        if f is None or f[0] in seen_funcs:
            continue
        anchors = [x for x in lib.exact(f[0]) if len(lib.by_hash[x]) == 1]
        if not anchors:
            continue
        key = (f[0], f[1])
        if key not in func_cache:
            func_cache[key] = sigs.decode(img, f[0], f[1])
        insns = func_cache[key]
        for n, ins in enumerate(insns):
            if not (ins.rva <= h < ins.rva + ins.size) or h + 4 > ins.rva + ins.size:
                continue
            k = h - ins.rva
            raw = img.read(ins.rva, ins.size)
            if int.from_bytes(raw[k:k + 4], 'little') != v or ins.mask[k]:
                break
            # Window around it with the carried operand masked as well, so it still matches when
            # only the offset moved.
            lo, hi = max(0, n - 6), min(len(insns), n + 7)
            parts = []
            for m in insns[lo:hi]:
                r = img.read(m.rva, m.size)
                for b in range(m.size):
                    own = m is ins and k <= b < k + 4
                    parts.append('??' if m.mask[b] or own else f'{r[b]:02X}')
            out.append(dict(func=f[0], func_end=f[1], anchor=anchors[0], fhash=sigs.masked_hash(img, insns),
                            insn=ins.rva - f[0], byte=k, size=ins.size, window=' '.join(parts),
                            wofs=ins.rva - insns[lo].rva, mnemonic=f'{ins.mnemonic} {ins.op_str}'))
            seen_funcs.add(f[0])
            break
    return out


def index(args):
    img = pe.Image(pe.find_game_files(args.game)[0])
    lib = pe.AddressLibrary(pe.find_game_files(args.game)[1], img)
    fields = field_values()
    cache = {}
    data = {}
    for v, why in sorted(fields.items()):
        data[f'{v:#x}'] = dict(why=why, carriers=find_carriers(img, lib, v, cache))
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(dict(exe=img.timestamp, values=data), f, indent=1)
    have = sum(1 for d in data.values() if d['carriers'])
    print(f'{len(data)} field values, {have} with carriers in the game code '
          f'({sum(len(d["carriers"]) for d in data.values())} carriers) -> {args.out}')
    for k, d in data.items():
        if not d['carriers']:
            print(f'  no carrier: {k} ({d["why"]})')


def measure(new, nlib, c, cache, force_window=False):
    bases = nlib.by_hash.get(c['anchor'], [])
    if len(bases) != 1:
        return None, 'anchor lost'
    nf = new.function_at(bases[0])
    if nf is None or nf[0] != bases[0]:
        return None, 'function lost'
    key = (nf[0], nf[1])
    if key not in cache:
        cache[key] = sigs.decode(new, nf[0], nf[1])
    if not force_window and sigs.masked_hash(new, cache[key]) == c['fhash']:
        return 'same', 'unchanged code'
    hits = sigs.find_pattern(new, c['window'], nf[0], nf[1], limit=2)
    if len(hits) != 1:
        return None, 'window not found' if not hits else 'window ambiguous'
    at = hits[0] + c['wofs'] + c['byte']
    return int.from_bytes(new.read(at, 4), 'little'), 'read from the moved code'


def check(args):
    with open(args.index, encoding='utf-8') as f:
        idx = json.load(f)
    new = pe.Image(pe.find_game_files(args.new)[0])
    nlib = pe.AddressLibrary(pe.find_game_files(args.new)[1], new)
    cache = {}
    result = {}
    for k, d in idx['values'].items():
        v = int(k, 16)
        votes = collections.Counter()
        notes = collections.Counter()
        for c in d['carriers']:
            got, why = measure(new, nlib, c, cache, args.force_window)
            notes[why] += 1
            if got == 'same':
                votes[v] += 1
            elif got is not None:
                votes[got] += 1
        if not votes:
            result[v] = dict(status='unknown', why=d['why'], notes=dict(notes))
        elif len(votes) > 1:
            result[v] = dict(status='conflict', votes={f'{a:#x}': n for a, n in votes.items()}, why=d['why'])
        else:
            nv, n = votes.most_common(1)[0]
            status = 'unchanged' if nv == v else ('moved' if n >= 2 or len(d['carriers']) == 1 else 'moved-1vote')
            result[v] = dict(status=status, new=nv, votes=n, of=len(d['carriers']), why=d['why'])
    # Bounds and neighbours with no carrier follow the nearest field below them (<= 64 bytes).
    known = {v: r for v, r in result.items() if r['status'] in ('unchanged', 'moved')}
    for v, r in result.items():
        if r['status'] != 'unknown':
            continue
        below = [u for u in known if 0 < v - u <= 0x100]
        if below:
            u = max(below)
            r.update(status='follows', new=known[u]['new'] + (v - u), base=f'{u:#x}')
    st = collections.Counter(r['status'] for r in result.values())
    print('field offsets:', dict(st.most_common()))
    for v, r in sorted(result.items()):
        if r['status'] in ('unchanged',):
            continue
        extra = f"-> {r['new']:#x}" if 'new' in r else ''
        print(f"  {r['status']:11s} {v:#x} {extra} ({r['why']}) {r.get('votes', '')}/{r.get('of', '')} "
              f"{r.get('notes', '')}{r.get('base', '')}")
    if args.apply:
        rewrite({v: r['new'] for v, r in result.items() if r['status'] in ('moved', 'moved-1vote', 'follows')
                 and r['new'] != v})
    # A value that never had a carrier (0x274248 on 2.31) cannot be measured in any build; only
    # carriers that disagree or were lost, and moves, are news.
    lost = sum(1 for v, r in result.items() if r['status'] == 'unknown' and idx['values'][f'{v:#x}']['carriers'])
    bad = st.get('conflict', 0) + lost
    moved = st.get('moved', 0) + st.get('moved-1vote', 0)
    print('FIELD OFFSETS', 'OK' if not bad and not moved else f'{bad} need a human, {moved} moved')
    sys.exit(0 if not bad and not moved else 2)


def rewrite(moves):
    """Every literal of a moved value in src/ include/ (by value; a field offset is one thing)."""
    if not moves:
        print('nothing to rewrite')
        return
    lit = re.compile(r'(?<![\w.])0[xX]([0-9A-Fa-f]+)([uUlL]*)\b')
    n = 0
    for d in ('src', 'include'):
        for dp, _, files in os.walk(os.path.join(REPO, d)):
            for f in files:
                if not f.endswith(('.cpp', '.hpp', '.h', '.inc')):
                    continue
                p = os.path.join(dp, f)
                with open(p, encoding='utf-8', newline='') as fh:
                    text = fh.read()

                def sub(m):
                    nonlocal n
                    v = int(m.group(1), 16)
                    if v not in moves:
                        return m.group(0)
                    n += 1
                    digits = m.group(1)
                    upper = any(c in 'ABCDEF' for c in digits) or not any(c in 'abcdef' for c in digits)
                    return '0x' + (f'{moves[v]:X}' if upper else f'{moves[v]:x}') + m.group(2)
                new = lit.sub(sub, text)
                if new != text:
                    with open(p, 'w', encoding='utf-8', newline='') as fh:
                        fh.write(new)
    print(f'rewrote {n} literal(s) of {len(moves)} moved field offset(s)')
    # The hand list names these values; it has to name the new ones for the next inventory.
    with open(OVERRIDES, encoding='utf-8') as f:
        j = json.load(f)
    j['not_rva'] = {(f'{moves[int(k, 16)]:#x}' if int(k, 16) in moves else k): why for k, why in j['not_rva'].items()}
    with open(OVERRIDES, 'w', encoding='utf-8') as f:
        json.dump(j, f, indent=2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    i = sub.add_parser('index')
    i.add_argument('--game', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    i.add_argument('--out', default=os.path.join(HERE, 'data', 'offset_carriers-2.31.json'))
    c = sub.add_parser('check')
    c.add_argument('--new', required=True)
    c.add_argument('--index', default=os.path.join(HERE, 'data', 'offset_carriers-2.31.json'))
    c.add_argument('--apply', action='store_true')
    c.add_argument('--force-window', action='store_true',
                   help='TEST: read every carrier through its window even when the code is unchanged')
    args = ap.parse_args()
    {'index': index, 'check': check}[args.cmd](args)


if __name__ == '__main__':
    main()
