"""Replace hex literals across src/ and include/ by a reviewed old -> new table.

    py -3.13 -B tools/patchday/literal_swap.py <table.json> [--dry]

The table is {"0x14324C": "0x2735E3", ...}. Only whole literals are replaced (0x14324C, not 0x14324C0), the digit
case of each literal is kept, and include/Core/GameBuild.hpp (the kUnmapped list) is left alone. Prints every file
and the count; for a review of what remap.py could not carry and a human re-found.
"""
import argparse
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PAT = re.compile(r'\b0[xX]([0-9A-Fa-f]+)\b')
SKIP = os.path.normcase(os.path.join(REPO, 'include', 'Core', 'GameBuild.hpp'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('table')
    ap.add_argument('--dry', action='store_true')
    a = ap.parse_args()
    table = {int(k, 16): int(v, 16) for k, v in json.load(open(a.table, encoding='utf-8')).items()}
    total = 0
    for root in ('src', 'include'):
        for d, _, names in os.walk(os.path.join(REPO, root)):
            for nm in names:
                if not nm.endswith(('.cpp', '.hpp', '.h', '.inc', '.asm')):
                    continue
                p = os.path.join(d, nm)
                if os.path.normcase(p) == SKIP:
                    continue
                s = open(p, encoding='utf-8', newline='').read()   # keep each file's line endings
                n = 0

                def rep(m):
                    nonlocal n
                    v = int(m.group(1), 16)
                    if v not in table:
                        return m.group(0)
                    n += 1
                    digits = m.group(1)
                    lower = any(c in 'abcdef' for c in digits) and not any(c in 'ABCDEF' for c in digits)
                    return m.group(0)[:2] + (f'{table[v]:x}' if lower else f'{table[v]:X}')
                t = PAT.sub(rep, s)
                if n:
                    total += n
                    print(f'{os.path.relpath(p, REPO)}: {n}')
                    if not a.dry:
                        open(p, 'w', encoding='utf-8', newline='').write(t)
    print('replaced', total)


if __name__ == '__main__':
    main()
