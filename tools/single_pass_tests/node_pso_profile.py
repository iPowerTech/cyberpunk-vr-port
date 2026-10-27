"""GPU time of one render-graph node per pipeline state and view (live owned launch).

Grouped scene profiles (CyberpunkVR_SceneGpuRequest) with CyberpunkVR_SceneGpuGroupMode=0 (one
range per run of commands with the same PSO) and CyberpunkVR_SceneGpuNodeFilter=<node>. For each
(view, PSO, detail) it reports ranges, commands and milliseconds per range, so two views running
the same node can be compared pass by pass: different PSOs or command counts mean different work,
the same PSOs and counts with different times mean overlap or different input sizes.
"""
import argparse
import json
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(ROOT / 'tools/quickboot_tests'), str(ROOT / 'tools/roomscale_tests'),
                str(ROOT / 'tools/swimming_tests'), str(HERE)]


def by_pso(profiles):
    """(side, pso, detail) -> ranges, commands, total ms, ms per range, commands per range; in first-seen order."""
    rows = {}
    for value in profiles:
        for row in value.get('groups', []):
            if row.get('side') not in (0, 1):
                continue
            key = (row['side'], row['pso'], row.get('detail', 0))
            entry = rows.setdefault(key, dict(side=row['side'], pso=hex(row['pso']), detail=row.get('detail', 0),
                                              ranges=0, commands=0, ms=0.0, maxMs=0.0))
            entry['ranges'] += row['ranges']
            entry['commands'] += row['commands']
            entry['ms'] += row['sum_ms']
            entry['maxMs'] = max(entry['maxMs'], row['max_ms'])
    for entry in rows.values():
        entry['msPerRange'] = entry['ms'] / entry['ranges'] if entry['ranges'] else 0.0
        entry['commandsPerRange'] = entry['commands'] / entry['ranges'] if entry['ranges'] else 0.0
    return list(rows.values())


def pair(rows):
    """Join the two views by PSO and detail: [(pso, detail, main row or None, vrcam row or None)]."""
    keys, seen = [], set()
    for row in rows:
        key = (row['pso'], row['detail'])
        if key not in seen:
            seen.add(key)
            keys.append(key)
    index = {(row['side'], row['pso'], row['detail']): row for row in rows}
    return [(pso, detail, index.get((0, pso, detail)), index.get((1, pso, detail))) for pso, detail in keys]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--node', action='append', required=True, help='node work RVA in hex')
    parser.add_argument('--profiles', type=int, default=6)
    args = parser.parse_args()
    import geometry_cost_live as live
    import geometry_cost_report as cost
    import launch_to_save as boot
    import view_node_profile as vnp
    from live_read import Reader
    from live_symbols import resolve
    state = live.load_state(args.state)
    pid = state['pid']
    game = boot.DEFAULT_GAME
    check = live.checker(pid, state['started'], state['baselineReporters'])
    symbols = resolve(pid, game / live.DLL, list(cost.NAMES))
    reader = Reader(pid, game / 'bin/x64/Cyberpunk2077.exe', 0, 0)
    probe = cost.LiveProbe(reader, symbols, pid, check)
    names = vnp.node_names()
    result = {}
    try:
        probe.write32('CyberpunkVR_RuntimeDiagnostics', 1)
        probe.write32('CyberpunkVR_SceneGpuGroupMode', 0)
        for text in args.node:
            node = int(text, 16)
            probe.write32('CyberpunkVR_SceneGpuNodeFilter', node)
            time.sleep(.5)
            profiles = [probe.profile() for _ in range(args.profiles)]
            rows = by_pso(profiles)
            result[hex(node)] = dict(name=names.get(node, '?'), rows=rows)
            print('== %s %s' % (names.get(node, '?'), hex(node)))
            for pso, detail, m, v in pair(rows):
                fmt = lambda r: '%4d x %6.3f ms %6.1f cmd' % (r['ranges'], r['msPerRange'], r['commandsPerRange']) if r else ' ' * 26
                print('  pso %-14s d%-2d MAIN %s | VRCAM %s' % (pso, detail, fmt(m), fmt(v)))
    finally:
        cleanup = probe.restore()
        reader.close()
    args.out.write_text(json.dumps(dict(pid=pid, cleanup=cleanup, nodes=result), indent=1) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
