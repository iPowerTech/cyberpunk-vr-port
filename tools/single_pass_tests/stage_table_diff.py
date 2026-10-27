"""Which descriptors of a RenderElements stage are view-dependent (live owned launch).

StageReplay (src/Render/StageReplay.cpp) replays VRCAM's commands into MAIN and substitutes only
the camera constant buffers. A stage whose draws bind any other per-view descriptor (a view
constant buffer, a view texture) renders MAIN with VRCAM's data. This tool turns on
`CyberpunkVR_StageReplayTableDump` and runs one census with the stage's streams kept: every root
table bind then carries the descriptors the engine flushed it from. MAIN's and VRCAM's draws are
aligned (same pipeline and draw arguments), and every table entry that differs between the two
views is counted as a (VRCAM descriptor -> MAIN descriptor) pair. A pair seen in many draws is a
per-view resource; the camera buffers appear as such a pair too.

Run with StageReplay mode 0 (both views record natively).
"""
import argparse
import collections
import difflib
import json
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(ROOT / 'tools/quickboot_tests'), str(ROOT / 'tools/roomscale_tests'),
                str(ROOT / 'tools/swimming_tests'), str(HERE)]

NAMES = ('CyberpunkVR_StageReplayTableDump', 'CyberpunkVR_StageStreamContextDump', 'CyberpunkVR_StageStreamStateDump',
         'CyberpunkVR_StageStreamContextAddr')
NO_PAYLOAD = 2 ** 64 - 1


def table_payload(row, payload):
    """(group, descriptors) of a RootTable row that carries its contents (flag 16), else None."""
    if row['op'] != 'RootTable' or not row['flags'] & 16 or row['d'] == NO_PAYLOAD or row['d'] + 8 > len(payload):
        return None
    head, = struct.unpack_from('<Q', payload, row['d'])
    count, group = head & 0xFFFFFFFF, head >> 32
    if not count or count > 256 or row['d'] + 8 * (count + 1) > len(payload):
        return None
    return group, struct.unpack_from('<%dQ' % count, payload, row['d'] + 8)


def draw_states(commands, payload):
    """Per draw: pipeline, draw arguments and the bound tables {parameter: descriptors or None}."""
    out, pso, tables = [], 0, {}
    for row in commands:
        if row['op'] == 'Pipeline':
            pso = row['b']
        elif row['op'] == 'RootTable':
            entry = table_payload(row, payload)
            tables = dict(tables)
            tables[row['a']] = entry[1] if entry else None
        elif row['op'] in ('Draw', 'DrawIndexed', 'Indirect'):
            out.append(dict(token=(row['op'], pso, row['a'], row['b'] & 0xFFFFFFFF), tables=tables))
    return out


def compare(main, vrcam, camera=()):
    """Align the two views' draws and count differing table entries per (parameter, index)."""
    matcher = difflib.SequenceMatcher(a=[d['token'] for d in main], b=[d['token'] for d in vrcam], autojunk=False)
    pairs = [(main[b.a + k], vrcam[b.b + k]) for b in matcher.get_matching_blocks() for k in range(b.size)]
    slots = collections.defaultdict(lambda: dict(equal=0, differ=0, pairs=collections.Counter()))
    unknown = 0
    for m, v in pairs:
        for param in sorted(set(m['tables']) | set(v['tables'])):
            a, b = m['tables'].get(param), v['tables'].get(param)
            if a is None or b is None or len(a) != len(b):
                unknown += 1
                continue
            for index, (x, y) in enumerate(zip(a, b)):
                slot = slots[(param, index)]
                if x == y:
                    slot['equal'] += 1
                else:
                    slot['differ'] += 1
                    slot['pairs'][(y, x)] += 1
    camera = set(camera)
    rows = []
    for (param, index), slot in sorted(slots.items()):
        if not slot['differ']:
            continue
        top = slot['pairs'].most_common(3)
        rows.append(dict(parameter=param, index=index, equal=slot['equal'], differ=slot['differ'],
                         distinctPairs=len(slot['pairs']),
                         camera=any(y in camera or x in camera for (y, x), _ in top),
                         top=[dict(vrcam=hex(y), main=hex(x), draws=n) for (y, x), n in top]))
    return dict(mainDraws=len(main), vrcamDraws=len(vrcam), matched=len(pairs), unknownTables=unknown, slots=rows)


def locate(slots, dumps):
    """Where each per-view pair lives: offsets at which VRCAM's descriptor sits in VRCAM's dump AND
    MAIN's descriptor sits in MAIN's dump (render context `context+off`, binding state `state+off`)."""
    out = []
    for row in slots:
        for top in row['top'][:1]:
            v, m = int(top['vrcam'], 16), int(top['main'], 16)
            found = []
            for name in ('context', 'state'):
                a, b = dumps['vrcam'][name], dumps['main'][name]
                found += ['%s+0x%X' % (name, 8 * i) for i in range(min(len(a), len(b))) if a[i] == v and b[i] == m]
            out.append(dict(parameter=row['parameter'], index=row['index'], vrcam=top['vrcam'], main=top['main'], at=found))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--stage', type=int, required=True)
    parser.add_argument('--frames', type=int, default=20)
    args = parser.parse_args()
    import stage_replay_live as srl
    srl.NAMES = tuple(srl.NAMES) + NAMES
    session = srl.Session(args.state)
    try:
        session.probe.write32('CyberpunkVR_StageReplayTableDump', 1)
        value, streams = srl.census(session, args.frames, args.stage)
        payloads = {}
        for side in ('main', 'vrcam'):
            info = value['streams'][side]['info']
            payloads[side] = session.raw(info['payload'], info['payloadBytes']) if info['payloadBytes'] else b''
        context = session.array('CyberpunkVR_StageStreamContextDump', 'Q', 512)
        state = session.array('CyberpunkVR_StageStreamStateDump', 'Q', 128)
        dumps = {side: dict(context=list(context[i * 256:(i + 1) * 256]), state=list(state[i * 64:(i + 1) * 64]))
                 for i, side in enumerate(('main', 'vrcam'))}
    finally:
        session.probe.write32('CyberpunkVR_StageReplayTableDump', 0)
        session.reader.close()
    camera = [value['streams'][s]['info'][k] for s in ('main', 'vrcam') for k in ('camVs', 'camPs')]
    result = compare(draw_states(streams['main']['commands'], payloads['main']),
                     draw_states(streams['vrcam']['commands'], payloads['vrcam']), camera)
    result['stage'] = args.stage
    result['located'] = locate(result['slots'], dumps)
    args.out.write_text(json.dumps(result, indent=1) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('stage', 'mainDraws', 'vrcamDraws', 'matched', 'unknownTables')}))
    for row in result['slots']:
        print('param %2d index %3d  equal %4d differ %4d  distinct %3d camera %-5s top %s' % (
            row['parameter'], row['index'], row['equal'], row['differ'], row['distinctPairs'], row['camera'],
            ', '.join('%s->%s x%d' % (t['vrcam'], t['main'], t['draws']) for t in row['top'][:2])))
    for row in result['located']:
        print('param %2d index %3d  %s -> %s  at %s' % (row['parameter'], row['index'], row['vrcam'], row['main'], row['at']))


if __name__ == '__main__':
    main()
