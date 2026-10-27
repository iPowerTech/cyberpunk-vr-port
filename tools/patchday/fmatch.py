"""How alike is an old function to each new candidate: token similarity of their instruction streams.

    py -3.13 -B tools/patchday/fmatch.py 0x1d49540 0x424b04 0x424d08 [...]
    py -3.13 -B tools/patchday/fmatch.py 0x4e3eb4 --gone [--top 6]     # candidates from gone_find

Tokens are site_find's (mnemonic + operands, RIP targets and callees by address-library hash), compared with
difflib's ratio; an inlined copy shows up as a candidate whose ratio is low but whose "contains" score (the share
of the old tokens found as one block inside the candidate) is high.
"""
import argparse
import difflib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import sigs  # noqa: E402
from gone_find import Build  # noqa: E402
from site_find import Side, tokens  # noqa: E402


def toks(side, f):
    return tokens(side, sigs.decode(side.img, f[0], f[1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('old')
    ap.add_argument('cands', nargs='*')
    ap.add_argument('--top', type=int, default=6)
    a = ap.parse_args()
    old = Side(Build(os.path.join(REPO, 'build', 'patchday', 'base-2.31')))
    new = Side(Build(os.path.join(REPO, 'build', 'patchday', 'base-2.5')))
    of = old.img.function_at(int(a.old, 16))
    to = toks(old, of)
    print(f'old {of[0]:#x} ({of[1] - of[0]} B, {len(to)} insns)')
    rows = []
    for c in a.cands:
        nf = new.img.function_at(int(c, 16))
        if not nf:
            print(f'  {c}: no function')
            continue
        tn = toks(new, nf)
        sm = difflib.SequenceMatcher(None, to, tn, autojunk=False)
        blocks = sm.get_matching_blocks()
        longest = max((b.size for b in blocks), default=0)
        matched = sum(b.size for b in blocks)
        rows.append((sm.ratio(), nf, len(tn), matched / max(1, len(to)), longest))
    for ratio, nf, n, contains, longest in sorted(rows, key=lambda r: -r[0]):
        print(f'  {nf[0]:#x} ({nf[1] - nf[0]} B, {n} insns): ratio {ratio:.3f}, old tokens matched {contains:.2f}, '
              f'longest run {longest}')


if __name__ == '__main__':
    main()
