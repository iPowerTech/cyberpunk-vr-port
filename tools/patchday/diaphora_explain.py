"""What changed in each changed function, from the Hex-Rays pseudocode in both Diaphora exports.

    py -3.13 -B tools/patchday/diaphora_explain.py [--diff build/patchday/diaphora-2.5]

For every row of functions.tsv that is not identical / addresses-only:

  matched  both pseudocodes are normalised -- calls to functions the match pairs become the same token (F<new rva>),
           data addresses, local variable numbers and labels are masked, declarations and comments dropped -- and
           diffed. The change class is the first that fits:
             codegen      the normalised code is the same (register allocation, scheduling, inlining of the same
                          code: nothing a caller can see)
             constants    the same once numbers are masked: only immediates / struct offsets moved, listed old->new
             calls        the same once numbers and callees are masked: it calls other functions
             logic        anything else (statements added, removed or reordered)
  added    what the new function does in short: size, its strings, its named callees, first statements
  removed  the same for the 2.31 function

Writes <diff>/changes.tsv (one row each), <diff>/changes_diffs.jsonl (the normalised unified diff of every matched
function whose class is not codegen, at most 160 lines each) and <diff>/CHANGES.md.
"""
import argparse
import collections
import difflib
import json
import os
import re
import sqlite3

HERE = os.path.dirname(os.path.abspath(__file__))
PD = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'build', 'patchday')
BASE = 0x140000000
SUB = re.compile(r'\b(?:sub|j_sub)_(14[0-9A-F]{7})\b')
DATA = re.compile(r'\b(?:qword|dword|byte|word|unk|off|stru|xmmword|ymmword|flt|dbl|asc|algn|loc|locret|jpt|def|funcs)_'
                  r'[0-9A-F]{6,}\b|\b(?:a[A-Z][A-Za-z0-9_]*?)(?:_\d+)?\b(?=[^(]|$)')
LOCAL = re.compile(r'\b(?:v|a)\d+\b|\bLABEL_\d+\b|\bi\b|\bj\b|\bk\b')
DECL = re.compile(r'^\s*(?:unsigned |signed |const |struct |volatile )*[\w:<>*]+(?: \*+| )[\w*]+(?:\[\d+\])?;\s*(?://.*)?$')
NUM = re.compile(r'(?<![\w.])(?:0x[0-9A-Fa-f]+|\d+)(?:u?i?(?:64|LL|uLL|u|LL)?)?\b')
OFFSET = re.compile(r'\+ (0x[0-9A-Fa-f]+|\d+)(?:LL)?\)')
STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
NAMED_CALL = re.compile(r'\b([A-Za-z_~][\w:~<>`\']*)\(')
KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'LODWORD', 'HIDWORD', 'LOBYTE', 'HIBYTE', 'LOWORD',
            'HIWORD', 'BYTE1', 'BYTE2', 'BYTE3', 'BYTE4', 'WORD1', 'WORD2', 'COERCE_FLOAT', 'COERCE_DOUBLE', 'F'}


def load_tsv(path):
    with open(path, encoding='utf-8', newline='\n') as f:
        lines = f.read().split('\n')
    cols = lines[0].split('\t')
    return [dict(zip(cols, l.split('\t'))) for l in lines[1:] if l]


def pseudocode(db, rvas):
    want = {r + BASE for r in rvas}
    out = {}
    con = sqlite3.connect('file:%s?mode=ro' % db, uri=True)
    for addr, pc in con.execute('select address, pseudocode from functions'):
        a = int(addr)
        if a in want:
            out[a - BASE] = pc or ''
    con.close()
    return out


def normalise(text, callee):
    """Lines of comparable code; callee(rva) gives the token a call target becomes."""
    out = []
    for line in text.split('\n'):
        line = line.split('//')[0].rstrip()
        if not line.strip() or DECL.match(line):
            continue
        line = SUB.sub(lambda m: callee(int(m.group(1), 16) - BASE), line)
        line = DATA.sub('D', line)
        line = LOCAL.sub('v', line)
        out.append(line.strip())
    return out


CAST = re.compile(r'\((?:const |volatile |unsigned |signed |struct )*(?:__int8|__int16|__int32|__int64|__int128|char|_BYTE|'
                  r'_WORD|_DWORD|_QWORD|_OWORD|int|float|double|bool|void|__m128i?|__m128d|size_t|[A-Za-z_]\w*)'
                  r'(?: \*+|\*+| \(__fastcall \*+\)\([^()]*\))?\)\s*')
WRAP = re.compile(r'\b(?:LOBYTE|HIBYTE|LOWORD|HIWORD|LODWORD|HIDWORD|BYTE\d|WORD\d|COERCE_\w+)\(([^()]*)\)')


def loose(lines):
    """Lines with what Hex-Rays prints differently for the same machine logic removed: casts, low/high-part wrappers,
    self-assignments, bare blocks and gotos/labels."""
    out = []
    for l in lines:
        l = CAST.sub('', l)
        l = WRAP.sub(r'\1', l)
        l = re.sub(r'\s+', ' ', l).strip()
        if l in ('{', '}', 'v = v;', 'v:', 'else') or l.startswith('goto ') or re.fullmatch(r'v = v[;,]?', l):
            continue
        out.append(l)
    return out


LOCK_OLD = re.compile(r'\bF(?:22ca300|7cba48)\b|_InterlockedExchangeAdd8')
LOCK_NEW = re.compile(r'scoped_lock<std::shared_mutex>|\bF(?:19417f0|1ffadc0)\b')
MODSVC = re.compile(r'^\+\(\*\(.*\*\*\)\(\*\(_QWORD \*\)D \+ (\d+)LL\)\)\(D|^\+return .*\(\*\(_QWORD \*\)D \+ (\d+)LL\)\)\(')


def patterns(diff):
    """Systematic 2.5 changes recognised in a diff (found while reading the port's / frameworks' functions)."""
    text = '\n'.join(diff)
    found = []
    if LOCK_OLD.search('\n'.join(d for d in diff if d.startswith('-'))) and \
            LOCK_NEW.search('\n'.join(d for d in diff if d.startswith('+'))):
        found.append('lock: RED spinlock -> std::shared_mutex')
    slots = [m.group(1) or m.group(2) for d in diff for m in [MODSVC.search(d)] if m]
    if '+if ( D )' in text and slots:
        found.append('GameModService hook (vfunc +%s)' % ','.join(dict.fromkeys(slots)))
    if re.search(r'^\+.*\bF(?:2005190|2005320|2007600|2007670|2007f90)\(', text, re.M):
        found.append('script call tracing added')
    return found


def features(lines):
    text = '\n'.join(lines)
    calls = set(re.findall(r'\bF[0-9a-f?]+\b', text)) | {n for n in NAMED_CALL.findall(text) if n not in KEYWORDS}
    return {'calls': calls, 'strings': set(STRING.findall(text)), 'offsets': OFFSET.findall(text)}


def number_pairs(old, new):
    """old->new number changes on lines that are equal once numbers are masked."""
    pairs = collections.Counter()
    sm = difflib.SequenceMatcher(a=[NUM.sub('#', l) for l in old], b=[NUM.sub('#', l) for l in new], autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag != 'equal':
            continue
        for a, b in zip(old[i1:i2], new[j1:j2]):
            na, nb = NUM.findall(a), NUM.findall(b)
            for x, y in zip(na, nb):
                if x != y:
                    pairs['%s->%s' % (x, y)] += 1
    return pairs


def short(text, limit=4):
    """First statements of a function, normalised, for an added/removed row."""
    body = [l for l in text if l not in ('{', '}')]
    return ' ; '.join(body[:limit])[:300]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--diff', default=os.path.join(PD, 'diaphora-2.5'))
    ap.add_argument('--old', default=os.path.join(PD, 'ida', '2.31', 'cp2077_2.31.sqlite'))
    ap.add_argument('--new', default=os.path.join(PD, 'ida', '2.5', 'cp2077_2.5.sqlite'))
    a = ap.parse_args()
    rows = load_tsv(os.path.join(a.diff, 'functions.tsv'))
    o2n = {int(r['old'], 16): int(r['new'], 16) for r in rows if r['status'] == 'matched'}
    changed = [r for r in rows if r['kind'] not in ('identical', 'addresses-only')]
    want_old = {int(r['old'], 16) for r in changed if r['old']}
    want_new = {int(r['new'], 16) for r in changed if r['new']}
    print('changed %d; loading pseudocode old %d, new %d' % (len(changed), len(want_old), len(want_new)), flush=True)
    pc_old, pc_new = pseudocode(a.old, want_old), pseudocode(a.new, want_new)
    # Diaphora exported no pseudocode where Hex-Rays failed on a bad stack frame (MERR_BADFRAME); those functions
    # were rebuilt in IDA (del_func + add_func) and decompiled again into nopc_pseudocode_<build>.json
    for pc, build in ((pc_old, '2.31'), (pc_new, '2.5')):
        extra = os.path.join(a.diff, 'nopc_pseudocode_%s.json' % build)
        if os.path.exists(extra):
            for k, v in json.load(open(extra, encoding='utf-8')).items():
                if v.get('mode') == 'c' and not pc.get(int(k, 16)):
                    pc[int(k, 16)] = v['text']
    callee_old = lambda r: 'F%x' % o2n[r] if r in o2n else 'F?'
    callee_new = lambda r: 'F%x' % r
    out_rows = []
    diffs = open(os.path.join(a.diff, 'changes_diffs.jsonl'), 'w', encoding='utf-8')
    classes = collections.Counter()
    all_pairs = collections.Counter()
    for k, r in enumerate(changed):
        rec = dict(status=r['status'], old=r['old'], new=r['new'], kind=r['kind'], subsystem=r['subsystem'], used=r['used'])
        if r['status'] == 'matched':
            ro, rn = int(r['old'], 16), int(r['new'], 16)
            if ro not in pc_old or rn not in pc_new or not pc_old[ro] or not pc_new[rn]:
                rec.update(change='no pseudocode', summary=r['detail'])
            else:
                lo, ln = normalise(pc_old[ro], callee_old), normalise(pc_new[rn], callee_new)
                fo, fn = features(lo), features(ln)
                masked = lambda ls: [NUM.sub('#', l) for l in ls]
                nocall = lambda ls: [re.sub(r'\bF[0-9a-f?]+\b', 'F', l) for l in masked(ls)]
                if lo == ln or loose(lo) == loose(ln):
                    cls = 'codegen'
                elif masked(lo) == masked(ln) or masked(loose(lo)) == masked(loose(ln)):
                    cls = 'constants'
                elif nocall(lo) == nocall(ln) or nocall(loose(lo)) == nocall(loose(ln)):
                    cls = 'calls'
                else:
                    cls = 'logic'
                pairs = number_pairs(lo, ln) if cls != 'codegen' else collections.Counter()
                all_pairs.update(pairs)
                diff = [] if cls == 'codegen' else list(difflib.unified_diff(lo, ln, 'old', 'new', n=1, lineterm=''))[2:]
                plus = sum(1 for d in diff if d.startswith('+'))
                minus = sum(1 for d in diff if d.startswith('-'))
                bits = []
                if pairs:
                    bits.append('numbers ' + ', '.join(p for p, _ in pairs.most_common(6)))
                ca, cr = sorted(fn['calls'] - fo['calls']), sorted(fo['calls'] - fn['calls'])
                if ca or cr:
                    bits.append('calls +%s -%s' % (','.join(ca[:4]), ','.join(cr[:4])))
                sa, sr = sorted(fn['strings'] - fo['strings']), sorted(fo['strings'] - fn['strings'])
                if sa or sr:
                    bits.append('strings +%s -%s' % ('|'.join(s[:40] for s in sa[:3]), '|'.join(s[:40] for s in sr[:3])))
                if cls == 'logic':
                    ll_o, ll_n = loose(lo), loose(ln)
                    sm = difflib.SequenceMatcher(a=ll_o, b=ll_n, autojunk=False)
                    same = sum(b.size for b in sm.get_matching_blocks())
                    bits.append('lines %d->%d (+%d -%d), %d%% of statements kept' % (
                        len(lo), len(ln), plus, minus, 100 * same // max(len(ll_o), len(ll_n), 1)))
                    pat = patterns(diff)
                    if pat:
                        bits.insert(0, '; '.join(pat))
                rec.update(change=cls, summary='; '.join(bits) or 'same normalised code')
                if diff:
                    diffs.write(json.dumps({'old': r['old'], 'new': r['new'], 'class': cls, 'diff': diff[:160]}) + '\n')
        elif r['status'] == 'added':
            ln = normalise(pc_new.get(int(r['new'], 16), ''), callee_new)
            f = features(ln)
            named = sorted(c for c in f['calls'] if not c.startswith('F'))
            rec.update(change='added', summary='%s | strings %s | calls %s | %s' % (
                r['detail'], '|'.join(s[:40] for s in sorted(f['strings'])[:3]), ','.join(named[:4]), short(ln)))
        else:
            lo = normalise(pc_old.get(int(r['old'], 16), ''), callee_old)
            f = features(lo)
            named = sorted(c for c in f['calls'] if not c.startswith('F'))
            rec.update(change='removed', summary='%s | strings %s | calls %s | %s' % (
                r['detail'], '|'.join(s[:40] for s in sorted(f['strings'])[:3]), ','.join(named[:4]), short(lo)))
        classes[rec['change']] += 1
        out_rows.append(rec)
        if k % 10000 == 0:
            print('%d / %d' % (k, len(changed)), flush=True)
    diffs.close()
    cols = ['status', 'old', 'new', 'kind', 'subsystem', 'change', 'summary', 'used']
    with open(os.path.join(a.diff, 'changes.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\t'.join(cols) + '\n')
        for rec in out_rows:
            f.write('\t'.join(re.sub(r'[\t\r\n\x0b\x0c\x1c-\x1e\x85  ]', ' ', str(rec.get(c, ''))) for c in cols) + '\n')
    by = collections.defaultdict(collections.Counter)
    for rec in out_rows:
        by[rec['subsystem']][rec['change']] += 1
    order = ['codegen', 'constants', 'calls', 'logic', 'no pseudocode', 'added', 'removed']
    md = ['# What changed in each of the %d changed functions' % len(out_rows), '',
          'changes.tsv: one row per function that is not identical / addresses-only, with its change class and what moved;',
          'changes_diffs.jsonl: the normalised diff of every matched one that is not codegen-only. Classes are defined in',
          'tools/patchday/diaphora_explain.py.', '', '| class | functions |', '|---|---|']
    md += ['| %s | %d |' % (c, classes[c]) for c in order if classes[c]]
    md += ['', '## by subsystem', '', '| subsystem | ' + ' | '.join(order) + ' |', '|---' * (len(order) + 1) + '|']
    for sub in sorted(by, key=lambda s: -sum(by[s].values())):
        md.append('| %s | ' % sub + ' | '.join(str(by[sub][c]) for c in order) + ' |')
    md += ['', '## most frequent number changes (struct offsets / immediates moved by the patch)', '',
           '| old->new | lines |', '|---|---|']
    md += ['| %s | %d |' % kv for kv in all_pairs.most_common(40)]
    used = [rec for rec in out_rows if rec['used']]
    md += ['', '## functions the port or a framework uses (%d changed)' % len(used), '',
           'Each one was read by hand: verdicts in review_used.tsv / review_used.md.', '',
           '| old | new | class | summary | used by |', '|---|---|---|---|---|']
    md += ['| %s | %s | %s | %s | %s |' % (rec['old'], rec['new'], rec['change'], rec['summary'][:120].replace('|', '/'),
                                         rec['used'][:80]) for rec in sorted(used, key=lambda x: order.index(x['change']))]
    open(os.path.join(a.diff, 'CHANGES.md'), 'w', encoding='utf-8').write('\n'.join(md) + '\n')
    print('classes:', dict(classes))


if __name__ == '__main__':
    main()
