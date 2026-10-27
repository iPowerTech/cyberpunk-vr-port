"""Every framework function a patch touched, decompiled on both builds and diffed.

    py -3.13 -B tools/patchday/framework_review.py [--hashes build/patchday/framework_hashes.json]
                                                  [--out build/patchday/framework-review]

Takes framework_hashes.py's JSON: every hash GONE (the symbol changed: the new function is proposed by
cg_match.py's call-graph neighbours), CHANGED (masked code differs), code-not-func-start or ambiguous. Both
builds' IDA databases decompile their side in one batch each (ida/scripts/decompile_list.py, run in parallel
on copies the MCP service does not hold), the pseudo-code is normalised (sub_/loc_/dword_ ... names, stack
variable numbers) and diffed. REPORT.md lists per function: identical after normalisation, or the diff, with the
new address and the hash the new address library gives it.
"""
import argparse
import difflib
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import cg_match  # noqa: E402

IDAT = r'C:\Program Files\IDA Professional 9.2\idat.exe'
IDA = os.path.join(REPO, 'build', 'patchday', 'ida')
OLD_DB = os.path.join(IDA, '2.31', 'cp2077_2.31.i64')
NEW_DB = os.path.join(IDA, '2.5b', 'cp2077_2.5b.i64')
SCRIPT = os.path.join(IDA, 'scripts', 'decompile_list.py')
WANT = ('GONE', 'CHANGED', 'code-not-func-start', 'ambiguous', 'was-ambiguous')
NORM = [
    (re.compile(r'\b(sub|loc|nullsub|j_sub|unk|off|qword|dword|word|byte|xmmword|stru|asc|flt|dbl|jpt|def)_[0-9A-F]{6,}\b'), r'\1_X'),
    (re.compile(r'\b(v|a)\d+\b'), r'\1N'),
    (re.compile(r'\b0x[0-9A-F]{6,}(?:LL|uLL|u)?\b'), '0xADDR'),
    (re.compile(r'// .*$'), ''),
    (re.compile(r'\[rsp\+[0-9A-Fh]+\]|\[rbp[-+][0-9A-Fh]+\]'), '[STK]'),
]


def norm(text):
    out = []
    for ln in text.splitlines()[1:]:
        for rx, rep in NORM:
            ln = rx.sub(rep, ln)
        ln = ln.rstrip()
        if ln.strip():
            out.append(ln)
    return out


def run_batch(db, items, out):
    os.makedirs(out, exist_ok=True)
    lst = os.path.join(out, '_list.txt')
    with open(lst, 'w', encoding='utf-8') as f:
        for rva, name in items:
            f.write(f'{rva:x} {name}\n')
    env = dict(os.environ, CVR_DECOMP_LIST=lst, CVR_DECOMP_OUT=out)
    return subprocess.Popen([IDAT, '-A', f'-L{os.path.join(out, "_ida.log")}', f'-S{SCRIPT}', db], env=env,
                            cwd=os.path.dirname(db))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hashes', default=os.path.join(REPO, 'build', 'patchday', 'framework_hashes.json'))
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'framework-review'))
    ap.add_argument('--no-ida', action='store_true', help='reuse the decompiled files already in --out')
    a = ap.parse_args()
    rows = json.load(open(a.hashes, encoding='utf-8'))
    by_hash = {}
    for r in rows:
        if r['status'] in WANT:
            e = by_hash.setdefault(r['hash'], dict(r, frameworks=[]))
            e['frameworks'].append(r['framework'])
    old_g = new_g = None
    items_old, items_new, plan = [], [], []
    for h, r in sorted(by_hash.items(), key=lambda kv: kv[1]['names'][0]):
        name = re.sub(r'\W', '_', r['names'][0])[:60]
        if not r['old']:
            continue
        ro = r['old'][0]
        cands = []
        if r['status'] == 'GONE':
            if old_g is None:
                old_g, new_g = cg_match.Graph(a.old), cg_match.Graph(a.new)
            m = cg_match.match(old_g, new_g, ro, top=3)
            cands = [c for _, c, _, known in m['candidates'] if not known][:2] or \
                    [c for _, c, _, _ in m['candidates']][:1]
        else:
            cands = r['new'][:2]
        items_old.append((ro, name))
        for c in cands:
            items_new.append((c, name))
        plan.append((h, r, name, ro, cands))
    od, nd = os.path.join(a.out, 'old'), os.path.join(a.out, 'new')
    if not a.no_ida:
        p1, p2 = run_batch(OLD_DB, items_old, od), run_batch(NEW_DB, items_new, nd)
        p1.wait()
        p2.wait()
    lines = ['# Framework functions touched by the patch: old vs new pseudo-code', '']
    stats = {}
    for h, r, name, ro, cands in plan:
        fo = os.path.join(od, f'{ro:x}_{name}.c')
        old_txt = open(fo, encoding='utf-8').read() if os.path.exists(fo) else ''
        best = None
        for c in cands:
            fn = os.path.join(nd, f'{c:x}_{name}.c')
            new_txt = open(fn, encoding='utf-8').read() if os.path.exists(fn) else ''
            d = list(difflib.unified_diff(norm(old_txt), norm(new_txt), 'old', 'new', lineterm='', n=1))
            if best is None or len(d) < len(best[1]):
                best = (c, d, new_txt)
        verdict = 'no-candidate' if best is None else ('identical' if not best[1] else f'diff {len(best[1])} lines')
        stats[verdict.split()[0]] = stats.get(verdict.split()[0], 0) + 1
        newc = f'{best[0]:#x}' if best else '-'
        lines.append(f'## {"/".join(r["names"])} ({", ".join(sorted(set(r["frameworks"])))}) -- {r["status"]}, {verdict}')
        lines.append(f'- hash `{h}`, old `{ro:#x}` -> new `{newc}`; uses: {r["uses"][0]}')
        if best and best[1]:
            lines.append('```diff')
            lines += best[1][:120]
            lines.append('```')
        lines.append('')
    lines.insert(1, '- ' + ', '.join(f'{k} {v}' for k, v in sorted(stats.items())))
    with open(os.path.join(a.out, 'REPORT.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print(stats)
    print('report:', os.path.join(a.out, 'REPORT.md'))


if __name__ == '__main__':
    main()
