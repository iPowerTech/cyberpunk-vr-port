"""Patch day: carry every address in the source from the old game build to the new one.

    py -3.13 -B tools/patchday/remap.py --new "<game dir>"            # report only
    py -3.13 -B tools/patchday/remap.py --new "<game dir>" --apply    # rewrite src/ include/

Reads the inventory (tools/patchday/data/rva_db-<old>.json, built by inventory.py against the old exe
kept in build/patchday/base-<ver>/), the OLD exe and library from that snapshot and the NEW ones from
--new, and decides for every address:

  same          unchanged RVA, unchanged code
  same-code     the function moved, its masked bytes did not -> same offset inside it
  moved-in-func the function changed, the instruction window around the site was found in it once
  fragment      a cold fragment of a function, re-found by its window among the new root's fragments
  sig-global    no usable anchor; the masked window matches exactly once in the new .text
  data-exact    .rdata/.data entry re-found by its library hash (vtables also by their slots)
  data-delta    data past a library entry, same distance from it (checked by code xrefs when any)
  ambiguous / changed / lost   -> NOT rewritten, listed for a human (IDA on the new exe)

Guards in the source are re-derived only where the code they cover is unchanged after masking:
FNV triples get the new hash, Site hex strings and byte arrays the new bytes. A guard over changed code
keeps its old value, so the feature behind it stays off until someone looks.

--apply also rewrites include/Core/GameBuild.hpp (the build the DLL accepts) and lists every address
left unmapped there, which the detour registry refuses to hook.
"""
import argparse
import collections
import datetime
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe  # noqa: E402
import sigs  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
RELIABLE = {'same', 'same-code', 'moved-in-func', 'fragment', 'sig-global', 'data-exact', 'data-delta',
            'data-xref', 'text-end', 'changed-func', 'sig-near'}
# Rewritten, but listed for review: the address is right, the code at it is not the old code.
REVIEW = {'changed-func', 'data-delta', 'sig-global', 'sig-near'}


class Remapper:
    def __init__(self, db, old_img, old_lib, new_img, new_lib):
        self.db, self.old, self.olib, self.new, self.nlib = db, old_img, old_lib, new_img, new_lib
        self._ofunc, self._nfunc = {}, {}
        self._fragments = None

    # ---- decode caches ------------------------------------------------------------------------
    def old_insns(self, f):
        k = (f[0], f[1])
        if k not in self._ofunc:
            self._ofunc[k] = sigs.decode(self.old, f[0], f[1])
        return self._ofunc[k]

    def new_insns(self, f):
        k = (f[0], f[1])
        if k not in self._nfunc:
            self._nfunc[k] = sigs.decode(self.new, f[0], f[1])
        return self._nfunc[k]

    def new_fragments(self, root):
        if self._fragments is None:
            self._fragments = collections.defaultdict(list)
            b, e, _ = self.new.pdata
            for begin, end in zip(b.tolist(), e.tolist()):
                f = self.new.function_at(begin)
                if f and f[2] != f[0]:
                    self._fragments[f[2]].append((f[0], f[1]))
        return self._fragments.get(root, [])

    def predict(self, v):
        """Where v should be if its neighbourhood moved as a block: the nearest unique library
        entry at or below it, carried to the new build, plus the old distance."""
        i = int(self.olib.rvas.searchsorted(v, side='right')) - 1
        while i >= 0 and v - int(self.olib.rvas[i]) < 0x20000:
            h = int(self.olib.hashes[i])
            if len(self.olib.by_hash[h]) == 1 and len(self.nlib.by_hash.get(h, ())) == 1:
                return self.nlib.by_hash[h][0] + (v - int(self.olib.rvas[i]))
            i -= 1
        return None

    def new_anchor(self, a):
        if not a:
            return []
        return self.nlib.by_hash.get(a['hash'], [])

    # ---- one address --------------------------------------------------------------------------
    def map_code(self, e):
        v = e['rva']
        a = e.get('anchor')
        f_old = e.get('func')
        window, wofs = e.get('window'), e.get('wofs')
        bases = self.new_anchor(a)
        tried_changed = False
        if a and a['kind'] in ('exact', 'func', 'func-end') and f_old:
            for nb in bases:
                # An exact entry names the address itself; func/func-end name the function start.
                start = nb - (v - f_old[0]) if a['kind'] == 'exact' else nb
                nf = self.new.function_at(start)
                if nf is None or nf[0] != start:
                    continue
                if sigs.masked_hash(self.new, self.new_insns(nf)) == e.get('fhash'):
                    new = nf[0] + (v - f_old[0])
                    return new, ('same' if new == v else 'same-code')
                tried_changed = True
                if window:
                    hits = sigs.find_pattern(self.new, window, nf[0], nf[1], limit=2)
                    if len(hits) == 1:
                        return hits[0] + wofs, 'moved-in-func'
                if v == f_old[0]:
                    # A function start whose body changed: the library still says where it is. A
                    # hook lands on the right function; whether it still means the same is for a
                    # human (listed under "changed functions").
                    return nf[0], 'changed-func'
        if a and a['kind'] == 'exact' and e.get('leaf'):
            for nb in bases:
                ins = sigs.decode_leaf(self.new, nb)
                if ins and sigs.masked_hash(self.new, ins) == e.get('fhash'):
                    return nb, ('same' if nb == v else 'same-code')
            if len(bases) == 1:
                return bases[0], 'changed-func'
        if a and a['kind'] == 'fragment' and window and bases:
            hits = []
            for nb in bases:
                for fb, fe in self.new_fragments(nb):
                    hits += sigs.find_pattern(self.new, window, fb, fe, limit=2)
            if len(hits) == 1:
                return hits[0] + wofs, 'fragment'
        if window:
            hits = sigs.find_pattern(self.new, window, limit=16)
            if len(hits) == 1:
                return hits[0] + wofs, 'sig-global'
            if len(hits) > 1:
                # Identical code in several places (template instances, folded twins): take the
                # copy that sits where the old neighbourhood went, if exactly one does.
                pred = self.predict(v)
                if pred is not None:
                    near = [h for h in hits if abs(h + wofs - pred) < 0x1000]
                    if len(near) == 1:
                        return near[0] + wofs, 'sig-near'
                return None, 'ambiguous'
        return None, 'changed' if tried_changed else 'lost'

    def map_data(self, e):
        v = e['rva']
        a = e.get('anchor')
        if not a:
            return None, 'lost'
        bases = self.new_anchor(a)
        if len(bases) != 1:
            return None, 'ambiguous' if bases else 'lost'
        new = bases[0] + a['delta']
        if e.get('slots'):
            # A vtable is the same table when its slots point at the same functions: compared as
            # sets of names per slot (ICF folding regroups names between builds), and by majority
            # (a slot's function may itself have been folded with a different partner).
            raw = self.new.read(new, 8 * len(e['slots']))
            match = comparable = 0
            for k, old_slot in enumerate(e['slots']):
                old_set = set(old_slot if isinstance(old_slot, list) else [old_slot] if old_slot else [])
                ptr = int.from_bytes(raw[8 * k:8 * k + 8], 'little') - self.new.image_base
                new_set = set(self.nlib.exact(ptr))
                if old_set and new_set:
                    comparable += 1
                    match += bool(old_set & new_set)
            if comparable and match < 0.75 * comparable:
                return new, 'data-slots-differ'
        return new, 'data-exact' if a['delta'] == 0 else 'data-delta'

    def run(self):
        results = {}
        text_end_new = next(s.va for s in self.new.sections if s.va > self.new.text.va)
        for e in self.db['entries']:
            d = e.get('decision')
            if d in ('not-rva', 'foreign', 'undecided'):
                continue
            if d == 'text-end':
                results[e['rva']] = dict(new=text_end_new, status='text-end')
                continue
            if e['section'] == '.text':
                new, status = self.map_code(e)
            else:
                new, status = self.map_data(e)
            results[e['rva']] = dict(new=new, status=status)
        self.cross_check_data(results)
        return results

    def cross_check_data(self, results):
        """Code that reads a data address says where it went: in a function whose masked code is
        unchanged, the RIP target of the same instruction is the new address of the same datum."""
        data_vals = {e['rva']: e for e in self.db['entries'] if e['section'] != '.text' and e['rva'] in results}
        if not data_vals:
            return
        votes = collections.defaultdict(collections.Counter)   # exact references to the datum
        near = collections.defaultdict(collections.Counter)    # references a little below it
        for e in self.db['entries']:
            r = results.get(e['rva'])
            if not r or r['status'] not in ('same', 'same-code') or not e.get('func'):
                continue
            f_old = e['func']
            delta = r['new'] - e['rva']
            nf = self.new.function_at(f_old[0] + delta)
            if not nf:
                continue
            oi = self.old_insns(tuple(f_old))
            ni = self.new_insns(nf)
            if len(oi) != len(ni):
                continue
            for a, b in zip(oi, ni):
                if a.rip_target is None or b.rip_target is None:
                    continue
                if a.rip_target in data_vals:
                    votes[a.rip_target][b.rip_target] += 1
                for dv in data_vals:
                    # A field of the same object (a vtable entry, a member of a global): only a
                    # short distance, since neighbouring objects move independently.
                    if 0 < dv - a.rip_target <= 0x40:
                        near[dv][b.rip_target + (dv - a.rip_target)] += 1
        for dv in set(votes) | set(near):
            exact = dv in votes
            c = votes[dv] if exact else near[dv]
            best, n = c.most_common(1)[0]
            r = results[dv]
            r['xref'] = dict(new=best, votes=n, total=sum(c.values()), exact=exact)
            unanimous = len(c) == 1
            if r['new'] is None and unanimous:
                r['new'], r['status'] = best, 'data-xref'
            elif r['new'] is not None and r['new'] != best and exact and unanimous:
                # Code that reads this very address, unchanged, says otherwise: a human decides.
                r['status'] = 'data-xref-conflict'
            elif r['new'] is not None and r['new'] == best and r['status'] == 'data-slots-differ' and exact:
                r['status'] = 'data-exact'


def remap_guards(db, results, old_img, new_img, remapper):
    out = []
    for g in db['guards']:
        # A named array guards the bytes some distance from the address the code names.
        r = results.get(g.get('base_value', g['rva']))
        item = dict(file=g['file'], line=g['line'], type=g['type'], rva=g['rva'])
        if not r or r['new'] is None or r['status'] not in RELIABLE:
            item['status'] = 'unmapped'
            out.append(item)
            continue
        new = r['new'] + g.get('delta', 0)
        length = g['length'] if g['type'] == 'fnv' else len(bytes.fromhex(g['bytes']))
        nf = new_img.function_at(new)
        mh = sigs.range_masked_hash(new_img, new, length, remapper.new_insns(nf) if nf else None)
        raw = new_img.read(new, length)
        item['new'] = new
        if mh != g.get('mhash') or raw is None:
            item['status'] = 'code-changed'
        else:
            item['status'] = 'ok'
            if g['type'] == 'fnv':
                item['fnv'] = pe.fnv1a64(raw)
            else:
                item['bytes'] = raw.hex()
        out.append(item)
    return out


# ---- source rewrite -------------------------------------------------------------------------------
HEX_LETTERS = re.compile(r'(?<![\w.])0[xX]([0-9A-Fa-f]*[A-Fa-f][0-9A-Fa-f]*)\b')


def case_hint(line, text):
    """Upper or lower case for a literal made of digits only: what the line's other literals use,
    else the file's majority, else upper."""
    for scope in (line, text):
        up = low = 0
        for m in HEX_LETTERS.finditer(scope):
            d = m.group(1)
            up += d.isupper() or not any(c.islower() for c in d)
            low += any(c.islower() for c in d) and not any(c.isupper() for c in d)
        if up != low:
            return up > low
    return True


def fmt_like(literal, value, min_width=0, upper_hint=True):
    """New hex in the style of the old literal: case, zero padding, suffix."""
    m = re.match(r'(0[xX])([0-9A-Fa-f]+)([uUlL]*)$', literal)
    if not m:
        return None
    prefix, digits, suffix = m.groups()
    if any(c in 'ABCDEF' for c in digits):
        upper = True
    elif any(c in 'abcdef' for c in digits):
        upper = False
    else:
        upper = upper_hint
    width = max(len(digits) if digits.startswith('0') else 0, min_width)
    h = f'{value:0{width}X}' if upper else f'{value:0{width}x}'
    return prefix + h + suffix


def fmt_ida(literal, value):
    upper = any(c in 'ABCDEF' for c in literal) or not any(c in 'abcdef' for c in literal)
    v = value + pe.IMAGE_BASE
    return f'{v:X}' if upper else f'{v:x}'


def apply_edits(repo, db, results, guards, dry=False):
    edits = collections.defaultdict(list)   # file -> [(line, col, end, text)]
    arrays = collections.defaultdict(list)  # file -> [(line, name, old hex, new hex)]
    applied = collections.Counter()
    for o in db['occurrences']:
        r = results.get(o['value'])
        if not r or r['new'] is None or r['status'] not in RELIABLE or r['new'] == o['value']:
            continue
        if o['kind'] == 'foreign-module':
            continue
        if o['kind'] == 'ida-name':
            new_text = fmt_ida(o['literal'], r['new'])
        else:
            # Formatted at apply time, when the line and the file are there to take the case from.
            new_text = ('lit', r['new'])
        if new_text:
            edits[o['file']].append((o['line'], o['col'], o['end'], o['literal'], new_text))
            applied[o['kind']] += 1
    for g, src in zip(guards, db['guards']):
        if g['status'] != 'ok':
            continue
        if g['type'] == 'fnv' and g['fnv'] != src['fnv']:
            lo, hi = src['fnv_span']
            edits[src['file']].append((src['line'], lo, hi, None, ('fnv', g['fnv'])))
            applied['guard-fnv'] += 1
        elif g['type'] == 'hex' and g['bytes'].lower() != src['bytes'].lower():
            lo, hi = src['hex_span']
            upper = any(c in 'ABCDEF' for c in src['bytes'])
            edits[src['file']].append((src['line'], lo, hi, None, g['bytes'].upper() if upper else g['bytes'].lower()))
            applied['guard-hex'] += 1
        elif g['type'] == 'bytes' and g['bytes'].lower() != src['bytes'].lower():
            edits[src['file']].append((src['line'], -1, -1, src['bytes'], g['bytes']))
            applied['guard-bytes'] += 1
        elif g['type'] == 'array' and g['bytes'].lower() != src['bytes'].lower():
            arrays[src['file']].append((src['line'], src['name'], src['bytes'], g['bytes']))
            applied['guard-array'] += 1
    conflicts = []
    if dry:
        return applied, conflicts
    for rel in set(edits) | set(arrays):
        items = edits.get(rel, [])
        path = os.path.join(repo, rel)
        with open(path, encoding='utf-8', newline='') as f:
            text = f.read()
        # Column edits first, on the text the inventory measured; the array initializers after,
        # because rewriting a token can change the width of the line it is on.
        lines = text.split('\n')
        by_line = collections.defaultdict(list)
        for it in items:
            by_line[it[0]].append(it)
        for ln, its in by_line.items():
            line = lines[ln - 1]
            for (_, col, end, old, new) in sorted(its, key=lambda t: t[1], reverse=True):
                if col == -1:
                    line = rewrite_byte_array(line, old, new)
                    continue
                if old is not None and line[col:end] != old:
                    conflicts.append(f'{rel}:{ln} expected {old!r} at {col}, found {line[col:end]!r}')
                    continue
                if isinstance(new, tuple):
                    # An FNV is always all 16 digits (the suffix after them is outside the span);
                    # an address keeps the old literal's style.
                    cur = line[col:end]
                    hint = case_hint(lines[ln - 1], text)
                    rep = fmt_like(cur, new[1], 16 if new[0] == 'fnv' else 0, hint)
                    if not rep:
                        conflicts.append(f'{rel}:{ln} literal {cur!r} not recognised')
                        continue
                    line = line[:col] + rep + line[end:]
                else:
                    line = line[:col] + new + line[end:]
            lines[ln - 1] = line
        text = '\n'.join(lines)
        for ln, name, old_hex, new_hex in arrays.get(rel, []):
            text, ok = rewrite_named_array(text, name, ln, old_hex, new_hex)
            if not ok:
                conflicts.append(f'{rel}:{ln} byte array {name} not found with its old bytes')
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(text)
    return applied, conflicts


def rewrite_named_array(text, name, line, old_hex, new_hex):
    """The initializer of `name` declared at (about) line, token by token in its own style."""
    old_b, new_b = bytes.fromhex(old_hex), bytes.fromhex(new_hex)
    start = 0
    for _ in range(max(0, line - 3)):
        nxt = text.find('\n', start)
        if nxt < 0:
            break
        start = nxt + 1
    rx = re.compile(r'\b' + re.escape(name) + r'\s*\[[^\]]*\]\s*=?\s*\{([^}]*)\}')
    m = rx.search(text, start)
    if not m:
        return text, False
    inner = m.group(1)
    toks = list(re.finditer(r'0[xX][0-9A-Fa-f]{1,2}\b|(?<![\w.])\d{1,3}\b', inner))
    if bytes(int(t.group(0), 0) for t in toks) != old_b or len(new_b) != len(old_b):
        return text, False
    for k in reversed(range(len(toks))):
        t = toks[k]
        cur = t.group(0)
        if cur.lower().startswith('0x'):
            digits = cur[2:]
            upper = any(c in 'ABCDEF' for c in digits) or not any(c in 'abcdef' for c in digits)
            rep = cur[:2] + (f'{new_b[k]:02X}' if upper else f'{new_b[k]:02x}')
        else:
            rep = str(new_b[k]) if new_b[k] < 10 else f'0x{new_b[k]:02x}'
        inner = inner[:t.start()] + rep + inner[t.end():]
    return text[:m.start(1)] + inner + text[m.end(1):], True


def rewrite_byte_array(line, old_hex, new_hex):
    old_b = bytes.fromhex(old_hex)
    new_b = bytes.fromhex(new_hex)
    m = None
    for cand in re.finditer(r'\{((?:\s*0[xX][0-9A-Fa-f]{1,2}\s*,?)+)\}', line):
        bs = bytes(int(x, 16) for x in re.findall(r'0[xX]([0-9A-Fa-f]{1,2})', cand.group(1)))
        if bs == old_b:
            m = cand
            break
    if not m:
        return line
    toks = list(re.finditer(r'0[xX]([0-9A-Fa-f]{1,2})', m.group(1)))
    inner = m.group(1)
    for k in reversed(range(len(toks))):
        t = toks[k]
        upper = any(c in 'ABCDEF' for c in t.group(1)) or t.group(1).isdigit()
        rep = f"0x{new_b[k]:02X}" if upper else f"0x{new_b[k]:02x}"
        inner = inner[:t.start()] + rep + inner[t.end():]
    return line[:m.start(1)] + inner + line[m.end(1):]


def carry_overrides(src, dst, results):
    with open(src, encoding='utf-8') as f:
        j = json.load(f)
    moved = 0
    for key in ('rva',):
        out = {}
        for k, why in j.get(key, {}).items():
            r = results.get(int(k, 16))
            if r and r['new'] is not None and r['status'] in RELIABLE:
                out[f"{r['new']:#x}"] = why
                moved += r['new'] != int(k, 16)
            else:
                out[k] = why
        j[key] = out
    ends = []
    for k in j.get('text_end', []):
        r = results.get(int(k, 16))
        ends.append(f"{r['new']:#x}" if r and r['new'] is not None else k)
    j['text_end'] = ends
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, 'w', encoding='utf-8') as f:
        json.dump(j, f, indent=2)
    print(f'literal_overrides: {moved} forced address(es) carried -> {dst}')


def write_game_build(repo, new_img, new_exe_sha, version, unmapped):
    path = os.path.join(repo, 'include', 'Core', 'GameBuild.hpp')
    with open(path, encoding='utf-8', newline='') as f:
        text = f.read()
    nl = '\r\n' if '\r\n' in text else '\n'
    text = re.sub(r'(kTimestamp\s*=\s*)0x[0-9A-Fa-f]+', lambda m: m.group(1) + f'0x{new_img.timestamp:08X}', text)
    text = re.sub(r'(kSizeOfImage\s*=\s*)0x[0-9A-Fa-f]+', lambda m: m.group(1) + f'0x{new_img.size_of_image:08X}', text)
    text = re.sub(r'(kVersion\s*=\s*)"[^"]*"', lambda m: m.group(1) + f'"{version}"', text)
    text = re.sub(r'(kExeSha256\s*=\s*)"[^"]*"', lambda m: m.group(1) + f'"{new_exe_sha}"', text)
    # The placeholder 0x0 stays first: an empty array is not C++.
    body = '    0x0,' + nl + ''.join(f'    0x{v:X},' + nl for v in sorted(unmapped))
    text = re.sub(r'(// BEGIN UNMAPPED\r?\n).*?([ \t]*// END UNMAPPED)', lambda m: m.group(1) + body + m.group(2),
                  text, flags=re.S)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    return path


def report(db, results, guards, applied, conflicts, out_dir, new_img, new_sha):
    st = collections.Counter(r['status'] for r in results.values())
    occ_by = collections.defaultdict(list)
    for o in db['occurrences']:
        if o['kind'] not in ('comment', 'ida-name'):
            occ_by[o['value']].append(o)
    ents = {e['rva']: e for e in db['entries']}
    L = []
    L.append(f"# Remap {db['exe']['timestamp']:08x} -> {new_img.timestamp:08x}\n")
    L.append(f"- old exe sha256 `{db['exe']['sha256']}`\n- new exe sha256 `{new_sha}`\n"
             f"- generated {datetime.datetime.now().isoformat(timespec='seconds')}\n")
    L.append('## Addresses\n')
    for k, v in st.most_common():
        L.append(f'- {k}: {v}' + ('' if k in RELIABLE else '  **(not rewritten)**'))
    gs = collections.Counter(g['status'] for g in guards)
    L.append('\n## Guards\n')
    for k, v in gs.most_common():
        L.append(f'- {k}: {v}')
    L.append('\n## Rewrites\n')
    for k, v in applied.most_common():
        L.append(f'- {k}: {v}')
    if conflicts:
        L.append('\n### Rewrite conflicts\n')
        L += [f'- {c}' for c in conflicts]
    rev = [(v, r) for v, r in results.items() if r['status'] in REVIEW]
    if rev:
        L.append('\n## Rewritten, review\n')
        L.append('changed-func: the library names the function, its masked code differs (hook lands on the right')
        L.append('function; check what the detour reads). data-delta: data past a library entry at the old distance.')
        L.append('sig-global: no anchor, the window matched once in all of .text.\n')
        L.append('| old RVA | new RVA | status | anchor | where |')
        L.append('|---|---|---|---|---|')
        for v, r in sorted(rev, key=lambda t: (t[1]['status'], t[0])):
            e = ents[v]
            a = e.get('anchor') or {}
            where = '; '.join(f"{o['file']}:{o['line']}" for o in occ_by[v][:3])
            L.append(f"| `{v:#x}` | `{r['new']:#x}` | {r['status']} | {a.get('kind', '-')} {a.get('symbol') or ''} | {where} |")
    bad = [(v, r) for v, r in results.items() if r['status'] not in RELIABLE]
    if bad:
        L.append('\n## Needs a human\n')
        L.append('| old RVA | status | anchor | where |')
        L.append('|---|---|---|---|')
        for v, r in sorted(bad, key=lambda t: (t[1]['status'], t[0])):
            e = ents[v]
            a = e.get('anchor') or {}
            where = '; '.join(f"{o['file']}:{o['line']}" for o in occ_by[v][:3])
            L.append(f"| `{v:#x}` | {r['status']} | {a.get('kind', '-')} {a.get('symbol') or ''} | {where} |")
    cg = [g for g in guards if g['status'] == 'code-changed']
    if cg:
        L.append('\n## Guards over changed code (feature stays off until re-verified)\n')
        for g in cg:
            L.append(f"- {g['file']}:{g['line']} {g['type']} `{g['rva']:#x}` -> `{g.get('new', 0):#x}`")
    path = os.path.join(out_dir, 'report.md')
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')
    return path, st


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--db', default=os.path.join(HERE, 'data', 'rva_db-68af45ea.json'))
    ap.add_argument('--old', default=None, help='old game dir (default: the db exe path)')
    ap.add_argument('--new', required=True, help='new game dir holding bin/x64')
    ap.add_argument('--version', default='new', help='game version string for GameBuild.hpp')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--out', default=None)
    ap.add_argument('--repo', default=REPO, help='tree to rewrite (a scratch copy for a dry run)')
    ap.add_argument('--overrides-out', default=None,
                    help='where --apply writes the carried literal_overrides.json (default: the real one for '
                         'the real tree, <out>/literal_overrides.json for a scratch tree)')
    ap.add_argument('--simulate-shift', type=lambda x: int(x, 0), default=None,
                    help='TEST: pretend every mapped address moved by this much (use with --repo <copy>)')
    args = ap.parse_args()
    with open(args.db, encoding='utf-8') as f:
        db = json.load(f)
    old_exe = db['exe']['path'] if not args.old else pe.find_game_files(args.old)[0]
    old_img = pe.Image(old_exe)
    old_lib = pe.AddressLibrary(os.path.join(os.path.dirname(old_exe), 'cyberpunk2077_addresses.json'), old_img)
    new_exe, new_libp = pe.find_game_files(args.new)
    new_img = pe.Image(new_exe)
    new_lib = pe.AddressLibrary(new_libp, new_img)
    import inventory
    new_sha = inventory._sha256(new_exe)
    rm = Remapper(db, old_img, old_lib, new_img, new_lib)
    results = rm.run()
    if args.simulate_shift is not None:
        if os.path.abspath(args.repo) == REPO and args.apply:
            sys.exit('--simulate-shift --apply rewrites the tree: give it --repo <scratch copy>')
        for r in results.values():
            if r['new'] is not None:
                r['new'] += args.simulate_shift
    guards = remap_guards(db, results, old_img, new_img, rm)
    if args.simulate_shift is not None:
        # The shifted addresses hold unrelated code, so every guard reads "changed"; pretend
        # instead that each one re-derived to a recognisable new value (FNV + 1, every byte + 1),
        # which exercises the rewrite of all four guard forms.
        for g, src in zip(guards, db['guards']):
            g['status'], g['new'] = 'ok', src['rva'] + args.simulate_shift
            if src['type'] == 'fnv':
                g['fnv'] = (src['fnv'] + 1) & pe.U64
            else:
                g['bytes'] = bytes((b + 1) & 0xFF for b in bytes.fromhex(src['bytes'])).hex()
    out_dir = args.out or os.path.join(REPO, 'build', 'patchday', f'remap-{new_img.timestamp:08x}')
    os.makedirs(out_dir, exist_ok=True)
    applied, conflicts = apply_edits(args.repo, db, results, guards, dry=not args.apply)
    if args.apply:
        code_values = {e['rva'] for e in db['entries'] if e.get('decision') == 'rva'}
        unmapped = {v for v, r in results.items() if r['status'] not in RELIABLE and v in code_values}
        write_game_build(args.repo, new_img, new_sha, args.version, unmapped)
        # The hand decisions are keyed by value: the forced addresses and the .text end move with the
        # build, or the next inventory would not recognise them. Into the real file only when the real
        # tree was rewritten; a scratch run gets its own copy.
        src_ov = os.path.join(HERE, 'data', 'literal_overrides.json')
        dst_ov = args.overrides_out or (src_ov if os.path.abspath(args.repo) == REPO
                                        else os.path.join(out_dir, 'literal_overrides.json'))
        carry_overrides(src_ov, dst_ov, results)
    with open(os.path.join(out_dir, 'map.json'), 'w', encoding='utf-8') as f:
        json.dump({f'{k:#x}': dict(v, new=(f"{v['new']:#x}" if v['new'] is not None else None))
                   for k, v in sorted(results.items())}, f, indent=1)
    with open(os.path.join(out_dir, 'guards.json'), 'w', encoding='utf-8') as f:
        json.dump(guards, f, indent=1)
    path, st = report(db, results, guards, applied, conflicts, out_dir, new_img, new_sha)
    print('statuses:', dict(st.most_common()))
    print('guards:', dict(collections.Counter(g['status'] for g in guards)))
    print('rewrites:', dict(applied), '(applied)' if args.apply else '(dry run)')
    if conflicts:
        print(len(conflicts), 'rewrite conflicts -- see report')
    print('report:', path)
    code_values = {e['rva'] for e in db['entries'] if e.get('decision') == 'rva'}
    human = sorted(v for v, r in results.items() if r['status'] not in RELIABLE and v in code_values)
    if human or conflicts:
        print(f'{len(human)} address(es) need a human (report: "Needs a human")')
        sys.exit(2)


if __name__ == '__main__':
    main()
