"""Replace one hex literal on one line, for struct offsets whose value means a different field per struct.

    py -3.13 -B tools/patchday/line_swap.py <table.json> [--dry]

The table is a list of ["src/file.cpp", line, "0x1E10", "0x2C40"] rows, made by hand after struct_drift.py /
the per-struct maps say which struct a line reads. Every row must find its old literal on its line as a whole
literal (0x1E10, not 0x1E100) or nothing is written; the digit case of the old literal is kept, as are the
file's line endings. Several rows may name the same line.
"""
import argparse
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('table')
    ap.add_argument('--dry', action='store_true')
    a = ap.parse_args()
    rows = json.load(open(a.table, encoding='utf-8'))
    by_file = collections.defaultdict(list)
    for f, ln, old, new in rows:
        by_file[f.replace('\\', '/')].append((int(ln), old, new))
    errors, edits = [], {}
    for f, items in by_file.items():
        p = os.path.join(REPO, f)
        text = open(p, encoding='utf-8', newline='').read()
        lines = text.split('\n')
        per_line = collections.defaultdict(list)
        for ln, old, new in items:
            per_line[ln].append((old, new))
        for ln, swaps in sorted(per_line.items()):
            if not (1 <= ln <= len(lines)):
                errors.append(f'{f}:{ln}: no such line')
                continue
            # All swaps of a line read the ORIGINAL line: the n-th row naming a literal takes its n-th
            # occurrence, so 0x8C0 -> 0x8D0 next to 0x8D0 -> 0x8E0 cannot chain.
            line = lines[ln - 1]
            literals = [(m, int(m.group(0), 16)) for m in re.finditer(r'\b0[xX][0-9A-Fa-f]+\b', line)]
            used, reps = set(), []
            for old, new in swaps:
                want = int(old, 16)
                k = next((k for k, (m, v) in enumerate(literals) if v == want and k not in used), None)
                if k is None:
                    errors.append(f'{f}:{ln}: {old} not on the line: {line.strip()[:100]}')
                    continue
                used.add(k)
                m = literals[k][0]
                digits = m.group(0)[2:]
                lower = any(c in 'abcdef' for c in digits) and not any(c in 'ABCDEF' for c in digits)
                v = int(new, 16)
                reps.append((m.start(), m.end(), m.group(0)[:2] + (f'{v:x}' if lower else f'{v:X}'), m.group(0)))
            for s, e, rep, was in sorted(reps, reverse=True):
                line = line[:s] + rep + line[e:]
                print(f'{f}:{ln}: {was} -> {rep}')
            lines[ln - 1] = line
        edits[p] = '\n'.join(lines)
    if errors:
        print('\n'.join(errors))
        sys.exit(1)
    if not a.dry:
        for p, t in edits.items():
            open(p, 'w', encoding='utf-8', newline='').write(t)
    print(len(rows), 'rows', 'checked' if a.dry else 'written')


if __name__ == '__main__':
    main()
