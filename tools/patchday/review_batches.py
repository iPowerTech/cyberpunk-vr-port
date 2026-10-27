"""Reading batches for the hand review of every changed function the first pass did not read.

    py -3.13 -B tools/patchday/review_batches.py [--out build/patchday/diaphora-2.5/review]

Takes changes.tsv / changes_diffs.jsonl (diaphora_explain.py), leaves out what review_used.tsv already holds, and
writes the rest, sorted by subsystem, as review/batch_NNN.txt (150 functions each, about 25k tokens):

    #<n> <old>><new> <change> <subsystem> | <summary>
    -/+ diff lines

The diff keeps only + and - lines; a - and a + line that differ only in numbers are dropped (those moves are in
the summary's "numbers" field), at most 20 lines per function, 150 characters per line. review/index.tsv maps n
to old/new; verdicts go to review/verdicts.tsv (n, classes, note), the classes of review_used_verdicts.py.
"""
import argparse
import collections
import csv
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
DIFF = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'build', 'patchday', 'diaphora-2.5')
NUM = re.compile(r'\b0x[0-9A-Fa-f]+\b|\b\d+(?:LL|uLL|u|i64)?\b')
PER_BATCH = 150
MAX_LINES = 20


def compress(diff):
    lines = [l for l in diff if l[:1] in '+-']
    norm = lambda l: NUM.sub('N', l[1:].strip())
    common = collections.Counter(norm(l) for l in lines if l[0] == '-') & \
        collections.Counter(norm(l) for l in lines if l[0] == '+')
    drop_m, drop_p = collections.Counter(common), collections.Counter(common)
    keep = []
    for l in lines:
        k = norm(l)
        bucket = drop_m if l[0] == '-' else drop_p
        if bucket[k] > 0:
            bucket[k] -= 1
            continue
        keep.append(l[:150])
    return keep


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=os.path.join(DIFF, 'review'))
    a = ap.parse_args()
    done = {r['old'] for r in csv.DictReader(open(os.path.join(DIFF, 'review_used.tsv'), encoding='utf-8', newline='\n'),
                                             delimiter='\t')}
    rows = {}
    for r in csv.DictReader(open(os.path.join(DIFF, 'changes.tsv'), encoding='utf-8', newline='\n'), delimiter='\t'):
        if r['change'] in ('logic', 'constants', 'calls', 'no pseudocode') and r['old'] not in done:
            rows[(r['old'], r['new'])] = r
    diffs = {}
    for line in open(os.path.join(DIFF, 'changes_diffs.jsonl'), encoding='utf-8'):
        d = json.loads(line)
        if (d['old'], d['new']) in rows:
            diffs[(d['old'], d['new'])] = d.get('diff') or []
    order = sorted(rows, key=lambda k: (rows[k]['subsystem'], rows[k]['change'], int(k[0], 16)))
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, 'index.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('n\told\tnew\tchange\tsubsystem\n')
        for n, k in enumerate(order):
            f.write('%d\t%s\t%s\t%s\t%s\n' % (n, k[0], k[1], rows[k]['change'], rows[k]['subsystem']))
    for b in range(0, len(order), PER_BATCH):
        out = []
        for n in range(b, min(b + PER_BATCH, len(order))):
            k = order[n]
            r = rows[k]
            out.append('#%d %s>%s %s %s | %s' % (n, k[0], k[1], r['change'], r['subsystem'][:28], r['summary'][:170]))
            keep = compress(diffs.get(k, []))
            out += keep[:MAX_LINES]
            if len(keep) > MAX_LINES:
                out.append('  ...+%d lines' % (len(keep) - MAX_LINES))
        open(os.path.join(a.out, 'batch_%03d.txt' % (b // PER_BATCH)), 'w', encoding='utf-8', newline='\n').write(
            '\n'.join(out) + '\n')
    print(len(order), 'functions,', (len(order) + PER_BATCH - 1) // PER_BATCH, 'batches')


if __name__ == '__main__':
    main()
