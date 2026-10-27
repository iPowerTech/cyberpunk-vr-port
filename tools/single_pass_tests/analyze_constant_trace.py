"""Summarize one immutable command/fence trace without opening a game or replay device."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct


def analyze(data):
    if not data or len(data) % 64 or len(data) > 131072 * 64:
        raise ValueError('Invalid constant trace size')
    events = list(struct.iter_unpack('<8Q', data))
    if any(not 1 <= e[0] <= 20 for e in events):
        raise ValueError('Unknown trace operation')
    fences = defaultdict(lambda: dict(signals=0, waits=0, cpu=0, signalQueues=set(), cpuValues=Counter(), callers=Counter()))
    draws, bindings, uploads = {}, defaultdict(set), {}
    query_slots, callers, statuses = Counter(), Counter(), Counter()
    submission = None
    for op, *a in events:
        if op == 7:
            if submission is not None:
                raise ValueError('Nested submission')
            submission = a[2]
        elif op == 8:
            if submission is None or submission <= 0:
                raise ValueError('Orphan submission list')
            submission -= 1
        elif op == 9:
            if submission != 0:
                raise ValueError('Incomplete submission')
            submission = None
        elif submission is not None:
            raise ValueError('Interleaved submission')
        if op == 2:
            uploads[a[0]] = dict(address=a[3], bytes=a[4], committed=bool(a[5]))
        elif op == 10:
            fences[a[0]]['baseline'] = a[1]
        elif op == 11:
            f = fences[a[1]]
            f['signals'] += 1
            f['signalQueues'].add(a[0])
        elif op == 12:
            fences[a[1]]['waits'] += 1
        elif op == 13:
            label = f"exe+{a[2]-a[3]:x}" if 0 <= a[2]-a[3] < 0x10000000 else hex(a[2])
            key = f"{label}:nested={a[4]}"
            callers[key] += 1
            f = fences[a[0]]
            f['cpu'] += 1
            f['cpuValues'][str(a[1])] += 1
            f['callers'][key] += 1
        elif op == 16:
            draws[a[0]] = dict(sequence=a[1], family=a[2], side=a[3], frame=a[4])
        elif op == 17:
            bindings[a[0], a[3], a[4]].add(f'{a[1]}:b{a[2]}')
        elif op == 18:
            if a[0] not in draws or not bindings[a[0], a[1], a[2]] or a[3] > 7 or a[4] > 7:
                raise ValueError('Unidentified query')
            statuses[f'{a[3]}:{a[4]}'] += 1
            for slot in bindings[a[0], a[1], a[2]]:
                query_slots[f'{slot}:side={draws[a[0]]["side"]}:status={a[3]}:detail={a[4]}'] += 1
    if submission is not None:
        raise ValueError('Unterminated submission')
    rows = []
    for address, values in fences.items():
        rows.append(dict(address=hex(address), **dict(values, signalQueues=[hex(q) for q in sorted(values['signalQueues'])],
                                                     cpuValues=dict(values['cpuValues']), callers=dict(values['callers']))))
    return dict(events=len(events), operations=dict(Counter(e[0] for e in events)), draws=len(draws),
                queryStatuses=dict(statuses), querySlots=dict(query_slots), cpuCallers=dict(callers),
                fences=sorted(rows, key=lambda f: (-f['cpu'], -f['waits'])),
                observedWrites=len(uploads), committedWrites=sum(v['committed'] for v in uploads.values()))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    report = analyze(a.trace.read_bytes())
    a.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('fences', 'querySlots')}, indent=2))
