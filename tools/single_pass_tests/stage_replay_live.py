"""Live control and census of single-pass stage replay (src/Render/StageReplay.cpp).

  census - count every RenderElements consumer call per (view, stage) for N frames, with QPC time,
           D3D12 command mix, camera slots and descriptor-copy coverage; record one stage's complete
           command stream for both views and compare them.
  set    - write mode / stage mask / source side.
  stats  - read replay counters (captures, replays, fallbacks by reason).
  shot   - simulator screenshot of both eyes.

Operates only on an owned launch (geometry_cost_live.py state file). Census controls are restored.
"""
import argparse
import ctypes as c
import difflib
import json
from pathlib import Path
import re
import struct
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(ROOT / 'tools/quickboot_tests'), str(ROOT / 'tools/roomscale_tests'),
                str(ROOT / 'tools/swimming_tests'), str(HERE)]
HEADER = ROOT / 'include/Render/StageReplay.hpp'
SOURCE = ROOT / 'src/Render/StageReplay.cpp'
NAMES = ('CyberpunkVR_StageReplayMode', 'CyberpunkVR_StageReplayStages', 'CyberpunkVR_StageReplaySource',
         'CyberpunkVR_StageCensusRequest', 'CyberpunkVR_StageCensusState', 'CyberpunkVR_StageStreamStage',
         'CyberpunkVR_StageCensusRows', 'CyberpunkVR_StageCensusOps', 'CyberpunkVR_StageCensusSummary',
         'CyberpunkVR_StageStreamInfo', 'CyberpunkVR_StageReplayStats', 'CyberpunkVR_StageEvents',
         'CyberpunkVR_StageEventCount', 'CyberpunkVR_VrcamGbufferPartitions', 'CyberpunkVR_VrcamGbufferPartitionsState',
         'CyberpunkVR_StageReplayAllowBarriers', 'CyberpunkVR_StageReplayWaitUs', 'CyberpunkVR_StageReplayCameraDiag')
BARRIER = struct.Struct('<IIQIII4x')
EVENT_KINDS = ('forward', 'capture', 'skip', 'replay', 'fallback', 'present')
COMMAND = struct.Struct('<BBHIQQQ')
SIDES = ('main', 'vrcam')


def enum_names(text, name):
    body = re.search(r'enum class ' + name + r'[^{]*\{(.*?)\};', text, re.S).group(1)
    body = re.sub(r'//[^\n]*', '', body)
    return [token.split('=')[0].strip() for token in body.replace('\n', ' ').split(',') if token.strip()]


def op_names(text=None):
    return enum_names(text if text is not None else HEADER.read_text(encoding='utf-8'), 'Op')


def fallback_names(text=None):
    return enum_names(text if text is not None else HEADER.read_text(encoding='utf-8'), 'Fallback')


def columns(text=None):
    text = text if text is not None else SOURCE.read_text(encoding='utf-8')
    body = re.search(r'enum Column : uint32_t \{(.*?)\};', text, re.S).group(1)
    return [token.strip() for token in body.replace('\n', ' ').split(',') if token.strip()]


def decode_commands(blob, payload, ops):
    rows = []
    for offset in range(0, len(blob) - len(blob) % COMMAND.size, COMMAND.size):
        op, flags, count, a, b, cc, d = COMMAND.unpack_from(blob, offset)
        row = dict(op=ops[op] if op < len(ops) else op, flags=flags, count=count, a=a, b=b, c=cc, d=d)
        if row['op'] == 'Copy' and d != 2 ** 64 - 1 and d + 8 * count <= len(payload):
            row['sources'] = list(struct.unpack_from('<%dQ' % count, payload, d))
        if row['op'] == 'Barrier' and d != 2 ** 64 - 1 and d + BARRIER.size * min(count, 64) <= len(payload):
            row['barriers'] = [dict(zip(('type', 'flags', 'resource', 'subresource', 'before', 'after'),
                                        BARRIER.unpack_from(payload, d + i * BARRIER.size))) for i in range(min(count, 64))]
        rows.append(row)
    return rows


def summarize_stream(commands):
    mix = {}
    for row in commands:
        mix[row['op']] = mix.get(row['op'], 0) + 1
    tables = [row for row in commands if row['op'] == 'RootTable']
    return dict(commands=len(commands), mix=dict(sorted(mix.items(), key=lambda kv: -kv[1])),
                draws=sum(mix.get(name, 0) for name in ('Draw', 'DrawIndexed', 'Indirect')),
                pipelines=len({row['b'] for row in commands if row['op'] == 'Pipeline'}),
                roots=len({row['b'] for row in commands if row['op'] == 'RootSignature'}),
                tables=len(tables), tablesMatched=sum(1 for row in tables if row['count']),
                cameraTables=sum(1 for row in tables if row['flags'] & 2),
                cameraTableParams=sorted({row['a'] for row in tables if row['flags'] & 2}),
                foreign=sum(1 for row in commands if row['flags'] & 1),
                blocking=[row['op'] for row in commands if row['op'] in BLOCKING][:20])


BLOCKING = {'Dispatch', 'Targets', 'Barrier', 'CopyBuffer', 'CopyTexture', 'Clear', 'Discard', 'Query', 'Resolve',
            'Immediate', 'Predication', 'Bundle', 'Reset', 'Other'}


def compare_streams(main, vrcam):
    def tokens(commands):
        out, pso = [], 0
        for row in commands:
            if row['op'] == 'Pipeline':
                pso = row['b']
            elif row['op'] in ('Draw', 'DrawIndexed', 'Indirect'):
                out.append((row['op'], pso, row['a']))
        return out
    a, b = tokens(main), tokens(vrcam)
    matcher = difflib.SequenceMatcher(a=a, b=b, autojunk=False)
    return dict(mainDraws=len(a), vrcamDraws=len(b), drawSimilarity=round(matcher.ratio(), 4),
                matchedDraws=sum(block.size for block in matcher.get_matching_blocks()))


def decode_events(words):
    events = []
    for i in range(0, len(words) - 5, 6):
        head = words[i]
        events.append(dict(frame=head & 0xFFFFFFFF, side=(head >> 32) & 0xFF, stage=(head >> 40) & 0xFF,
                           ordinal=(head >> 48) & 0xFF, kind=EVENT_KINDS[(head >> 56) & 0xFF] if (head >> 56) & 0xFF < len(EVENT_KINDS) else (head >> 56) & 0xFF,
                           thread=words[i + 1], start=words[i + 2], end=words[i + 3], spanBegin=words[i + 4], spanEnd=words[i + 5]))
    return events


def order_summary(events, stage, frequency):
    """Per frame: do all MAIN calls of `stage` finish before VRCAM's first call starts, and vice versa?"""
    frames = {}
    for e in events:
        if e['stage'] != stage or e['side'] > 1:
            continue
        frames.setdefault(e['frame'], {0: [], 1: []})[e['side']].append(e)
    main_first = vrcam_first = overlap = 0
    main_ms, vrcam_ms, gaps = [], [], []
    for calls in frames.values():
        m, v = calls[0], calls[1]
        if not m or not v:
            continue
        m_start, m_end = min(e['start'] for e in m), max(e['end'] for e in m)
        v_start, v_end = min(e['start'] for e in v), max(e['end'] for e in v)
        main_ms.append(1000 * (m_end - m_start) / frequency)
        vrcam_ms.append(1000 * (v_end - v_start) / frequency)
        if m_end <= v_start:
            main_first += 1; gaps.append(1000 * (v_start - m_end) / frequency)
        elif v_end <= m_start:
            vrcam_first += 1; gaps.append(-1000 * (m_start - v_end) / frequency)
        else:
            overlap += 1
    median = lambda xs: sorted(xs)[len(xs) // 2] if xs else None
    return dict(stage=stage, frames=main_first + vrcam_first + overlap, mainFinishesFirst=main_first, vrcamFinishesFirst=vrcam_first,
                overlap=overlap, medianMainSpanMs=median(main_ms), medianVrcamSpanMs=median(vrcam_ms), medianGapMs=median(gaps))


def cpu_per_frame(events, frequency, trim=2):
    """Median per frame of the consumer CPU milliseconds each view spends in RenderElements stages
    (forward, capture, replay and fallback scopes; skips are zero-length), the span from a view's
    first stage start to its last stage end, and the span over both views. Frame-time evidence that
    stays valid when FPS is held by the GPU."""
    import statistics
    frames = {}
    for e in events:
        if e['side'] > 1:
            continue
        frames.setdefault(e['frame'], {0: [], 1: []})[e['side']].append(e)
    rows = []
    for frame in sorted(frames):
        sides = frames[frame]
        if not sides[0] or not sides[1]:
            continue
        cpu = [sum(e['end'] - e['start'] for e in sides[s]) * 1000 / frequency for s in (0, 1)]
        span = [(max(e['end'] for e in sides[s]) - min(e['start'] for e in sides[s])) * 1000 / frequency for s in (0, 1)]
        both = sides[0] + sides[1]
        rows.append((cpu, span, (max(e['end'] for e in both) - min(e['start'] for e in both)) * 1000 / frequency))
    rows = rows[trim:len(rows) - trim] if len(rows) > 2 * trim else rows
    if not rows:
        return None
    med = statistics.median
    return dict(frames=len(rows), mainCpuMs=med(r[0][0] for r in rows), vrcamCpuMs=med(r[0][1] for r in rows),
                totalCpuMs=med(r[0][0] + r[0][1] for r in rows), mainSpanMs=med(r[1][0] for r in rows),
                vrcamSpanMs=med(r[1][1] for r in rows), bothSpanMs=med(r[2] for r in rows))


def summarize_census(rows, ops, summary, cols, opnames, frequency):
    frames = max(1, summary[0])
    out = []
    for index, row in enumerate(rows):
        named = dict(zip(cols, row))
        if not named['Calls']:
            continue
        side, stage = divmod(index, 64)
        mix = {opnames[i]: n for i, n in enumerate(ops[index][:len(opnames)]) if n}
        out.append(dict(side=SIDES[side], stage=stage, callsPerFrame=named['Calls'] / frames,
                        msPerFrame=1000 * named['Ticks'] / frequency / frames,
                        drawsPerFrame=named['Draws'] / frames, commandsPerFrame=named['Commands'] / frames,
                        packetsPerFrame=named['Packets'] / frames, blockingOps=named['BlockingOps'],
                        foreignOps=named['ForeignOps'], maxOrdinal=named['MaxOrdinal'],
                        peerDone=named['PeerDone'], peerPending=named['PeerPending'],
                        camVs=hex(named['CamVs']), camPs=hex(named['CamPs']), cameraSlots=hex(named['CameraSlots']),
                        cameraParams=[i for i in range(64) if named['CameraParams'] >> i & 1],
                        contextOk=named['ContextOk'], shadowValid=named['ShadowValid'],
                        copies=named['Copies'], tables=named['Tables'], args=[hex(named['Args%d' % i]) for i in range(4)],
                        mix=dict(sorted(mix.items(), key=lambda kv: -kv[1]))))
    out.sort(key=lambda r: (r['stage'], r['side']))
    return out


class Session:
    def __init__(self, state_path):
        import geometry_cost_live as live
        import geometry_cost_report as cost
        import launch_to_save as boot
        from live_read import Reader
        from live_symbols import resolve
        self.live = live
        state = live.load_state(state_path)
        self.pid = state['pid']
        game = boot.DEFAULT_GAME
        self.check = live.checker(self.pid, state['started'], state['baselineReporters'])
        self.symbols = resolve(self.pid, game / live.DLL, list(cost.NAMES) + list(NAMES))
        self.reader = Reader(self.pid, game / 'bin/x64/Cyberpunk2077.exe', 0, 0)
        self.probe = cost.LiveProbe(self.reader, self.symbols, self.pid, self.check)

    def raw(self, address, size):
        # The probe's handle is write-only; reads go through the live reader.
        out = bytearray()
        while len(out) < size:
            chunk = min(1 << 20, size - len(out))
            out += self.reader.read(address + len(out), chunk)
        return bytes(out)

    def array(self, name, fmt, count):
        data = self.raw(self.probe.address(name), struct.calcsize('<' + fmt) * count)
        return list(struct.unpack('<%d%s' % (count, fmt), data))

    def write64(self, name, value):
        data, done = c.c_uint64(value), c.c_size_t()
        if not self.probe.k.WriteProcessMemory(self.probe.handle, self.probe.address(name), c.byref(data), 8, c.byref(done)) or done.value != 8:
            raise c.WinError(c.get_last_error())

    def close(self):
        self.probe.restore()
        self.reader.close()


def frequency():
    value = c.c_int64()
    c.windll.kernel32.QueryPerformanceFrequency(c.byref(value))
    return value.value


def census(session, frames, stream_stage, timeout=30):
    p = session.probe
    original_stream = p.read32('CyberpunkVR_StageStreamStage')
    try:
        p.write32('CyberpunkVR_StageStreamStage', stream_stage)
        # A finished window leaves state 2; clear it so completion means THIS window.
        p.write32('CyberpunkVR_StageCensusState', 0)
        p.write32('CyberpunkVR_StageCensusRequest', frames)
        deadline = time.monotonic() + timeout
        while p.read32('CyberpunkVR_StageCensusState') != 2:
            session.check()
            if time.monotonic() > deadline:
                raise TimeoutError('Census did not complete')
            time.sleep(.1)
        rows = session.array('CyberpunkVR_StageCensusRows', 'Q', 128 * 32)
        ops = session.array('CyberpunkVR_StageCensusOps', 'I', 128 * 48)
        summary = session.array('CyberpunkVR_StageCensusSummary', 'Q', 16)
        info = session.array('CyberpunkVR_StageStreamInfo', 'Q', 32)
        streams = {}
        names = op_names()
        for side in range(2):
            row = info[side * 16:(side + 1) * 16]
            address, count, payload_address, payload_bytes = row[0], row[1], row[2], row[3]
            if not address or not count:
                continue
            blob = session.raw(address, count * COMMAND.size)
            payload = session.raw(payload_address, payload_bytes) if payload_address and payload_bytes else b''
            commands = decode_commands(blob, payload, names)
            streams[SIDES[side]] = dict(info=dict(zip(('commands', 'count', 'payload', 'payloadBytes', 'overflow', 'list', 'engineFrame', 'stage',
                                                       'camVs', 'camPs', 'heapCpu', 'heapGpu', 'stride', 'nativeHeap', 'sliceBaseCapacity', 'cursor'), row)),
                                        summary=summarize_stream(commands), commands=commands)
        count = min(p.read32('CyberpunkVR_StageEventCount'), 16384)
        events = decode_events(session.array('CyberpunkVR_StageEvents', 'Q', count * 6)) if count else []
    finally:
        p.write32('CyberpunkVR_StageStreamStage', original_stream)
    cols = columns()
    rows2 = [rows[i * 32:(i + 1) * 32] for i in range(128)]
    ops2 = [ops[i * 48:(i + 1) * 48] for i in range(128)]
    value = dict(frames=summary[0], summary=summary, stages=summarize_census(rows2, ops2, summary, cols, op_names(), frequency()),
                 streams={k: dict(info=v['info'], summary=v['summary']) for k, v in streams.items()})
    if 'main' in streams and 'vrcam' in streams:
        value['comparison'] = compare_streams(streams['main']['commands'], streams['vrcam']['commands'])
    freq = frequency()
    stages = sorted({e['stage'] for e in events if e['side'] <= 1})
    value['order'] = [order_summary(events, stage, freq) for stage in stages]
    value['cpu'] = cpu_per_frame(events, freq)
    value['events'] = len(events)
    for name in streams:
        value['streams'][name]['barriers'] = [row.get('barriers') for row in streams[name]['commands'] if row['op'] == 'Barrier'][:4]
    streams['events'] = events
    return value, streams


def stats(session):
    raw = session.array('CyberpunkVR_StageReplayStats', 'Q', 32)
    keys = ('captures', 'captureOverflow', 'captureBusy', 'skipped', 'replayed', 'replayCommands', 'replayTicks',
            'fallbacks', 'cameraTables', 'cameraDescriptors', 'restoreOps')
    out = dict(zip(keys, raw[:11]))
    out['pairedReplays'] = raw[11]
    p = session.probe
    out['partitions'] = p.read32('CyberpunkVR_VrcamGbufferPartitions')
    out['partitionsState'] = p.read32('CyberpunkVR_VrcamGbufferPartitionsState')
    diag = session.array('CyberpunkVR_StageReplayCameraDiag', 'Q', 12)
    out['camera'] = dict(targetCameraUnknown=diag[0], noHeap=diag[1], noSubstitution=diag[2], tablesBuilt=diag[3],
                         tablesReused=diag[4], targetCamera=hex(diag[5]), sourceCamera=hex(diag[6]), lastTableSize=diag[7],
                         ringCode=diag[8], ringHeap=diag[9], ringExhausted=diag[10], ringDescriptors=diag[11])
    out['fallbackReasons'] = {name: raw[16 + i] for i, name in enumerate(fallback_names()[:16]) if raw[16 + i]}
    return out


def parse_stages(text):
    mask = 0
    for part in text.split(','):
        if part:
            mask |= 1 << int(part)
    return mask


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='step', required=True)
    for name in ('census', 'set', 'stats', 'shot'):
        step = sub.add_parser(name)
        step.add_argument('--state', type=Path, required=True)
        if name == 'census':
            step.add_argument('--out', type=Path, required=True)
            step.add_argument('--frames', type=int, default=120)
            step.add_argument('--stream-stage', type=int, default=0xFFFFFFFF)
        if name == 'set':
            step.add_argument('--mode', type=int, required=True)
            step.add_argument('--stages', default='')
            step.add_argument('--source', type=int, default=1)
            step.add_argument('--partitions', type=int)
            step.add_argument('--allow-barriers', type=int)
            step.add_argument('--wait-us', type=int)
        if name == 'shot':
            step.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    session = Session(args.state)
    try:
        if args.step == 'census':
            value, streams = census(session, args.frames, args.stream_stage)
            args.out.write_text(json.dumps(value, indent=1) + '\n', encoding='utf-8')
            args.out.with_suffix('.streams.json').write_text(json.dumps(streams) + '\n', encoding='utf-8')
            print(json.dumps(dict(frames=value['frames'], events=value['events'], cpu=value['cpu'], comparison=value.get('comparison'),
                                  streams={k: v['summary'] for k, v in value['streams'].items()}), indent=1))
            for row in value['stages']:
                print(f"{row['side']:5} st{row['stage']:2} x{row['callsPerFrame']:.2f} {row['msPerFrame']:.3f}ms "
                      f"draws {row['drawsPerFrame']:.0f} cmds {row['commandsPerFrame']:.0f} block {row['blockingOps']} "
                      f"foreign {row['foreignOps']} peer {row['peerDone']}/{row['peerPending']} cam {row['camVs']}/{row['camPs']} "
                      f"slots {row['cameraSlots']} camParams {row['cameraParams']} copies {row['copies']} tables {row['tables']} shadow {row['shadowValid']}")
        elif args.step == 'set':
            p = session.probe
            session.write64('CyberpunkVR_StageReplayStages', parse_stages(args.stages))
            p.write32('CyberpunkVR_StageReplaySource', args.source)
            if args.partitions is not None:
                p.write32('CyberpunkVR_VrcamGbufferPartitions', args.partitions)
            if args.allow_barriers is not None:
                p.write32('CyberpunkVR_StageReplayAllowBarriers', args.allow_barriers)
            if args.wait_us is not None:
                p.write32('CyberpunkVR_StageReplayWaitUs', args.wait_us)
            p.write32('CyberpunkVR_StageReplayMode', args.mode)
            print(json.dumps(dict(mode=p.read32('CyberpunkVR_StageReplayMode'), stages=hex(parse_stages(args.stages)),
                                  source=p.read32('CyberpunkVR_StageReplaySource'))))
        elif args.step == 'stats':
            print(json.dumps(stats(session), indent=1))
        elif args.step == 'shot':
            print(session.live.screenshot(args.out, session.check))
    finally:
        session.reader.close()


if __name__ == '__main__':
    main()
