"""Cross-check remap's function addresses against Diaphora's function matches.

    py -3.13 -B tools/patchday/remap_vs_diaphora.py [--map build/patchday/check-6ad55daf/remap/map.json]
        [--diaphora build/patchday/diaphora-2.5/matched.tsv] [--old base-2.31] [--new base-2.5]

remap maps every address the port names on its own evidence (same code, a byte signature, data deltas). A byte
signature that is only a common prologue (mov rax,rsp / save the argument registers / push everything) can land
on an unrelated function: 2.31 sub_1401FAFA4 (the submission point) became 2.5 sub_140329AD0 that way, and the
port's detour then ran a different function with the old arguments (a write through garbage). Diaphora matched
the two databases by structure and call graph, independently. For every mapped code address the old function is
looked up in Diaphora's table; a mapping that lands outside the function Diaphora pairs it with is reported,
with both candidates' prologue bytes. Data addresses and old addresses Diaphora left unmatched are listed apart.
"""
import argparse
import csv
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import Image  # noqa: E402

PD = os.path.join(REPO, 'build', 'patchday')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', default=os.path.join(PD, 'check-6ad55daf', 'remap', 'map.json'))
    ap.add_argument('--diaphora', default=os.path.join(PD, 'diaphora-2.5', 'matched.tsv'))
    ap.add_argument('--old', default=os.path.join(PD, 'base-2.31', 'bin', 'x64', 'Cyberpunk2077.exe'))
    ap.add_argument('--new', default=os.path.join(PD, 'base-2.5', 'bin', 'x64', 'Cyberpunk2077.exe'))
    a = ap.parse_args()
    old, new = Image(a.old), Image(a.new)
    pairs = {}
    with open(a.diaphora, encoding='utf-8') as f:
        for row in csv.DictReader(f, delimiter='\t'):
            pairs[int(row['old'], 16)] = (int(row['new'], 16), row['type'], float(row['ratio']), row['desc'])
    m = json.load(open(a.map, encoding='utf-8'))
    agree, disagree, unmatched, data = [], [], [], 0
    for k, v in sorted(m.items(), key=lambda kv: int(kv[0], 16)):
        if not v.get('new'):
            continue
        o, n = int(k, 16), int(v['new'], 16)
        fo = old.function_at(o)
        if not fo:
            data += 1
            continue
        root = fo[2]   # a cold fragment belongs to its parent, which is what Diaphora pairs
        if root not in pairs:
            unmatched.append((o, n, v['status']))
            continue
        dn, dtype, ratio, desc = pairs[root]
        fnew = new.function_at(n)
        # inside the paired function (or one of its fragments) is agreement; the offset is remap's business
        inside = fnew is not None and fnew[2] == dn
        (agree if inside else disagree).append((o, n, v['status'], fo, dn, dtype, ratio, desc, fnew))
    print(f'{len(agree)} agree with Diaphora, {len(disagree)} disagree, {len(unmatched)} old functions Diaphora '
          f'did not match, {data} data/unknown addresses')
    for o, n, status, fo, dn, dtype, ratio, desc, fnew in disagree:
        print(f'DISAGREE old {o:#x} (func {fo[2]:#x}) remap -> {n:#x} [{status}] '
              f'(func {fnew[2]:#x})' if fnew else f'DISAGREE old {o:#x} remap -> {n:#x} [{status}] (no func)', end='')
        print(f' | Diaphora func -> {dn:#x} [{dtype} {ratio:.2f} {desc}]')
        print(f'      old  {bytes(old.read(fo[2], 24)).hex(" ")}')
        if fnew:
            print(f'      remap {bytes(new.read(fnew[2], 24)).hex(" ")}')
        print(f'      diaph {bytes(new.read(dn, 24)).hex(" ")}')
    for o, n, status in unmatched:
        print(f'UNMATCHED old {o:#x} remap -> {n:#x} [{status}]')


if __name__ == '__main__':
    main()
